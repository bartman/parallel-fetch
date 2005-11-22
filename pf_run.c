#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/time.h>
#include <asm/bitops.h>

#include "pf_dbg.h"
#include "pf_ctx.h"
#include "pf_conf.h"
#include "pf_stat.h"

// ------------------------------------------------------------------------

typedef struct {
        const pf_conf_t *conf;
        pf_stat_t       *stat;

        // variables for select
        uint max_fd;
        fd_set rd_set, wr_set, er_set;
        uint rd_cnt, wr_cnt, er_cnt;

        // agents
        pf_ctx_t       *agents;

        // number of words in masks to accomodate the conf->no_agents
        size_t mask_long_cnt;

        // which ones have sockets
        uint            ctx_has_sock_count;
        ulong          *ctx_has_sock_mask;
        // which ones need to connect
        uint            ctx_need_conn_count;
        ulong          *ctx_need_conn_mask;
        // which ones have connected
        uint            ctx_did_conn_count;
        ulong          *ctx_did_conn_mask;

        // what is completed
        uint            no_failed;
        uint            no_completed;
} pf_run_t;

// ------------------------------------------------------------------------

static int pf_run_init (pf_run_t *run, const pf_conf_t *conf, pf_stat_t *stat);
static void pf_run_cleanup (pf_run_t *run);
static int pf_run_open_sockets (pf_run_t *run);
static int pf_run_create_connections (pf_run_t *run);
static int pf_run_prepare_for_io (pf_run_t *run);
static int pf_run_perform_select (pf_run_t *run);
static int pf_run_perform_io (pf_run_t *run);

// ------------------------------------------------------------------------

int 
pf_run (const pf_conf_t *conf, pf_stat_t *stat)
{
        int rc;
        pf_run_t run;

        rc = pf_run_init (&run, conf, stat);
        if (rc<0) {
                DBG (1, "failed to init state structure, rc=%d\n", rc);
                return rc;
        }

        DBG (1, "main loop\n");
        // main loop
        while (run.no_completed < conf->no_connections) {

                DBG (1, "\n------------------------------------------------------------\n");
                DBG (1, "completed %u/%u  (sock %u, start %u, conn %u), fail %u", 
                        run.no_completed, conf->no_connections, 
                        run.ctx_has_sock_count, run.ctx_need_conn_count, 
                        run.ctx_did_conn_count, run.no_failed);
                DBG (1, "\n");

                // open new sockets
                rc = pf_run_open_sockets (&run);
                if (rc<0) {
                        DBG (1, "failed to open sockets, rc=%d\n", rc);
                        return rc;
                }

                // create connections
                rc = pf_run_create_connections (&run);
                if (rc<0) {
                        DBG (1, "failed to open sockets, rc=%d\n", rc);
                        return rc;
                }

                // prepare the select bits
                rc = pf_run_prepare_for_io (&run);
                if (rc<0) {
                        DBG (1, "failed to prepare for IO, rc=%d\n", rc);
                        return rc;
                }

                // wait for IO to become available
                rc = pf_run_perform_select (&run);
                if (rc<0) {
                        if (errno == EINTR)
                                continue;

                        DBG (1, "failed to perform IO select, rc=%d\n", rc);
                        return rc;
                }

                // do the IO operations
                rc = pf_run_perform_io (&run);
                if (rc<0) {
                        DBG (1, "failed to perform IO operations, rc=%d\n", rc);
                        return rc;
                }

        }
        DBG (1, "\n");

        pf_run_cleanup (&run);

        return 0;
}

// ------------------------------------------------------------------------

static int 
pf_run_init (pf_run_t *r, const pf_conf_t *conf, pf_stat_t *stat)
{
        uint i;

        memset (r, 0, sizeof (*r));
        r->conf = conf;
        r->stat = stat;

        // allocate agents
        r->agents = calloc (conf->no_agents, sizeof (pf_ctx_t));
        if (!r->agents) BAIL ("failed to allocate array");

        // allocate state masks
        r->mask_long_cnt = 2 + (conf->no_agents/(sizeof(ulong) * 8));

        r->ctx_has_sock_mask = calloc (r->mask_long_cnt, sizeof (ulong));
        if (!r->ctx_has_sock_mask) BAIL ("failed to allocate array");

        r->ctx_need_conn_mask = calloc (r->mask_long_cnt, sizeof (ulong));
        if (!r->ctx_need_conn_mask) BAIL ("failed to allocate array");

        r->ctx_did_conn_mask = calloc (r->mask_long_cnt, sizeof (ulong));
        if (!r->ctx_did_conn_mask) BAIL ("failed to allocate array");

        // initialize
        DBG (1, "initialzie contexts\n");
        for (i=0; i<conf->no_agents; i++)
                pf_ctx_init (&r->agents[i], conf, stat);

        return 0;
}

static void
pf_run_cleanup (pf_run_t *r)
{
        free(r->ctx_has_sock_mask);
        free(r->ctx_need_conn_mask);
        free(r->ctx_did_conn_mask);
        free (r->agents);
}

static int 
pf_run_open_sockets (pf_run_t *r)
{
        int rc;
        uint i;
        const pf_conf_t *conf = r->conf;

        DBG (2, "\n - open sockets\n");
        while (r->ctx_has_sock_count < conf->no_agents) {

                pf_ctx_t *ctx;

                DBG (2, "  ctx_has_sock_mask: ");
                for (i=0; i<r->mask_long_cnt; i++)
                        DBG (2, "%08lx ", r->ctx_has_sock_mask[i]);
                DBG (2, "\n");

                // find one that is not being used
                i = find_first_zero_bit (r->ctx_has_sock_mask,
                                conf->no_agents);
                if (i >= conf->no_agents)
                        BAIL ("failed to find free agent (cnt=%d/%d)",
                                        r->ctx_has_sock_count, conf->no_agents);

                DBG (2, "  new socket on agent %u/%u\n", i, conf->no_agents);
                ctx = &r->agents[i];

                // start it up
                rc = pf_ctx_new (ctx);
                if (rc<0) break;

                // update state
                set_bit (i, r->ctx_has_sock_mask);
                r->ctx_has_sock_count ++;

                set_bit (i, r->ctx_need_conn_mask);
                r->ctx_need_conn_count ++;
        }
        return 0;
}

static int 
pf_run_create_connections (pf_run_t *r)
{
        int rc;
        uint i;
        const pf_conf_t *conf = r->conf;

        DBG (2, "\n - start connections\n");
        while (r->ctx_need_conn_count) {

                pf_ctx_t *ctx;

                DBG (2, "  ctx_need_conn_mask: ");
                for (i=0; i<r->mask_long_cnt; i++)
                        DBG (2, "%08lx ", r->ctx_need_conn_mask[i]);
                DBG (2, "\n");

                // find one that is not being used
                i = find_first_bit (r->ctx_need_conn_mask,
                                conf->no_agents);
                DBG (2, "  %u\n",i);
                if (i >= conf->no_agents)
                        BAIL ("failed to find agent for connection (cnt=%d/%d)",
                                        r->ctx_need_conn_count, conf->no_agents);

                DBG (1, "  new connection on agent %u/%u\n", i, conf->no_agents);
                ctx = &r->agents[i];

                rc = pf_ctx_connect (ctx);
                if (rc<0) {
                        DBG (0, "  - failed to connect %u/%u\n", i, conf->no_agents);

                        pf_ctx_close (ctx);
                        pf_ctx_reset (ctx);

                        clear_bit (i, r->ctx_has_sock_mask);
                        r->ctx_has_sock_count --;
                        clear_bit (i, r->ctx_need_conn_mask);
                        r->ctx_need_conn_count --;

                        r->no_failed ++;
                        stat_atomic_inc (r->stat,no_failed);
                        break;
                }

                conf->do_connected (ctx);

                // update state
                clear_bit (i, r->ctx_need_conn_mask);
                r->ctx_need_conn_count --;

                set_bit (i, r->ctx_did_conn_mask);
                r->ctx_did_conn_count ++;
        }

        return 0;
}

static int 
pf_run_prepare_for_io (pf_run_t *r)
{
        uint i;
        const pf_conf_t *conf = r->conf;

        r->max_fd = 0;
        FD_ZERO (&r->rd_set);
        FD_ZERO (&r->wr_set);
        FD_ZERO (&r->er_set);
        r->rd_cnt = r->wr_cnt = r->er_cnt = 0;

        // figure out what to select on
        DBG (2, "\n - select selection\n");
        for (i=0; i<conf->no_agents; i++) {
                pf_ctx_t *ctx = &r->agents[i];

                // only those in connection are eligable
                if (! test_bit (i, r->ctx_did_conn_mask))
                        continue;

                DBG (2, "  doing IO on %u/%u %s\n", i, conf->no_agents,
                                (ctx->wants_to_send_more) ? "[W]" : "");

                if (r->max_fd < ctx->fd)
                        r->max_fd = ctx->fd;

                // we always want exceptions
                FD_SET (ctx->fd, &r->er_set);
                r->er_cnt ++;

                // we always want to read
                FD_SET (ctx->fd, &r->rd_set);
                r->rd_cnt ++;

                // we sometimes want to write
                if (ctx->wants_to_send_more) {
                        FD_SET (ctx->fd, &r->wr_set);
                        r->wr_cnt ++;
                }
        }

        return 0;
}

static int 
pf_run_perform_select (pf_run_t *r)
{
        int rc;

        DBG (1, "\n - selecting (r=%u, w=%u, e=%u)\n", 
                        r->rd_cnt, r->wr_cnt, r->er_cnt);

        // wait for events
        rc = select (r->max_fd+1, &r->rd_set, &r->wr_set, &r->er_set, NULL);
        DBG (2, "  return %d\n", rc);

        return rc;
}

static int 
pf_run_perform_io (pf_run_t *r)
{
        int rc;
        uint i;
        const pf_conf_t *conf = r->conf;

        for (i=0; i<conf->no_agents; i++) {
                pf_ctx_t *ctx = &r->agents[i];
                int closing = 0;
                int success = 0;

                if (FD_ISSET (ctx->fd, &r->rd_set)) {

                        DBG (2, "  read on %u/%u\n", i, conf->no_agents);
                        rc = conf->do_recv (ctx);
                        DBG (2, "  %d\n", rc);
                        if (rc<=0) closing = 1;
                        if (rc==0) success = 1;
                }

                if (!closing && ctx->wants_to_send_more 
                                && FD_ISSET (ctx->fd, &r->wr_set)) {

                        DBG (2, "  write on %u/%u\n", i, conf->no_agents);
                        rc = conf->do_send (ctx);
                        DBG (2, "  %d\n", rc);
                        if (rc<=0) closing = 1;
                }

                if (!closing && FD_ISSET (ctx->fd, &r->er_set)) {

                        BAIL ("exception on %u/%u\n", i, conf->no_agents);
                        closing = 1;
                }

                if (closing) {
                        DBG (1, "  closing %u/%u\n", i, conf->no_agents);

                        pf_ctx_close (ctx);
                        pf_ctx_reset (ctx);

                        clear_bit (i, r->ctx_has_sock_mask);
                        r->ctx_has_sock_count --;
                        clear_bit (i, r->ctx_did_conn_mask);
                        r->ctx_did_conn_count --;

                        if (success) {
                                r->no_completed ++;
                                stat_atomic_inc (r->stat,no_completed);
                        } else {
                                r->no_failed ++;
                                stat_atomic_inc (r->stat,no_failed);
                        }
                }
        }

        return 0;
}

