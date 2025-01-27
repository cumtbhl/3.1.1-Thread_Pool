#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "thrd_pool.h"

/*
    当前代码存在一个潜在的死锁问题：
     在thrdpool_waitdone(pool)函数中，主线程会等待所有工作线程（通过pthread_join），
    但在等待期间，如果工作线程完成了所有任务，它们会进入阻塞状态，等待新的任务被添加到队列。
    然而，只有在thrdpool_terminate(pool)中才会调用__nonblock(pool->task_queue);来唤醒这些阻塞的线程。
*/

// 一个简单的任务函数，模拟工作负载
void my_task(void *arg) {
    int num = *(int*)arg;
    printf("Task %d started by thread %lu\n", num, pthread_self());
    sleep(1);  // 模拟任务执行时间
    printf("Task %d completed by thread %lu\n", num, pthread_self());
    free(arg);  // 释放传递给任务的参数
}

int main() {
    // 创建线程池，设定线程数为4
    thrdpool_t *pool = thrdpool_create(4);
    if (pool == NULL) {
        fprintf(stderr, "Failed to create thread pool\n");
        return 1;
    }

    // 提交10个任务到线程池
    for (int i = 0; i < 10; i++) {
        int *task_arg = malloc(sizeof(int));
        if (task_arg == NULL) {
            fprintf(stderr, "Failed to allocate memory for task argument\n");
            continue;
        }
        *task_arg = i + 1;
        if (thrdpool_post(pool, my_task, task_arg) != 0) {
            fprintf(stderr, "Failed to post task %d\n", i + 1);
            free(task_arg);  // 如果任务提交失败，释放内存
        }
    }

    // 等待线程池处理完所有任务
    thrdpool_waitdone(pool);

    // 销毁线程池
    thrdpool_terminate(pool);

    return 0;
}
