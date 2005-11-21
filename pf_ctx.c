#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "pf_dbg.h"
#include "pf_ctx.h"
#include "pf_conf.h"

void
pf_ctx_init (pf_ctx_t *ctx, const pf_conf_t *conf)
{
        memset (ctx, 0, sizeof (*ctx));
        ctx->conf = conf;
        ctx->fd = -1;
}

void
pf_ctx_reset (pf_ctx_t *ctx)
{
        const struct pf_conf_s *conf = ctx->conf;;
        pf_ctx_init (ctx, conf);
}

int 
pf_ctx_new (pf_ctx_t *ctx)
{
        int rc;

        rc = socket (PF_INET, SOCK_STREAM, 0);
        if (rc<0) BAIL ("new socked creation: PF_INET SOCK_STREAM");

        ctx->fd = rc;

        return rc;
}

int 
pf_ctx_connect (pf_ctx_t *ctx)
{
        int rc;

        rc = connect (ctx->fd, (void*)&ctx->conf->server, sizeof (ctx->conf->server));
        if (rc<0) BAIL ("failed to connect to server %08x %04x",
                        ctx->conf->server.sin_addr.s_addr,
                        ctx->conf->server.sin_port);

        return rc;
}

int 
pf_ctx_close (pf_ctx_t *ctx)
{
        if (ctx->fd != -1)
                close (ctx->fd);
        ctx->fd = -1;

        return 0;
}

