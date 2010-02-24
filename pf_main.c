#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/wait.h>
//#include <asm/bitops.h>

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
        uint total_connections;
        uint no_threads;

        // thread config and status
        const pf_conf_t        *conf;
        pf_stat_t              *stat;

        // when we started
        struct timeval          start_time;
} pf_main_info_t;

static void* thread_helper (void*);

static void pf_display (pf_main_info_t *minfo);

// ------------------------------------------------------------------------

static void show_help(void)
{
	printf ("pf [-h] [-t <threads>] [-a <agents>] "
		"[-c <connections>] [-d <what>=<delay>] "
		"<url>\n"
		"\n"
		"Options:\n"
		"  -h              print this help\n"
		"  -t <num>        threads to run\n"
		"  -a <num>        agents per thread\n"
		"  -c <num>        total connections\n"
		"  -d start:<num>  delay for # sec after connect\n"
		"  -d close:<num>  delay for # seconds before close\n"
		"\n"
		"Url format:\n"
		"  [http://]<host>:[<port>[/<path>]]\n");
}

#define HTTP_PREFIX "http://"

static void parse_url_arg (const char *optarg, pf_conf_t *conf)
{
	char *buf = strdup(optarg);
	char *p, *q;
	char *addr, *port = "80", *path = NULL;
	ulong tmp;
	int rc;

	p = buf;
	if (!strncasecmp(p, HTTP_PREFIX, strlen(HTTP_PREFIX)))
		p += strlen(HTTP_PREFIX);

	addr = p;

	q = index(p, ':');
	if (q) {
		*q = 0;
		port = p = q+1;
	}

	q = index(p, '/');
	if (q) {
		path = strdup(q);
		*q = 0;
	}

	// set server info
	conf->server.sin_family = PF_INET;

	rc = inet_pton (conf->server.sin_family, addr,
			&conf->server.sin_addr);
	if (rc<0) 
		BAIL ("inet_pton %s", addr);

	tmp = strtoul (port, NULL, 0);
	if (tmp == ULONG_MAX) 
		BAIL ("strtoul %s", port);
	conf->server.sin_port = htons(tmp);

	conf->path = path;

	free(buf);
}

static void parse_delay_arg (const char *optarg,  pf_conf_t *conf)
{
	struct {
		const char *name;
		uint *value;
	} table[] = {
		{ "start",	&conf->start_delay_sec },
		{ "close",	&conf->close_delay_sec },
		{ NULL,		NULL }
	}, *p;

	for (p=table; p->name; p++) {
		uint nlen = strlen (p->name);
		if (strncmp (p->name, optarg, nlen)
				|| optarg[nlen] != '=')
			continue;

		*(p->value) = atoi(optarg+nlen+1);
		return;
	}

	fprintf (stderr, "Delay format: -d <what>=<delay>\n"
			"Valid for <what>: ");
	for (p=table; p->name; p++)
		fprintf (stderr, "%s ", p->name);
	fprintf (stderr, "\n");
	exit(EXIT_FAILURE);
}

int
main (int argc, char *argv[])
{
        int rc = 0;
        pf_conf_t conf;
        pf_stat_t stat;
        pf_main_info_t minfo;
        pthread_t *threads;
        uint t;
	int opt;

        memset (&conf, 0, sizeof (conf));
        memset (&stat, 0, sizeof (stat));
        memset (&minfo, 0, sizeof (minfo));

        // read configuration from command line
        minfo.no_threads = 10;
        conf.no_agents = 10;	// per thread
        minfo.total_connections = 100000;

	while ((opt = getopt (argc, argv, "t:a:c:d:h")) != -1) {
		switch (opt) {
		case 'h':
			show_help();
			return 0;
		case 't':
			minfo.no_threads = atoi(optarg);
			break;
		case 'a':
			conf.no_agents = atoi(optarg);
			break;
		case 'c':
			minfo.total_connections = atoi(optarg);
			break;
		case 'd':
			parse_delay_arg (optarg, &conf);
			break;
		default:
			show_help();
			exit(EXIT_FAILURE);
		}
	}

	if ((argc - optind) != 1)
		BAIL ("need to provide <url>");
	if (conf.no_agents < 1)
		BAIL ("need at least one agent per thread");
	if (minfo.no_threads < 1)
		BAIL ("need at least one thread");
	if (minfo.total_connections < 1)
		BAIL ("need at least one connection");

	parse_url_arg (argv[optind], &conf);

	printf ("connect to %s\n"
		"%9u threads\n"
		"%9u agents per thread\n"
		"%9u total connections\n"
		"%9u sec delay before a start\n"
		"%9u sec delay before a close\n",
		argv[optind],
		minfo.no_threads,
		conf.no_agents,
		minfo.total_connections,
		conf.start_delay_sec,
		conf.close_delay_sec);

        // configure main info structure
        minfo.conf = &conf;
        minfo.stat = &stat;
        gettimeofday (&minfo.start_time, NULL);
        pthread_mutex_init (&stat.__lock, NULL);

	// set handlers
	conf.do_init = http_init;
	conf.do_connected = http_connected;
	conf.do_recv = http_recv;
	conf.do_send = http_send;
	conf.do_closing = http_closing;

        // set number of connections
	conf.no_connections = minfo.total_connections / minfo.no_threads;
	conf.kill_switch = 0;

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

	// force kill
	conf.kill_switch = 1;

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





