#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "pf_dbg.h"
#include "pf_ctx.h"

// TODO: make this thread-safe
static char http_buf[4096];
static const size_t http_buf_max = 4096;

int 
http_connected (pf_ctx_t *ctx)
{
        ctx->wants_to_send_more = 1;
        return 0;
}

int 
http_recv (pf_ctx_t *ctx)
{
        int rc;

        rc = read (ctx->fd, http_buf, http_buf_max);

        if (rc>0) {
                ctx->recv_cnt ++;
                ctx->recv_bytes += rc;

                if (dbg_level >= 3) {
                        fprintf (stdout, "--------------\n");
                        fflush (stdout);
                        write (1, http_buf, rc);
                        fflush (stdout);
                        fprintf (stdout, "--------------\n");
                }
        }

        return rc;
}

int 
http_send (pf_ctx_t *ctx)
{
        int rc;
        //const char get[] = "GET /\n\n";
        const char get[] =
		"GET / HTTP/1.0\n"
		"User-Agent: pf\n"
		"\n";

        // only once
        if (ctx->send_cnt)
                return 0;

        rc = write (ctx->fd, get, sizeof(get));

        if (rc>0) {
                ctx->send_cnt ++;
                ctx->send_bytes += rc;
        }

        if (rc >=0 && rc != sizeof (get))
                rc = -EIO;

        ctx->wants_to_send_more = 0;

        return rc;
}

int 
http_closing (pf_ctx_t *ctx, int rc)
{
        return 0;
}
