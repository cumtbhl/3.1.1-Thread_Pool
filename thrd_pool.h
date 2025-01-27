#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

typedef struct thrdpool_s thrdpool_t;
typedef void (*handler_pt)(void *);

#ifdef __cplusplus
extern "C"
{
#endif

    thrdpool_t *thrdpool_create(int thrd_count);
    int thrdpool_post(thrdpool_t *pool, handler_pt func, void *arg);
    void thrdpool_waitdone(thrdpool_t *pool);
    void thrdpool_terminate(thrdpool_t *pool);

#ifdef __cplusplus
}
#endif

#endif