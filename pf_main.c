#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/time.h>
//#include <signal.h>

#include <asm/bitops.h>

#include "pf_dbg.h"
#include "pf_http.h"
#include "pf_ctx.h"
#include "pf_conf.h"
#include "pf_run.h"

// global debug verbosity level
int dbg_level = 0;

// ------------------------------------------------------------------------

int
main (void)
{
        int rc;
        pf_conf_t conf;
        const char *srv_addr = "192.168.10.7";
        const char *srv_port = "80";
        uint no_agents = 100;
        uint no_connections = 1000;
        ulong tmp;

        memset (&conf, 0, sizeof (conf));

        // set server info
        conf.server.sin_family = PF_INET;

        rc = inet_pton (conf.server.sin_family, srv_addr, 
                        &conf.server.sin_addr);
        if (rc<0) 
                BAIL ("inet_pton %s", srv_addr);

        tmp = strtoul (srv_port, NULL, 0);
        if (tmp == ULONG_MAX) 
                BAIL ("strtoul %s", srv_port);
        conf.server.sin_port = htons(tmp);

        // set handlers
        conf.do_connected = http_connected;
        conf.do_recv = http_recv;
        conf.do_send = http_send;

        // set number of connections
        conf.no_agents = no_agents;
        conf.no_connections = no_connections;

        // run the test
        rc = pf_run (&conf);

        return rc;
}





