# tiny-c-thread-pool

This is a minimal threadpool implementation. 

## Run the exemple 

```sh
gcc tinythpool.h tinythpool.c exemple.c -o exemple && ./exemple
```

## Basic usage

```c
#include "tinythpool.h"

int main() {
    thpool threadpool = thpool_init(4);

    thpool_add_work(threadpool, (void *) function_p, (void *)arg_p);

    thpool_wait(threadpool);

    thpool_destroy(threadpool);

    return 0;
}
```

## Contribution

Contributions are welcome! Feel free to fork this repository, make your changes, and submit a pull request.

