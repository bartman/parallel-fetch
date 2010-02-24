#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "pf_dbg.h"
#include "pf_conf.h"
#include "pf_ctx.h"

// TODO: make this thread-safe
static char http_buf[4096];
static const size_t http_buf_max = 4096;

typedef struct pf_http_s {
	size_t req_len;
	char *request;
} pf_http_t;

int
http_init (pf_ctx_t *ctx)
{
	pf_http_t *http;

	http = malloc(sizeof(*http));
	http->request = NULL;
	http->req_len = asprintf(&http->request,
			"GET %s HTTP/1.0\n"
			"User-Agent: pf\n"
			"\n",
			ctx->conf->path ?: "/");
	ctx->private_data = http;
	return 0;
}

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
	pf_http_t *http = ctx->private_data;

        // only once
        if (ctx->send_cnt)
                return 0;

        rc = write (ctx->fd, http->request, http->req_len);

        if (rc>0) {
                ctx->send_cnt ++;
                ctx->send_bytes += rc;
        }

        if (rc >=0 && rc != http->req_len)
                rc = -EIO;

        ctx->wants_to_send_more = 0;

        return rc;
}

int 
http_closing (pf_ctx_t *ctx, int rc)
{
        return 0;
}
