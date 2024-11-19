#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include "tinythpool.h"

#define err(str) fprintf(stderr, str)

typedef struct bsem {
    pthread_mutex_t mutex;
    pthread_cond_t   cond;
    bool            value;
} bsem;

typedef struct Job  {
    struct Job *next;
    void (* function)(void *);
    void                 *arg;
} Job;

typedef struct JobQueue {
    struct Job      *head;
    struct Job      *tail;
    size_t            len;
    bsem        *has_jobs;
    pthread_mutex_t mutex;
} JobQueue;

typedef struct Thread {
    int                 id;
    pthread_t      pthread;
    struct thpool_ *thpool;
} Thread;

typedef struct thpool_ {
    struct Thread              *threads;
    struct JobQueue           *jobqueue;
    volatile int num_of_threads_working;
    volatile int   num_of_threads_alive;
    volatile bool            keep_alive;
    pthread_mutex_t   thread_count_lock;
    pthread_cond_t     threads_all_idle;
} thpool_;

// PROTOTYPES

int             jobqueue_init(struct JobQueue *jobqueue_p);
void            jobqueue_push(struct JobQueue *jobqueue_p, struct Job *job_p);
Job            *jobqueue_peek(struct JobQueue *jboqueue_p);
void            jobqueue_destroy(struct JobQueue *jobqueue_p);

void            thread_init(struct thpool_ *thpool_p, struct Thread *thread_p, int id);
void           *thread_do(struct thpool_ *thpool_p);
void            thread_destroy(struct Thread *thread_p);

void            wait_for_jobs(struct bsem *bsem_p);
void            post_job(struct bsem *bsen_p);


struct thpool_ *thpool_init (int num_threads) {
    if (num_threads < 0) {
        num_threads = 0;
    }

    thpool_ *thpool_p = NULL;
    thpool_p = (struct thpool_ *) malloc(sizeof(struct thpool_));
    if (thpool_p == NULL) {
        err("thpool_init(): Could not allocate memory for the thread pool .\n");
        return NULL;
    }

    thpool_p->num_of_threads_alive   =    0;
    thpool_p->num_of_threads_working =    0;
    thpool_p->keep_alive             = true;

    thpool_p->jobqueue = (struct JobQueue *) malloc(sizeof(struct JobQueue));
    if (jobqueue_init(thpool_p->jobqueue) == -1) {
        free(thpool_p);
        return NULL;
    }

    pthread_mutex_init(&thpool_p->thread_count_lock, NULL);
    pthread_cond_init(&thpool_p->threads_all_idle, NULL);

    thpool_p->threads = (struct Thread *) malloc(num_threads * sizeof(struct Thread));
    int i = 0;
    for (; i < num_threads; i++) {
        thread_init(thpool_p, &thpool_p->threads[i], i);
    }

    return thpool_p;
}

int thpool_add_work(struct thpool_ *thpool_p, void (*function)(void *), void *arg) {
    struct Job *job = NULL;
    job = (struct Job *) malloc(sizeof(struct Job));
    if (job == NULL) {
        err("thpool_add_work(): Could not allocate memory for new job .\n");
        return -1;
    }

    job->function = function;
    job->arg      =      arg;
    job->next     =     NULL;

    jobqueue_push(thpool_p->jobqueue, job);
    return 0;
}

void thpool_wait(struct thpool_ *thpool_p) {
    pthread_mutex_lock(&thpool_p->thread_count_lock);
    while (thpool_p->num_of_threads_working || thpool_p->jobqueue->len) {
        pthread_cond_wait(&thpool_p->threads_all_idle, &thpool_p->thread_count_lock);
    }
    pthread_mutex_unlock(&thpool_p->thread_count_lock);
}

void thpool_destroy(struct thpool_ *thpool_p) {
    if (thpool_p == NULL) {
        return;
    }
    int total = thpool_p->num_of_threads_alive;
    for (int i = 0; i < total; i++) {
        thread_destroy(&thpool_p->threads[i]);
    }

    jobqueue_destroy(thpool_p->jobqueue);

    free(thpool_p->threads);
    free(thpool_p);
    return ;
}

int jobqueue_init(struct JobQueue *jobqueue_p) {
    jobqueue_p->head =      NULL;
    jobqueue_p->tail =      NULL;
    jobqueue_p->has_jobs = (struct bsem *) malloc(sizeof(struct bsem));
    jobqueue_p->has_jobs->value = false;
    jobqueue_p->len =          0;

    pthread_mutex_init(&jobqueue_p->mutex, NULL);
    pthread_mutex_init(&jobqueue_p->has_jobs->mutex, NULL);
    pthread_cond_init(&jobqueue_p->has_jobs->cond, NULL);

    return 0;
}

void jobqueue_push(struct JobQueue *jobqueue_p, struct Job *job_p) {
    pthread_mutex_lock(&jobqueue_p->mutex);
    switch (jobqueue_p->len) {
        case 0:
            jobqueue_p->head = job_p;
            jobqueue_p->tail = job_p;
            break;
        default:
            jobqueue_p->tail->next = job_p;
            jobqueue_p->tail       = job_p;
    }

    jobqueue_p->len ++;
    post_job(jobqueue_p->has_jobs);
    pthread_mutex_unlock(&jobqueue_p->mutex);
}

struct Job *jobqueue_peek(struct JobQueue *jboqueue_p) {
    pthread_mutex_lock(&jboqueue_p->mutex);
    Job *job = jboqueue_p->head;
    switch (jboqueue_p->len) {
        case 0:
            break;
        case 1:
            jboqueue_p->head = NULL;
            jboqueue_p->tail = NULL;
            jboqueue_p->len  =    0;
            break;
        default:
            jboqueue_p->head = job->next;
            jboqueue_p->len --;
            post_job(jboqueue_p->has_jobs);
    }

    pthread_mutex_unlock(&jboqueue_p->mutex);
    return job;
}

void jobqueue_destroy(struct JobQueue *jobqueue_p) {
    while (jobqueue_p->len) {
        free(jobqueue_peek(jobqueue_p));
    }
    free(jobqueue_p->has_jobs);
}

void thread_init(struct thpool_ *thpool_p, struct Thread *thread_p, int id) {
    thread_p->id     =       id;
    thread_p->thpool = thpool_p;

    pthread_create(&thread_p->pthread, NULL, (void * (*)(void *))thread_do, (void *)thpool_p);
    pthread_detach(thread_p->pthread);
}

void *thread_do(struct thpool_ *thpool_p) {
    while (thpool_p->keep_alive) {
        wait_for_jobs(thpool_p->jobqueue->has_jobs);

        if (thpool_p->keep_alive) {
            pthread_mutex_lock(&thpool_p->thread_count_lock);
            thpool_p->num_of_threads_working ++;
            pthread_mutex_unlock(&thpool_p->thread_count_lock);

            Job *job = jobqueue_peek(thpool_p->jobqueue);
            if (job) {
                void (* function)(void *);
                void *arg;
                function = job->function;
                arg      = job->arg;

                function(arg);
                free(job);
            }

            pthread_mutex_lock(&thpool_p->thread_count_lock);
            thpool_p->num_of_threads_working--;
            if (!thpool_p->num_of_threads_working) {
                pthread_cond_signal(&thpool_p->threads_all_idle);
            }
            pthread_mutex_unlock(&thpool_p->thread_count_lock);
        }
    }
    return NULL;
}

void thread_destroy(struct Thread *thread_p) {
    free(thread_p);
}

void wait_for_jobs(struct bsem *bsem_p) {
    pthread_mutex_lock(&bsem_p->mutex);
    while (!bsem_p->value) {
        pthread_cond_wait(&bsem_p->cond, &bsem_p->mutex);
    }

    bsem_p->value = false;
    pthread_mutex_unlock(&bsem_p->mutex);
}

void post_job(struct bsem *bsem_p) {
    pthread_mutex_lock(&bsem_p->mutex);
    bsem_p->value = true;
    pthread_cond_signal(&bsem_p->cond);
    pthread_mutex_unlock(&bsem_p->mutex);
}