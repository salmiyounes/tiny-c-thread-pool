# tiny-c-thread-pool

This is a minimal threadpool implementation. 

## Run the exemple 

```sh
$ gcc tinythpool.c exemple.c -o exemple -pthread 
$ ./exemple
```

## Basic usage

```c
#include "tinythpool.h"

int main() {
    thpool threadpool = thpool_init(4); // Creating a thread pool with 4 threads

    thpool_add_work(threadpool, (void *) function_p, (void *)arg_p); // Add a new work to the thread pool

    thpool_wait(threadpool); // Wait for all jobs to finish

    thpool_destroy(threadpool); // Destroy the thread pool

    return 0;
}
```

## Contribution

Contributions are welcome! Feel free to fork this repository, make your changes, and submit a pull request.

