#ifndef __included__pf_conf_h__
#define __included__pf_conf_h__

#include <sys/types.h>
#include <netinet/in.h>

struct pf_ctx_s;

typedef struct pf_conf_s {

        // host to connect to
        struct sockaddr_in      server;

        // data handlers
        int (*do_connected) (struct pf_ctx_s *ctx);
        int (*do_send) (struct pf_ctx_s *ctx);
        int (*do_recv) (struct pf_ctx_s *ctx);
        int (*do_closing) (struct pf_ctx_s *ctx, int rc);

        // definition of the test
        uint                    no_agents;
        uint                    no_connections;
	uint			kill_switch;

} pf_conf_t;

#endif // __included__pf_conf_h__
