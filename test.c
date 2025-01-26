#include "thrd_pool.h"
#include <stdio.h>
#include <unistd.h>

void my_task(void *arg) {
    int num = *(int*)arg;
    printf("Task %d is running.\n", num);
    sleep(1);
}

int main() {
    thrdpool_t *pool = thrdpool_create(4); // 创建4个线程
    int data[] = {1, 2, 3, 4, 5};

    for (int i = 0; i < 5; i++) {
        thrdpool_post(pool, my_task, &data[i]);
    }

    sleep(3);
    thrdpool_terminate(pool);
    thrdpool_waitdone(pool);
    printf("All tasks done.\n");

    return 0;
}
