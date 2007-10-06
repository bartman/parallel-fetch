#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <asm/bitops.h>

#include "pf_dbg.h"
#include "pf_http.h"
#include "pf_ctx.h"
#include "pf_conf.h"
#include "pf_stat.h"
#include "pf_run.h"

// global debug verbosity level
int dbg_level = 0;

// ------------------------------------------------------------------------

typedef struct pf_main_info_s {

        // application configuration
        const char *srv_addr;
        const char *srv_port;
        uint total_connections;
        uint no_threads;
        uint no_agents;
        uint no_connections;

        // thread config and status
        const pf_conf_t        *conf;
        pf_stat_t              *stat;

        // when we started
        struct timeval          start_time;
} pf_main_info_t;

static void* thread_helper (void*);

static void pf_display (pf_main_info_t *minfo);

// ------------------------------------------------------------------------

int
main (void)
{
        int rc;
        pf_conf_t conf;
        pf_stat_t stat;
        pf_main_info_t minfo;
        pthread_t *threads;
        ulong tmp;
        uint t;

        memset (&conf, 0, sizeof (conf));
        memset (&stat, 0, sizeof (stat));
        memset (&minfo, 0, sizeof (minfo));

        // should read this from command line
        minfo.srv_addr = "192.168.1.15";
        minfo.srv_port = "80";
        minfo.no_threads = 10;
        minfo.no_agents = 10;
        minfo.total_connections = 1000;
        minfo.no_connections = minfo.total_connections / minfo.no_threads;

        // configure main info structure
        minfo.conf = &conf;
        minfo.stat = &stat;
        gettimeofday (&minfo.start_time, NULL);
        pthread_mutex_init (&stat.__lock, NULL);

        // set server info
        conf.server.sin_family = PF_INET;

        rc = inet_pton (conf.server.sin_family, minfo.srv_addr, 
                        &conf.server.sin_addr);
        if (rc<0) 
                BAIL ("inet_pton %s", minfo.srv_addr);

        tmp = strtoul (minfo.srv_port, NULL, 0);
        if (tmp == ULONG_MAX) 
                BAIL ("strtoul %s", minfo.srv_port);
        conf.server.sin_port = htons(tmp);

        // set handlers
        conf.do_connected = http_connected;
        conf.do_recv = http_recv;
        conf.do_send = http_send;
        conf.do_closing = http_closing;

        // set number of connections
        conf.no_agents = minfo.no_agents;
        conf.no_connections = minfo.no_connections;

        // threading
        threads = calloc (minfo.no_threads, sizeof (pthread_t));
        if (!threads) BAIL ("calloc (%d, pthread_t)", minfo.no_threads);

        for (t=0; t<minfo.no_threads; t++) {

                rc = pthread_create (&threads[t], NULL, thread_helper, &minfo);
                if (rc<0) BAIL ("pthread_create %d", t);

                printf ("started thread %u\n", t);
        }

        while (stat_atomic_read (minfo.stat,no_completed) < minfo.total_connections) {
                sleep (1);
                pf_display (&minfo);
        }
        printf ("\n");

        for (t=0; t<minfo.no_threads; t++) {

                void *ret;
                int rc;

                pthread_join (threads[t], &ret);

                rc = (int)(long)ret;

                printf ("stopped thread %u\n", t);
        }

        return rc;
}

// ------------------------------------------------------------------------

static void* 
thread_helper (void *arg)
{
        int rc;
        pf_main_info_t *minfo = arg;

        rc = pf_run (minfo->conf, minfo->stat);

        return (void*)(long)rc;
}

// ------------------------------------------------------------------------

static void 
pf_display (pf_main_info_t *minfo)
{
        //const pf_conf_t *conf = minfo->conf;
        pf_stat_t       *stat = minfo->stat;
        struct timeval now, diff;
        double us, conn_per_sec;
        uint no_completed, no_failed;

        no_completed = stat_atomic_read (stat, no_completed);
        no_failed = stat_atomic_read (stat, no_failed);

        gettimeofday (&now, NULL);

        timersub (&now, &minfo->start_time, &diff);

        us = diff.tv_sec + diff.tv_sec/1000000.0;

        conn_per_sec = no_completed / us;

        fprintf (stdout, "completed %u/%u  %f conn/sec  "
                        "(fail %u)           \r", 
                        no_completed, minfo->total_connections, 
                        conn_per_sec, no_failed);
        fflush (stdout);
}





