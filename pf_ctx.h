#ifndef __included__pf_ctx_h__
#define __included__pf_ctx_h__

struct pf_conf_s;

typedef struct pf_ctx_s {
        // the configuration
        const struct pf_conf_s *conf;

        // the socket
        int                     fd;

        // read/write and byte counts
        size_t                  send_cnt;
        size_t                  send_bytes;
        size_t                  recv_cnt;
        size_t                  recv_bytes;

        // flags
        uint32_t                wants_to_send_more:1;
} pf_ctx_t;

extern void pf_ctx_init (pf_ctx_t *ctx, const struct pf_conf_s *conf);
extern void pf_ctx_reset (pf_ctx_t *ctx);
extern int pf_ctx_new (pf_ctx_t *ctx);
extern int pf_ctx_connect (pf_ctx_t *ctx);
extern int pf_ctx_close (pf_ctx_t *ctx);

#endif // __included__pf_ctx_h__
