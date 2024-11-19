#ifndef TINYTHPOOL_H
#define TINYTHPOOL_H

typedef struct thpool_ * thpool;

/**
 *  @brief  Initialize threadpool
 * 
 *  This function will return a pointer to the thread pool. 
 * 
 *  @example
 *      
 *      ``
 *      thpool thpool_p;               // first we declare a thread pool pointer
 *      thpool_p = thpool_init(4);      // then we initialize it to 4 threads
 *      ``
 *  
 *  @param num_threads    number of threads to be created in the threadpool
 *  @return thread_pool_p return a pointer to the thread pool on success, NULL on error
*/  

thpool thpool_init(int num_threads);

/**
 *  @brief Add work to the job queue
 *  
 *  Takes an action and its argument and adds it to the threadpool's job queue.
 *  if you want to pass and action with multiple arguments then you need to pass
 *  pointer to a structure, for more info check: exemple.c .
 * 
 *  NOTICE: You have to cast both the function and its argument to not get warnings.
 * 
 *  @param thread_pool_p   A pointer to thread pool to wish the work will be added
 *  @param function_p      A pointer to the function to add as work
 *  @param arg_p           A pointer to the function argument
 *  @return 0 in success, -1 otherwise
*/

int            thpool_add_work(thpool thpool_p, void (*function)(void *), void *arg);

/** 
 *  @brief Wait for all queue jobs to finish
 *  
 *  Takes a pointer to the thread pool and then waits for all jobs in the queue
 *  and the running to finish, once the queue is empty and and all work has completed  
 *  the main program will continue .
 * 
 *  @param thread_pool_p pointer to the thread pool
 *  @return nothing
*/

void            thpool_wait(thpool thpool_p);

void            thpool_destroy(thpool thpool_p);

#endif