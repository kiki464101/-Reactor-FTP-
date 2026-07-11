#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include <pthread.h>
#include <stdbool.h>

#define MAX_QUEUE 128

/* Task types (matches protocol command numbers where convenient) */
enum {
    TASK_LOGIN = 0,
    TASK_LS    = 1,
    TASK_GET   = 2,
    TASK_PUT   = 3,
    TASK_BYE     = 4,
    TASK_LISTDIR = 5,
};

typedef struct {
    int    type;           /* TASK_LOGIN / LS / GET / PUT / BYE */
    int    fd;             /* client socket fd */
    void  *session;        /* opaque client_session_t* pointer */
    unsigned char *payload;     /* malloc'd command payload (worker frees) */
    int    payload_len;
} task_t;

typedef struct {
    task_t queue[MAX_QUEUE];
    int head, tail, count;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    pthread_t threads[8];
    int num_threads;
    bool shutdown;
} thread_pool_t;

int thread_pool_init(thread_pool_t *pool, int n);
void thread_pool_submit(thread_pool_t *pool, task_t *t);
void thread_pool_destroy(thread_pool_t *pool);

/* worker entry — defined in handler.c */
void *worker_func(void *arg);
#endif
