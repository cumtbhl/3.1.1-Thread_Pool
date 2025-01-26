// 防止某个源文件中重复包含该头文件
#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

typedef struct thrdpool_s thrdpool_t;

// 定义一个函数指针类型 handler_pt
// 它指向一个函数，该函数接收一个 void* 类型的参数，返回 void
typedef void (*handler_pt)(void *);

#ifdef __cplusplus

extern "C"
{
// 对称处理
thrdpool_t *thrdpool_create(int thrd_count);

void thrdpool_terminate(thrdpool_t * pool);

int thrdpool_post(thrdpool_t *pool, handler_pt func, void *arg);

void thrdpool_waitdone(thrdpool_t *pool);
}

#endif

#endif