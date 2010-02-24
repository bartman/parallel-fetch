#ifndef __included__pf_http_h__
#define __included__pf_http_h__

struct pf_ctx_s;

extern int http_init (struct pf_ctx_s *ctx);
extern int http_connected (struct pf_ctx_s *ctx);
extern int http_recv (struct pf_ctx_s *ctx);
extern int http_send (struct pf_ctx_s *ctx);
extern int http_closing (struct pf_ctx_s *ctx, int rc);

#endif /* __included__pf_http_h__ */
