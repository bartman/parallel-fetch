#ifndef __included__pf_ctx_h__
#define __included__pf_ctx_h__

struct pf_conf_s;
struct pf_stat_s;

#include <stdint.h>
#include <sys/types.h>

#include "pf_list.h"

enum pf_ctx_state_e {
	PF_CTX_AVAIL,
	PF_CTX_CONN,
	PF_CTX_DELAY_ACTIVE,
	PF_CTX_ACTIVE,
	PF_CTX_DELAY_CLOSE,
	PF_CTX_STATE_MAX
};

typedef struct pf_ctx_s {
	struct list_head	link;
	enum pf_ctx_state_e	state;
	uint 			number;

        // the configuration
        const struct pf_conf_s *conf;
        struct pf_stat_s       *stat;

        // the socket
        int                     fd;

        // read/write and byte counts
        size_t                  send_cnt;
        size_t                  send_bytes;
        size_t                  recv_cnt;
        size_t                  recv_bytes;

	// flags
	time_t			delay_finish_time;
	uint32_t                wants_to_send_more:1;
} pf_ctx_t;

extern void pf_ctx_init (pf_ctx_t *ctx, const struct pf_conf_s *conf, 
                struct pf_stat_s *stat);
extern void pf_ctx_reset (pf_ctx_t *ctx);
extern int pf_ctx_socket (pf_ctx_t *ctx);
extern int pf_ctx_connect (pf_ctx_t *ctx);
extern int pf_ctx_close (pf_ctx_t *ctx);

#endif // __included__pf_ctx_h__
