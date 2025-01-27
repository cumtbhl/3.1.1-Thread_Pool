#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include "thrd_pool.h"
#include "spinlock.h"

/*
 * shell: gcc thrd_pool.c -c -fPIC
 * shell: gcc -shared thrd_pool.o -o libthrd_pool.so -I./ -L./ -lpthread
 * usage: include thrd_pool.h & link libthrd_pool.so
 */

// _s 表示 struct 定义的结构体
// _t 表示 typedef 定义的新类型 


typedef struct spinlock spinlock_t;

// 单个 task_t 结构体
typedef struct task_s {
    void *next;
    // 当任务被取出准备执行时，工作线程会调用 func 函数
    handler_pt func;
    void *arg;
} task_t;


// 任务队列 task_queue_t 结构体
typedef struct task_queue_s {
    // head 指向 task_queue_t 中第一个任务
    void *head;
    // tail 指向 task_queue_t 中最后一个任务的 next 成员
    void **tail; 
    // block == 0：非阻塞模式
    // 当 task_queue_t 为空时，消费者线程不会阻塞(挂起)
    // block == 1：阻塞模式
    // 当 task_queue_t 为空时，消费者线程会被阻塞，直到被唤醒为止
    int block;

    // spinlock_t：自旋锁，通过不断"自旋"来尝试持有该锁，不会让线程被挂起
    // mutex_t：互斥锁，当线程尝试持有挂锁失败时，线程会被挂起
    spinlock_t lock;

    // mutex 和 cond 配合使用，进行消费者线程的阻塞与唤醒 
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} task_queue_t;


// 线程池 thrdpool_t 结构体
struct thrdpool_s {
    task_queue_t *task_queue;
    // quit = 0：线程池正常运行，线程持续获取任务
    // quit = 1：线程池关闭，关闭所有线程
    atomic_int quit;
    int thrd_count;
    // 每个 thread 的 ID 都是 pthread_t类型
    // threads 指向所有 thread ID 的首地址
    pthread_t *threads;
};

// 创建 task_queue_t：回滚式编程
static task_queue_t *
__taskqueue_create() {
    int ret;
    task_queue_t *queue = (task_queue_t *)malloc(sizeof(task_queue_t));
    if (queue) {
        ret = pthread_mutex_init(&queue->mutex, NULL);
        if (ret == 0) {
            ret = pthread_cond_init(&queue->cond, NULL);
            if (ret == 0) {
                spinlock_init(&queue->lock);
                queue->head = NULL;
                // queue->tail 指向第一个任务 head( tail 和 head 指向同一个地方)
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

// 生产者线程解除 task_queue_t 的阻塞状态，唤醒消费者线程
// static 使得定义的函数只能在本源文件中使用 
static void
__nonblock(task_queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->block = 0;
    pthread_mutex_unlock(&queue->mutex);
    pthread_cond_broadcast(&queue->cond);
}

// _add_task：1.将原来 queue 中最后一个任务的 next 字段的值修改成新增 task 的 next 字段的首地址
//            2.将原来 queue 中的 tail 修改为 新增task 的 next 的首地址
static inline void 
__add_task(task_queue_t *queue, void *task) {

    // 获取新增 task 的 next 字段的首地址
    void **link = (void**)task;
    // 将新增 task 的 next 字段修改成 NULL
    *link = NULL;
    
    spinlock_lock(&queue->lock);

    // 将原来 queue 中最后一个任务的 next 字段的值修改成新增的 task 的 next 字段的首地址
    *queue->tail = link;

    // 将原来 queue 中的 tail 修改为 新增task 的 next 字段的首地址
    queue->tail = link;

    spinlock_unlock(&queue->lock);
    pthread_cond_signal(&queue->cond);
}

// inline：声明函数为内联函数
// 1.内联函数在函数调用处直接展开代码，减少函数调用开销
static inline void * 
__pop_task(task_queue_t *queue) {
    spinlock_lock(&queue->lock);
    if (queue->head == NULL) {
        spinlock_unlock(&queue->lock);
        return NULL;
    }
    // 获取 queue 的第一个任务 task
    task_t *task;
    task = queue->head;

    // 获取 queue 的第一个任务 task 的 next 字段的首地址
    void **link = (void**)task;

    // 将 queue 的 head 字段的值修改成 queue 的第一个任务 task 的 next 字段的值
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
    // 虚假唤醒：使用 while 循环，确保每次线程被唤醒后都检查任务队列是否为空
    // 如果任务队列依旧为空，则线程继续进入阻塞状态
    while ((task = __pop_task(queue)) == NULL) {
        pthread_mutex_lock(&queue->mutex);
        if (queue->block == 0) {
            pthread_mutex_unlock(&queue->mutex);
            return NULL;
        }
        // 1. 先 unlock(&mtx)
        // 2. 在 cond 休眠
        // --- __add_task 时唤醒
        // 3. 在 cond 唤醒
        // 4. 加上 lock(&mtx);
        pthread_cond_wait(&queue->cond, &queue->mutex);
        pthread_mutex_unlock(&queue->mutex);
    }
    // 如果使用 if 循环，当线程被虚假唤醒后，它直接跳出 if 循环，返回 task = NULL 
    // 而此时不应该让线程返回任何东西，线程应该阻塞
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

// 线程池启动时，多个线程会并行执行 _thrdpool_worker()函数
// 传入到 _thrdpool_worker()函数中的参数 arg 为 &thrdpool_t
static void *
__thrdpool_worker(void *arg) {
    thrdpool_t *pool = (thrdpool_t*) arg;
    task_t *task;
    void *ctx;

    while (atomic_load(&pool->quit) == 0) {
        // 当任务队列没有任务时，线程执行_get_task()函数时会被挂起，一直卡着
        // 1.当返回的 task 非空时，则处理任务
        // 2.当返回的 task 是空时，说明 queue->block 被设置成 0 (说明要终止线程池工作)，跳出循环，结束__thrdpool_worker函数
        task = (task_t*)__get_task(pool->task_queue);
        if (!task) break;
        handler_pt func = task->func;
        ctx = task->arg;
        free(task);
        func(ctx);
    }
    
    return NULL;
}

static void 
__threads_terminate(thrdpool_t * pool) {
    // 让线程池中没有阻塞的线程直接结束 __thrdpool_worker() 函数，活跃线程结束
    atomic_store(&pool->quit, 1);

    // 让线程池中阻塞的线程被唤醒，使得 __get_task() 函数返回 NULL，阻塞线程结束
    __nonblock(pool->task_queue);

    int i;
    for (i=0; i<pool->thrd_count; i++) {
        // 等待线程池中所有线程的 __thrdpool_worker() 函数返回
        // 清理线程池中的所有线程
        pthread_join(pool->threads[i], NULL);
    }
}

static int 
__threads_create(thrdpool_t *pool, size_t thrd_count) {
    // 创建并初始化线程属性变量 attr ————1. 成功 0  2.失败 非 O 值
    pthread_attr_t attr;
	int ret;
    ret = pthread_attr_init(&attr); 

    if (ret == 0) {
        // 为 thrdpool 中的 threads 分配内存
        pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thrd_count);
        if (pool->threads) {
            int i = 0;
            for (; i < thrd_count; i++) {
                // __thrdpool_worker：线程的工作函数，当线程创建成功后，线程执行__thrdpool_worker函数
                // __thrdpool_worker 函数的参数
                if (pthread_create(&pool->threads[i], &attr, __thrdpool_worker, pool) != 0) {
                    break;
                }
            }
            pool->thrd_count = i;
            pthread_attr_destroy(&attr);
            if (i == thrd_count)
                return 0;
            __threads_terminate(pool);
            free(pool->threads);
        }
        ret = -1;
    }
    return ret; 
}

// 这个代码似乎没被用到
void
thrdpool_terminate(thrdpool_t * pool) {
    atomic_store(&pool->quit, 1);
    __nonblock(pool->task_queue);
}

thrdpool_t *
thrdpool_create(int thrd_count) {
    thrdpool_t *pool;

    pool = (thrdpool_t*)malloc(sizeof(*pool));
    if (pool) {
        task_queue_t *queue = __taskqueue_create();
        if (queue) {
            pool->task_queue = queue;
            atomic_init(&pool->quit, 0);
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
    if (atomic_load(&pool->quit) == 1) 
        return -1;
    task_t *task = (task_t*) malloc(sizeof(task_t));
    if (!task) return -1;
    task->func = func;
    task->arg = arg;
    __add_task(pool->task_queue, task);
    return 0;
}

void
thrdpool_waitdone(thrdpool_t *pool) {
    int i;
    for (i=0; i<pool->thrd_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    __taskqueue_destroy(pool->task_queue);
    free(pool->threads);
    free(pool);
}
