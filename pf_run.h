
#ifndef __included__pf_run_h__
#define __included__pf_run_h__

struct pf_conf_s;
struct pf_stat_s;

extern int pf_run (const pf_conf_t *conf, pf_stat_t *stat);

#endif // __included__pf_run_h__
