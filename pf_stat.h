#ifndef __included__pf_stat_h__
#define __included__pf_stat_h__

#include <pthread.h>

typedef struct pf_stat_s {
        // use atomic macros for these
        uint                    __no_completed; 
        uint                    __no_failed;

        // lock that protects above variables
        pthread_mutex_t         __lock;
} pf_stat_t;

#define stat_atomic_read(s,n) ({             \
        uint __val;                          \
        pthread_mutex_lock (&(s)->__lock);   \
        __val = (s)->__##n;                  \
        pthread_mutex_unlock (&(s)->__lock); \
        __val;                               \
        })

#define stat_atomic_inc(s,n) ({              \
        pthread_mutex_lock (&(s)->__lock);   \
        (s)->__##n ++;                        \
        pthread_mutex_unlock (&(s)->__lock); \
        })




#endif // __included__pf_stat_h__
