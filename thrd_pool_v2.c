#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "thrd_pool_v2.h"
#include "spinlock.h"

typedef struct spinlock spinlock_t;

typedef struct task_s {
    void *next;
    handler_pt func;
    void *arg;
} task_t;

typedef struct task_queue_s {
    void *head;
    void **tail;
    int block;
    spinlock_t lock;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} task_queue_t;

struct thrdpool_s {
    task_queue_t *task_queue;
    atomic_int quit;
    atomic_int pending_tasks;  // 新增任务计数器
    int thrd_count;
    pthread_t *threads;
};

static task_queue_t *
__taskqueue_create() {
    int ret;
    task_queue_t *queue = (task_queue_t *)malloc(sizeof(task_queue_t));
    if(queue) {
        ret = pthread_mutex_init(&queue->mutex, NULL);
        if(ret == 0) {
            ret = pthread_cond_init(&queue->cond, NULL);
            if(ret == 0) {
                spinlock_init(&queue->lock);
                queue->head = NULL;
                queue->tail = &queue->head;
                queue->block = 1;
                return queue;
            }
            pthread_mutex_destroy(&queue->mutex);
        }
        free(queue);
    }
    return NULL;
}

static void
__nonblock(task_queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->block = 0;
    pthread_mutex_unlock(&queue->mutex);
    pthread_cond_broadcast(&queue->cond);
}

static inline void
__add_task(task_queue_t *queue, void *task) {
    void **link = (void**)task;
    *link = NULL;

    spinlock_lock(&queue->lock);
    *queue->tail = link;
    queue->tail = link;
    spinlock_unlock(&queue->lock);

    pthread_cond_signal(&queue->cond);
}

static inline void *
__pop_task(task_queue_t *queue) {
    spinlock_lock(&queue->lock);
    if (queue->head == NULL) {
        spinlock_unlock(&queue->lock);
        return NULL;
    }

    task_t *task = queue->head;
    void **link = (void**)task;
    queue->head = *link;

    if (queue->head == NULL) {
        queue->tail = &queue->head;
    }
    spinlock_unlock(&queue->lock);
    return task;
}

static inline void *
__get_task(task_queue_t *queue) {
    task_t *task;
    while ((task = __pop_task(queue)) == NULL) {
        pthread_mutex_lock(&queue->mutex);

        if (queue->block == 0) {
            pthread_mutex_unlock(&queue->mutex);
            return NULL;
        }

        pthread_cond_wait(&queue->cond, &queue->mutex);
        pthread_mutex_unlock(&queue->mutex);
    }
    return task;
}

static void
__taskqueue_destroy(task_queue_t *queue) {
    task_t *task;
    while ((task = __pop_task(queue))) {
        free(task);
    }
    spinlock_destroy(&queue->lock);
    pthread_cond_destroy(&queue->cond);
    pthread_mutex_destroy(&queue->mutex);
    free(queue);
}

static void * 
__thrdpool_worker(void *arg) {
    thrdpool_t *pool = arg;
    while (!atomic_load(&pool->quit)) {
        task_t *task = __get_task(pool->task_queue);
        if (!task) break;
        handler_pt func = task->func;
        void *ctx = task->arg;
        free(task);
        func(ctx);
        atomic_fetch_sub(&pool->pending_tasks, 1);  // 减少计数
    }
    return NULL;
}

static void
__threads_terminate(thrdpool_t *pool) {
    atomic_store(&pool->quit, 1);
    __nonblock(pool->task_queue);

    int i;
    for (i = 0; i < pool->thrd_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
}

static int
__threads_create(thrdpool_t *pool, size_t thrd_count) {
    pthread_attr_t attr;
    int ret;
    ret = pthread_attr_init(&attr);

    if (ret == 0) {
        pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thrd_count);
        if (pool->threads) {
            int i = 0;
            for(; i < thrd_count; i++) {
                if (pthread_create(&pool->threads[i], &attr, __thrdpool_worker, pool) != 0) {
                    break;
                }
            }
            pool->thrd_count = i;
            pthread_attr_destroy(&attr);
            if ( i == thrd_count)
                return 0;
            __threads_terminate(pool);
            free(pool->threads);
        }
        ret = -1;
    }
    return ret;
}

thrdpool_t *
thrdpool_create(int thrd_count) {
    thrdpool_t *pool;
    pool = (thrdpool_t*)malloc(sizeof(thrdpool_t));
    if (pool) {
        task_queue_t *queue = __taskqueue_create();
        if (queue) {
            pool->task_queue = queue;
            atomic_init(&pool->quit, 0);
            atomic_init(&pool->pending_tasks, 0);
            if (__threads_create(pool, thrd_count) == 0)
                return pool;
            __taskqueue_destroy(queue);
        }
        free(pool);
    }
    return NULL;
}

int 
thrdpool_post(thrdpool_t *pool, handler_pt func, void *arg) {
    if (atomic_load(&pool->quit)) return -1;
    task_t *task = malloc(sizeof(task_t));
    if (!task) return -1;
    task->func = func;
    task->arg = arg;
    atomic_fetch_add(&pool->pending_tasks, 1);  // 增加计数
    __add_task(pool->task_queue, task);
    return 0;
}

void 
thrdpool_waitall(thrdpool_t *pool) {
    while (atomic_load(&pool->pending_tasks) > 0) {
        usleep(1000);  // 可优化为条件变量等待
    }
}

void 
thrdpool_terminate(thrdpool_t *pool) {
    atomic_store(&pool->quit, 1);
    __nonblock(pool->task_queue);
}

void 
thrdpool_waitdone(thrdpool_t *pool) {
    for (int i = 0; i < pool->thrd_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
}

void 
thrdpool_destroy(thrdpool_t *pool) {
    __taskqueue_destroy(pool->task_queue);
    free(pool->threads);
    free(pool);
}
