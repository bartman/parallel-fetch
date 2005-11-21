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

// ------------------------------------------------------------------------

typedef struct {
        const pf_conf_t *conf;

        // display throdling
        struct timeval  next_update;
        struct timeval  update_delta;

        // timing information
        struct timeval  start_time;

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
} pf_state_t;

// ------------------------------------------------------------------------

static int pf_state_init (pf_state_t *state, const pf_conf_t *conf);
static void pf_state_cleanup (pf_state_t *state);
static int pf_state_open_sockets (pf_state_t *state);
static int pf_state_create_connections (pf_state_t *state);
static int pf_state_prepare_for_io (pf_state_t *state);
static int pf_state_perform_select (pf_state_t *state);
static int pf_state_perform_io (pf_state_t *state);
static void pf_state_display (pf_state_t *state, int force);

// ------------------------------------------------------------------------

int 
pf_run (const pf_conf_t *conf)
{
        int rc;
        pf_state_t state;

        rc = pf_state_init (&state, conf);
        if (rc<0) {
                DBG (1, "failed to init state structure, rc=%d\n", rc);
                return rc;
        }

        DBG (1, "main loop\n");
        // main loop
        while (state.no_completed < conf->no_connections) {

                DBG (1, "\n------------------------------------------------------------\n");
                pf_state_display (&state, 0);
                DBG (1, "\n");

                // open new sockets
                rc = pf_state_open_sockets (&state);
                if (rc<0) {
                        DBG (1, "failed to open sockets, rc=%d\n", rc);
                        return rc;
                }

                // create connections
                rc = pf_state_create_connections (&state);
                if (rc<0) {
                        DBG (1, "failed to open sockets, rc=%d\n", rc);
                        return rc;
                }

                // prepare the select bits
                rc = pf_state_prepare_for_io (&state);
                if (rc<0) {
                        DBG (1, "failed to prepare for IO, rc=%d\n", rc);
                        return rc;
                }

                // wait for IO to become available
                rc = pf_state_perform_select (&state);
                if (rc<0) {
                        if (errno == EINTR)
                                continue;

                        DBG (1, "failed to perform IO select, rc=%d\n", rc);
                        return rc;
                }

                // do the IO operations
                rc = pf_state_perform_io (&state);
                if (rc<0) {
                        DBG (1, "failed to perform IO operations, rc=%d\n", rc);
                        return rc;
                }

        }
        pf_state_display (&state, 1);
        DBG (0, "\n");

        pf_state_cleanup (&state);

        return 0;
}

// ------------------------------------------------------------------------

static int 
pf_state_init (pf_state_t *s, const pf_conf_t *conf)
{
        uint i;

        memset (s, 0, sizeof (*s));
        s->conf = conf;

        // timing
        gettimeofday (&s->start_time, NULL);

        // display config
        s->next_update  = s->start_time;
        s->update_delta = (struct timeval) {0,10000};

        // allocate agents
        s->agents = calloc (conf->no_agents, sizeof (pf_ctx_t));
        if (!s->agents) BAIL ("failed to allocate array");

        // allocate state masks
        s->mask_long_cnt = 2 + (conf->no_agents/(sizeof(ulong) * 8));

        s->ctx_has_sock_mask = calloc (s->mask_long_cnt, sizeof (ulong));
        if (!s->ctx_has_sock_mask) BAIL ("failed to allocate array");

        s->ctx_need_conn_mask = calloc (s->mask_long_cnt, sizeof (ulong));
        if (!s->ctx_need_conn_mask) BAIL ("failed to allocate array");

        s->ctx_did_conn_mask = calloc (s->mask_long_cnt, sizeof (ulong));
        if (!s->ctx_did_conn_mask) BAIL ("failed to allocate array");

        // initialize
        DBG (1, "initialzie contexts\n");
        for (i=0; i<conf->no_agents; i++)
                pf_ctx_init (&s->agents[i], conf);

        return 0;
}

static void
pf_state_cleanup (pf_state_t *s)
{
        free(s->ctx_has_sock_mask);
        free(s->ctx_need_conn_mask);
        free(s->ctx_did_conn_mask);
        free (s->agents);
}

static int 
pf_state_open_sockets (pf_state_t *s)
{
        int rc;
        uint i;
        const pf_conf_t *conf = s->conf;

        DBG (2, "\n - open sockets\n");
        while (s->ctx_has_sock_count < conf->no_agents) {

                pf_ctx_t *ctx;

                DBG (2, "  ctx_has_sock_mask: ");
                for (i=0; i<s->mask_long_cnt; i++)
                        DBG (2, "%08lx ", s->ctx_has_sock_mask[i]);
                DBG (2, "\n");

                // find one that is not being used
                i = find_first_zero_bit (s->ctx_has_sock_mask,
                                conf->no_agents);
                if (i >= conf->no_agents)
                        BAIL ("failed to find free agent (cnt=%d/%d)",
                                        s->ctx_has_sock_count, conf->no_agents);

                DBG (2, "  new socket on agent %u/%u\n", i, conf->no_agents);
                ctx = &s->agents[i];

                // start it up
                rc = pf_ctx_new (ctx);
                if (rc<0) break;

                // update state
                set_bit (i, s->ctx_has_sock_mask);
                s->ctx_has_sock_count ++;

                set_bit (i, s->ctx_need_conn_mask);
                s->ctx_need_conn_count ++;
        }
        return 0;
}

static int 
pf_state_create_connections (pf_state_t *s)
{
        int rc;
        uint i;
        const pf_conf_t *conf = s->conf;

        DBG (2, "\n - start connections\n");
        while (s->ctx_need_conn_count) {

                pf_ctx_t *ctx;

                DBG (2, "  ctx_need_conn_mask: ");
                for (i=0; i<s->mask_long_cnt; i++)
                        DBG (2, "%08lx ", s->ctx_need_conn_mask[i]);
                DBG (2, "\n");

                // find one that is not being used
                i = find_first_bit (s->ctx_need_conn_mask,
                                conf->no_agents);
                DBG (2, "  %u\n",i);
                if (i >= conf->no_agents)
                        BAIL ("failed to find agent for connection (cnt=%d/%d)",
                                        s->ctx_need_conn_count, conf->no_agents);

                DBG (1, "  new connection on agent %u/%u\n", i, conf->no_agents);
                ctx = &s->agents[i];

                rc = pf_ctx_connect (ctx);
                if (rc<0) {
                        DBG (0, "  - failed to connect %u/%u\n", i, conf->no_agents);

                        pf_ctx_close (ctx);
                        pf_ctx_reset (ctx);

                        clear_bit (i, s->ctx_has_sock_mask);
                        s->ctx_has_sock_count --;
                        clear_bit (i, s->ctx_need_conn_mask);
                        s->ctx_need_conn_count --;

                        s->no_failed ++;
                        break;
                }

                conf->do_connected (ctx);

                // update state
                clear_bit (i, s->ctx_need_conn_mask);
                s->ctx_need_conn_count --;

                set_bit (i, s->ctx_did_conn_mask);
                s->ctx_did_conn_count ++;
        }

        return 0;
}

static int 
pf_state_prepare_for_io (pf_state_t *s)
{
        uint i;
        const pf_conf_t *conf = s->conf;

        s->max_fd = 0;
        FD_ZERO (&s->rd_set);
        FD_ZERO (&s->wr_set);
        FD_ZERO (&s->er_set);
        s->rd_cnt = s->wr_cnt = s->er_cnt = 0;

        // figure out what to select on
        DBG (2, "\n - select selection\n");
        for (i=0; i<conf->no_agents; i++) {
                pf_ctx_t *ctx = &s->agents[i];

                // only those in connection are eligable
                if (! test_bit (i, s->ctx_did_conn_mask))
                        continue;

                DBG (2, "  doing IO on %u/%u %s\n", i, conf->no_agents,
                                (ctx->wants_to_send_more) ? "[W]" : "");

                if (s->max_fd < ctx->fd)
                        s->max_fd = ctx->fd;

                // we always want exceptions
                FD_SET (ctx->fd, &s->er_set);
                s->er_cnt ++;

                // we always want to read
                FD_SET (ctx->fd, &s->rd_set);
                s->rd_cnt ++;

                // we sometimes want to write
                if (ctx->wants_to_send_more) {
                        FD_SET (ctx->fd, &s->wr_set);
                        s->wr_cnt ++;
                }
        }

        return 0;
}

static int 
pf_state_perform_select (pf_state_t *s)
{
        int rc;

        DBG (1, "\n - selecting (r=%u, w=%u, e=%u)\n", 
                        s->rd_cnt, s->wr_cnt, s->er_cnt);

        // wait for events
        rc = select (s->max_fd+1, &s->rd_set, &s->wr_set, &s->er_set, NULL);
        DBG (2, "  return %d\n", rc);

        return rc;
}

static int 
pf_state_perform_io (pf_state_t *s)
{
        int rc;
        uint i;
        const pf_conf_t *conf = s->conf;

        for (i=0; i<conf->no_agents; i++) {
                pf_ctx_t *ctx = &s->agents[i];
                int closing = 0;
                int success = 0;

                if (FD_ISSET (ctx->fd, &s->rd_set)) {

                        DBG (2, "  read on %u/%u\n", i, conf->no_agents);
                        rc = conf->do_recv (ctx);
                        DBG (2, "  %d\n", rc);
                        if (rc<=0) closing = 1;
                        if (rc==0) success = 1;
                }

                if (!closing && ctx->wants_to_send_more 
                                && FD_ISSET (ctx->fd, &s->wr_set)) {

                        DBG (2, "  write on %u/%u\n", i, conf->no_agents);
                        rc = conf->do_send (ctx);
                        DBG (2, "  %d\n", rc);
                        if (rc<=0) closing = 1;
                }

                if (!closing && FD_ISSET (ctx->fd, &s->er_set)) {

                        BAIL ("exception on %u/%u\n", i, conf->no_agents);
                        closing = 1;
                }

                if (closing) {
                        DBG (1, "  closing %u/%u\n", i, conf->no_agents);

                        pf_ctx_close (ctx);
                        pf_ctx_reset (ctx);

                        clear_bit (i, s->ctx_has_sock_mask);
                        s->ctx_has_sock_count --;
                        clear_bit (i, s->ctx_did_conn_mask);
                        s->ctx_did_conn_count --;

                        if (success)
                                s->no_completed ++;
                        else
                                s->no_failed ++;
                }
        }

        return 0;
}

static void 
pf_state_display (pf_state_t *s, int force)
{
        int do_display = force;
        const pf_conf_t *conf = s->conf;
        struct timeval now, diff;

        gettimeofday (&now, NULL);

        if (dbg_level > 0)
                do_display = 1;

        else if (!force && dbg_level == 0) {

                do_display = 0;
                if (! timercmp(&s->next_update, &now, >)) {
                        do_display = 1;

                        timeradd (&now, &s->update_delta, &s->next_update);
                }
        }

        if (do_display) {
                double us, conn_per_sec;

                timersub (&now, &s->start_time, &diff);

                us = diff.tv_sec + diff.tv_sec/1000000.0;

                conn_per_sec = s->no_completed / us;

                printf ("completed %u/%u  %f conn/s  "
                        "(sock %u, start %u, conn %u), fail %u           \r", 
                        s->no_completed, conf->no_connections, 
                        conn_per_sec,
                        s->ctx_has_sock_count, s->ctx_need_conn_count, 
                        s->ctx_did_conn_count, s->no_failed);
        }
}

