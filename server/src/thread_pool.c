#include "thread_pool.h"
#include "handler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int thread_pool_init(thread_pool_t *pool, int n)
{
    if (!pool) return -1;
    memset(pool, 0, sizeof(*pool));
    if (n <= 0) n = 4;
    if (n > 8)  n = 8;
    pool->num_threads = n;
    pool->shutdown    = false;

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);

    for (int i = 0; i < n; i++) {
        int rc = pthread_create(&pool->threads[i], NULL,
                                worker_func, (void *)pool);
        if (rc != 0) {
            fprintf(stderr, "thread_pool: pthread_create %d failed (err=%d)\n", i, rc);
            pthread_mutex_lock(&pool->mutex);
            pool->shutdown = true;
            pthread_cond_broadcast(&pool->cond);
            pthread_mutex_unlock(&pool->mutex);
            /* Join threads that were created successfully */
            for (int j = 0; j < i; j++)
                pthread_join(pool->threads[j], NULL);

            pthread_mutex_destroy(&pool->mutex);
            pthread_cond_destroy(&pool->cond);
            pool->num_threads = 0;
            return -1;
        }
    }
    return 0;
}

void thread_pool_submit(thread_pool_t *pool, task_t *t)
{
    if (!pool || !t) return;
    pthread_mutex_lock(&pool->mutex);
    if (pool->shutdown || pool->count >= MAX_QUEUE) {
        pthread_mutex_unlock(&pool->mutex);
        return;
    }
    pool->queue[pool->tail] = *t;
    pool->tail = (pool->tail + 1) % MAX_QUEUE;
    pool->count++;
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

void thread_pool_destroy(thread_pool_t *pool)
{
    if (!pool) return;
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->num_threads; i++)
        pthread_join(pool->threads[i], NULL);

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    pool->num_threads = 0;
}
