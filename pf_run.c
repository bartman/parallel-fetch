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
        // display
        struct timeval  next_update;
        struct timeval  update_delta;
        // agents
        pf_ctx_t       *agents;
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

static void pf_state_display (pf_state_t *state, int force);

// ------------------------------------------------------------------------

int 
pf_run (const pf_conf_t *conf)
{
        int rc;
        uint i;
        size_t mask_long_cnt;
        pf_state_t state;

        memset (&state, 0, sizeof (state));
        state.conf = conf;

        // display config
        gettimeofday (&state.next_update, NULL);
        state.update_delta = (struct timeval) {0,10000};

        // allocate agents
        state.agents = calloc (conf->no_agents, sizeof (pf_ctx_t));
        if (!state.agents) BAIL ("failed to allocate array");

        // allocate state masks
        mask_long_cnt = 2 + (conf->no_agents/(sizeof(ulong) * 8));

        state.ctx_has_sock_mask = calloc (mask_long_cnt, sizeof (ulong));
        if (!state.ctx_has_sock_mask) BAIL ("failed to allocate array");

        state.ctx_need_conn_mask = calloc (mask_long_cnt, sizeof (ulong));
        if (!state.ctx_need_conn_mask) BAIL ("failed to allocate array");

        state.ctx_did_conn_mask = calloc (mask_long_cnt, sizeof (ulong));
        if (!state.ctx_did_conn_mask) BAIL ("failed to allocate array");

        // initialize
        DBG (1, "initialzie contexts\n");
        for (i=0; i<conf->no_agents; i++)
                pf_ctx_init (&state.agents[i], conf);

        DBG (1, "main loop\n");
        // main loop
        while (state.no_completed < conf->no_connections) {
                uint max_fd = 0;
                fd_set rd, wr, er;
                uint rdc, wrc, erc;

                FD_ZERO (&rd);
                FD_ZERO (&wr);
                FD_ZERO (&er);
                rdc = wrc = erc = 0;

                DBG (1, "\n------------------------------------------------------------\n");
                pf_state_display (&state, 0);
                DBG (1, "\n");

                // open new sockets
                DBG (2, "\n - open sockets\n");
                while (state.ctx_has_sock_count < conf->no_agents) {

                        pf_ctx_t *ctx;

                        DBG (2, "  ctx_has_sock_mask: ");
                        for (i=0; i<mask_long_cnt; i++)
                                DBG (2, "%08lx ", state.ctx_has_sock_mask[i]);
                        DBG (2, "\n");

                        // find one that is not being used
                        i = find_first_zero_bit (state.ctx_has_sock_mask,
                                        conf->no_agents);
                        if (i >= conf->no_agents)
                                BAIL ("failed to find free agent (cnt=%d/%d)",
                                                state.ctx_has_sock_count, conf->no_agents);

                        DBG (2, "  new socket on agent %u/%u\n", i, conf->no_agents);
                        ctx = &state.agents[i];

                        // start it up
                        rc = pf_ctx_new (ctx);
                        if (rc<0) break;

                        // update state
                        set_bit (i, state.ctx_has_sock_mask);
                        state.ctx_has_sock_count ++;

                        set_bit (i, state.ctx_need_conn_mask);
                        state.ctx_need_conn_count ++;
                }

                // create connections
                DBG (2, "\n - start connections\n");
                while (state.ctx_need_conn_count) {

                        pf_ctx_t *ctx;

                        DBG (2, "  ctx_need_conn_mask: ");
                        for (i=0; i<mask_long_cnt; i++)
                                DBG (2, "%08lx ", state.ctx_need_conn_mask[i]);
                        DBG (2, "\n");

                        // find one that is not being used
                        i = find_first_bit (state.ctx_need_conn_mask,
                                        conf->no_agents);
                        DBG (2, "  %u\n",i);
                        if (i >= conf->no_agents)
                                BAIL ("failed to find agent for connection (cnt=%d/%d)",
                                                state.ctx_need_conn_count, conf->no_agents);

                        DBG (1, "  new connection on agent %u/%u\n", i, conf->no_agents);
                        ctx = &state.agents[i];

                        rc = pf_ctx_connect (ctx);
                        if (rc<0) {
                                DBG (0, "  - failed to connect %u/%u\n", i, conf->no_agents);

                                pf_ctx_close (ctx);
                                pf_ctx_reset (ctx);

                                clear_bit (i, state.ctx_has_sock_mask);
                                state.ctx_has_sock_count --;
                                clear_bit (i, state.ctx_need_conn_mask);
                                state.ctx_need_conn_count --;

                                state.no_failed ++;
                                break;
                        }

                        conf->do_connected (ctx);

                        // update state
                        clear_bit (i, state.ctx_need_conn_mask);
                        state.ctx_need_conn_count --;

                        set_bit (i, state.ctx_did_conn_mask);
                        state.ctx_did_conn_count ++;
                }

                // figure out what to select on
                DBG (2, "\n - select selection\n");
                for (i=0; i<conf->no_agents; i++) {
                        pf_ctx_t *ctx = &state.agents[i];
                        
                        // only those in connection are eligable
                        if (! test_bit (i, state.ctx_did_conn_mask))
                                continue;

                        DBG (2, "  doing IO on %u/%u %s\n", i, conf->no_agents,
                                        (ctx->wants_to_send_more) ? "[W]" : "");

                        if (max_fd < ctx->fd)
                                max_fd = ctx->fd;

                        // we always want exceptions
                        FD_SET (ctx->fd, &er);
                        erc ++;

                        // we always want to read
                        FD_SET (ctx->fd, &rd);
                        rdc ++;

                        // we sometimes want to write
                        if (ctx->wants_to_send_more) {
                                FD_SET (ctx->fd, &wr);
                                wrc ++;
                        }
                }

                // wait for events
                DBG (1, "\n - selecting (r=%u, w=%u, e=%u)\n", rdc, wrc, erc);
                rc = select (max_fd+1, &rd, &wr, &er, NULL);
                DBG (2, "  return %d\n", rc);

                if (rc < 0) {
                        if (errno == EINTR)
                                continue;
                        BAIL ("failed to wait for IO");
                }

                for (i=0; i<conf->no_agents; i++) {
                        pf_ctx_t *ctx = &state.agents[i];
                        int closing = 0;
                        int success = 0;

                        if (FD_ISSET (ctx->fd, &rd)) {

                                DBG (2, "  read on %u/%u\n", i, conf->no_agents);
                                rc = conf->do_recv (ctx);
                                DBG (2, "  %d\n", rc);
                                if (rc<=0) closing = 1;
                                if (rc==0) success = 1;
                        }

                        if (rc>=0 && ctx->wants_to_send_more &&
                                        FD_ISSET (ctx->fd, &wr)) {

                                DBG (2, "  write on %u/%u\n", i, conf->no_agents);
                                rc = conf->do_send (ctx);
                                DBG (2, "  %d\n", rc);
                                if (rc<=0) closing = 1;
                        }

                        if (rc>=0 && FD_ISSET (ctx->fd, &er)) {

                                BAIL ("exception on %u/%u\n", i, conf->no_agents);
                                closing = 1;
                        }

                        if (closing) {
                                DBG (1, "  closing %u/%u\n", i, conf->no_agents);

                                pf_ctx_close (ctx);
                                pf_ctx_reset (ctx);

                                clear_bit (i, state.ctx_has_sock_mask);
                                state.ctx_has_sock_count --;
                                clear_bit (i, state.ctx_did_conn_mask);
                                state.ctx_did_conn_count --;

                                if (success)
                                        state.no_completed ++;
                                else
                                        state.no_failed ++;
                        }
                }
        }
        pf_state_display (&state, 1);
        DBG (0, "\n");
        DBG (0, "finished %d (%d retried)\n", 
                        state.no_completed,
                        state.no_failed);

        return 0;
}

// ------------------------------------------------------------------------

static void 
pf_state_display (pf_state_t *state, int force)
{
        int do_display = force;
        const pf_conf_t *conf = state->conf;

        if (dbg_level > 0)
                do_display = 1;

        else if (!force && dbg_level == 0) {
                struct timeval now;

                gettimeofday (&now, NULL);

                do_display = 0;
                if (! timercmp(&state->next_update, &now, >)) {
                        do_display = 1;

                        timeradd (&now, &state->update_delta, &state->next_update);
                }
        }

        if (do_display)
                printf ("completed %u/%u  (sock %u, need_conn %u, did_conn %u)           \r", 
                        state->no_completed, conf->no_connections, state->ctx_has_sock_count, 
                        state->ctx_need_conn_count, state->ctx_did_conn_count);
}

