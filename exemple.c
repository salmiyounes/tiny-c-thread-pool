#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include "tinythpool.h"

typedef struct Data {
    int     *arr;
    int   target;
    int      len;
} Data;

void *task1(void *arg) {
    fprintf(stdout, "Thread %lu working on %d\n", pthread_self(), (int)arg);
    return NULL; 
}

void *task2(void *arg) {
    struct Data *mydata = (struct Data *) arg;

    int right = mydata->len - 1, left = 0;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (mydata->arr[mid] > mydata->target) {
            right = mid - 1;
        } else if (mydata->arr[mid] < mydata->target) {
            left = mid + 1;
        } else {
            fprintf(stdout, "The right index is %d .\n", mid);
            free(mydata);
            return NULL;
        }
    }

    fprintf(stdout, "Could not found the right index .\n");
    free(mydata);
    return NULL;
} 

int main() {

    fprintf(stdout, "Making a thread pool with 4 threads\n");
    thpool threadpool = thpool_init(2);

    fprintf(stdout, "Adding 40 task to the thread pool\n");
    for (int i = 0; i < 40; i++) {
        thpool_add_work(threadpool, (void *) task1, (void *)(uintptr_t)i);
    }

    thpool_wait(threadpool);

    fprintf(stdout, "Adding the second job to the thread pool\n");
    int arr[] = {1 , 2, 3, 4, 5, 6, 7, 8};
    struct Data *mydata = (struct Data *)malloc(sizeof(struct Data));
    *mydata = (struct Data){
        .arr = arr,
        .target = 4,
        .len = 7
    };
    thpool_add_work(threadpool, (void *) task2, (void *)mydata);

    thpool_wait(threadpool);

    fprintf(stdout, "Killing the thread pool\n");
    thpool_destroy(threadpool);
    
    return 0;
}