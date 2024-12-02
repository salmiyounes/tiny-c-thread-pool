#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "tinythpool.h"

#define err(str) fprintf(stderr, str)

typedef struct bsem {
    pthread_mutex_t mutex; // (read/write) mutex
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

volatile bool thread_on_hold;

// PROTOTYPES

int             jobqueue_init(struct JobQueue *jobqueue_p);
void            jobqueue_push(struct JobQueue *jobqueue_p, struct Job *job_p);
struct Job      *jobqueue_pull(struct JobQueue *jboqueue_p);
void            jobqueue_destroy(struct JobQueue *jobqueue_p);

void            thread_init(struct thpool_ *thpool_p, struct Thread *thread_p, int id);
void           *thread_do(struct thpool_ *thpool_p);
void            thread_destroy(struct Thread *thread_p);
void            thread_hold(int sig_id);
void            thread_release(int sig_id);

void            bsem_init(struct bsem *bsem_p);
void            bsem_destroy(struct bsem *bsem_p);   

void            wait_for_jobs(struct bsem *bsem_p);
void            post_job(struct bsem *bsem_p);
void            post_all(struct bsem *bsem_p);

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

    thread_on_hold                   = false;
    thpool_p->num_of_threads_alive   =     0;
    thpool_p->num_of_threads_working =     0;
    thpool_p->keep_alive             =  true;

    thpool_p->jobqueue = (struct JobQueue *) malloc(sizeof(struct JobQueue));
    if (thpool_p->jobqueue == NULL) {
        err("thpool_init(): Could not allocate memory for the job queue .\n");
        free(thpool_p);
        return NULL;
    }
    if (jobqueue_init(thpool_p->jobqueue) == -1) {
        free(thpool_p);
        return NULL;
    }

    pthread_mutex_init(&thpool_p->thread_count_lock, NULL);
    pthread_cond_init(&thpool_p->threads_all_idle, NULL);

    thpool_p->threads = (struct Thread *) malloc(num_threads * sizeof(struct Thread));
    if (thpool_p->threads == NULL) {
        jobqueue_destroy(thpool_p->jobqueue);
        free(thpool_p);
        return NULL;
    }
    for (int i = 0; i < num_threads; i++) {
        thread_init(thpool_p, &(thpool_p->threads[i]), i);
    }

    while (thpool_p->num_of_threads_alive != num_threads) {};
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
    
    volatile int total = thpool_p->num_of_threads_alive;
    thpool_p->keep_alive = false;

    double TIMEOUT = 1.0;
    double diff;

    time_t start, end;
    time(&start);

    while (diff < TIMEOUT && thpool_p->num_of_threads_alive) {
        post_all(thpool_p->jobqueue->has_jobs);
        time(&end);
        diff = difftime(end, start);
    }

    while (thpool_p->num_of_threads_alive) {
        post_all(thpool_p->jobqueue->has_jobs);
        sleep(1);
    }

    jobqueue_destroy(thpool_p->jobqueue);

    free(thpool_p->threads);
    free(thpool_p);
    return ;
}

int thpool_num_threads_working(struct thpool_ *thpool_p) {
    return thpool_p->num_of_threads_working;
}

void thpool_pause(struct thpool_ *thpool_p) {
    for (int n = 0; n < thpool_p->num_of_threads_alive; n++) {
        pthread_kill(thpool_p->threads[n].pthread, SIGUSR1);
    }
}

void thpool_resume(struct thpool_ *thpool_p) {
    thread_on_hold = false;
}

int jobqueue_init(struct JobQueue *jobqueue_p) {
    jobqueue_p->head =      NULL;
    jobqueue_p->tail =      NULL;
    jobqueue_p->len =          0;
    jobqueue_p->has_jobs = (struct bsem *) malloc(sizeof(struct bsem));
    if (jobqueue_p->has_jobs == NULL) {
        err("jobqueue_init(): Could not allocate memory for bsem .\n");
        return -1;
    }
    bsem_init(jobqueue_p->has_jobs);
    pthread_mutex_init(&jobqueue_p->mutex, NULL);
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

struct Job *jobqueue_pull(struct JobQueue *jboqueue_p) {
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
        Job *job = jobqueue_pull(jobqueue_p);
        if (job != NULL) free(job);
    }
    jobqueue_p->head = NULL;
    jobqueue_p->tail = NULL;
    jobqueue_p->len  =    0;
    bsem_destroy(jobqueue_p->has_jobs);
    free(jobqueue_p->has_jobs);

}

void thread_init(struct thpool_ *thpool_p, struct Thread *thread_p, int id) {
    thread_p->id     =       id;
    thread_p->thpool = thpool_p;

    if (pthread_create(&thread_p->pthread, NULL, (void * (*)(void *))thread_do, (void *)thpool_p) != 0) {
        err("thread_init(): Failed to create thread .\n");
        return;
    };
    pthread_detach(thread_p->pthread);
}

void *thread_do(struct thpool_ *thpool_p) {
    // https://en.wikipedia.org/wiki/Sigaction
    
    struct sigaction act1;
    memset(&act1, 0, sizeof(struct sigaction));
    sigemptyset(&act1.sa_mask);

    act1.sa_handler = thread_hold;
    act1.sa_flags   =  SA_ONSTACK;

    if (sigaction(SIGUSR1, &act1, NULL) == -1) {
        err("thread_do(): cannot handle SIGUSR1");
    }

    pthread_mutex_lock(&thpool_p->thread_count_lock);
    thpool_p->num_of_threads_alive += 1;
    pthread_mutex_unlock(&thpool_p->thread_count_lock);
    
    while (thpool_p->keep_alive) {
        wait_for_jobs(thpool_p->jobqueue->has_jobs);

        if (thpool_p->keep_alive) {
            pthread_mutex_lock(&thpool_p->thread_count_lock);
            thpool_p->num_of_threads_working ++;
            pthread_mutex_unlock(&thpool_p->thread_count_lock);

            Job *job = jobqueue_pull(thpool_p->jobqueue);
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
    
    pthread_mutex_lock(&thpool_p->thread_count_lock);
    thpool_p->num_of_threads_alive --;
    pthread_mutex_unlock(&thpool_p->thread_count_lock);
    return NULL;
}

void thread_destroy(struct Thread *thread_p) {
    if (thread_p != NULL) 
    {
        free(thread_p);
    }
}

void thread_hold(int sig_id) {
    thread_on_hold = true;    
    while (thread_on_hold) {
        sleep(1);
    }
}

void thread_release(int sig_id) {
    thread_on_hold = false;
}

void bsem_init(struct bsem *bsem_p) {
    bsem_p->value = false;
    pthread_mutex_init(&bsem_p->mutex, NULL);
    pthread_cond_init(&bsem_p->cond, NULL);
}

void bsem_destroy(struct bsem *bsem_p) {
    if (bsem_p != NULL) {
        pthread_mutex_destroy(&bsem_p->mutex);
        pthread_cond_destroy(&bsem_p->cond);
        bsem_init(bsem_p);
    }
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

void post_all(struct bsem *bsem_p) {
    pthread_mutex_lock(&bsem_p->mutex);
    bsem_p->value = true;
    pthread_cond_broadcast(&bsem_p->cond);
    pthread_mutex_unlock(&bsem_p->mutex);
}

