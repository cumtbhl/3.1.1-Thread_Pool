// 防止某个源文件中重复包含该头文件
#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

typedef struct thrdpool_s thrdpool_t;

// 定义一个函数指针类型 handler_pt
// 它指向一个函数，该函数接收一个 void* 类型的参数，返回 void
typedef void (*handler_pt)(void *);

// 如果当前编译环境是 C++ , 则编译 extern "C" {...} 内容
// extern "C" {...} 告诉编译器 {}里的函数使用 C 语言的链接方式
#ifdef __cplusplus
extern "C"
{
#endif

// 暴露4个 API 给用户：  1.创建线程池 2.向线程池提交任务 3.等待线程池处理所有任务 4.销毁线程池
// 实现高内聚、低耦合的设计
thrdpool_t *thrdpool_create(int thrd_count);

void thrdpool_terminate(thrdpool_t *pool);

int thrdpool_post(thrdpool_t *pool, handler_pt func, void *arg);

void thrdpool_waitdone(thrdpool_t *pool);

#ifdef __cplusplus
}
#endif

#endif