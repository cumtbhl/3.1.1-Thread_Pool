#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "thrd_pool_v2.h"


void example_task(void *arg) {
    int task_num = *(int *)arg;
    printf("Task %d started by thread %lu\n", task_num, (unsigned long)pthread_self());
    
    // 模拟工作负载
    usleep(100000); // 100ms
    
    printf("Task %d completed by thread %lu\n", task_num, (unsigned long)pthread_self());
    free(arg);  
}

int main() {
    // 创建线程池（4个工作线程）
    thrdpool_t *pool = thrdpool_create(4);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        return EXIT_FAILURE;
    }

    // 提交20个任务
    for (int i = 0; i < 20; i++) {
        int *arg = malloc(sizeof(int));
        *arg = i;
        if (thrdpool_post(pool, example_task, arg) != 0) {
            fprintf(stderr, "Failed to post task %d\n", i);
            free(arg);
        }
    }

    // 等待所有任务完成
    thrdpool_waitall(pool);
    printf("\nAll tasks completed\n");
    
    // 向线程池发出终止信号
    thrdpool_terminate(pool);
    printf("\nTerminating thread pool...\n");

    // 等待线程池中所有线程退出
    thrdpool_waitdone(pool);
    printf("\nALL threads exit...\n");

    // 销毁线程池资源
    thrdpool_destroy(pool);
    printf("\npool is destroyed...\n");

    return EXIT_SUCCESS;
}