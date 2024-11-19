#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include "tinythpool.h"


void *task1(void *arg) {
    fprintf(stdout, "Thread %lu working on %d\n", pthread_self(), (int)arg);
    return NULL; 
}

int task2(int num) {
    return 0;
} 

int main() {
    fprintf(stdout, "Making a thread pool with 4 threads\n");
    thpool threadpool = thpool_init(4);
    
    fprintf(stdout, "Adding 40 task to the thread pool\n");
    for (int i = 0; i < 40; i++) {
        thpool_add_work(threadpool, (void *) task1, (void *)(uintptr_t)i);
    }
    
    thpool_wait(threadpool);
    fprintf(stdout, "Killing the thread pool\n");
    thpool_destroy(threadpool);
    
    return 0;
}