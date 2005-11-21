#ifndef __included__pf_dbg_h__
#define __included__pf_dbg_h__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BAIL(fmt,a...) ({ \
                fprintf (stderr, "ERROR: %s +%d\n", __FUNCTION__, __LINE__); \
                if (errno) fprintf (stderr, "ERROR: %s\n", strerror (errno)); \
                fprintf (stderr, "ERROR: " fmt "\n", ##a); \
                exit(-1); \
                })


extern int dbg_level;
#define DBG(lvl,fmt,a...) ({ if (lvl<=dbg_level) printf (fmt,##a); })

#endif // __included__pf_dbg_h__
