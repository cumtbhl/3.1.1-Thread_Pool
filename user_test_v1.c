#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "thrd_pool.h"

void my_task(void *arg) {
    int task_num = *(int *)arg;
    printf("Task %d started by thread %lu\n", task_num, (unsigned long)pthread_self());
    
    // 模拟工作负载
    usleep(100000); // 100ms
    
    printf("Task %d completed by thread %lu\n", task_num, (unsigned long)pthread_self());
    free(arg);  
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
            free(task_arg);  
        }
    }

    // 等待线程池处理完所有任务
    sleep(5);
    printf("\nAll tasks completed\n");

    // 向线程池发出终止信号
    thrdpool_terminate(pool);
    printf("\nTerminating thread pool...\n");

    // 等待线程池中所有线程退出 并 释放线程池资源
    thrdpool_waitdone(pool);
    printf("\npool is destroyed...\n");

    return 0;
}
