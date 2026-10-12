/* Keep the parent's host malloc arena busy while guest fork refreshes RPC fds. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
static atomic_int stopping;
static pthread_t workers[6];
static void *blocks[6];
static void *allocate(void *arg) {
    void **block=arg;
    while (!atomic_load(&stopping)) {
        *block=realloc(*block,8*1024*1024);if(!*block)abort();
        memset(*block,42,8*1024*1024);
        *block=realloc(*block,4*1024*1024);if(!*block)abort();
    }
    return NULL;
}
void fork_stress_start(void) {
    /* Allocate in the calling thread's arena, also used by the RPC bitmap.
     * Reallocating these blocks from workers exercises that inherited lock. */
    mallopt(M_MMAP_THRESHOLD,32*1024*1024);
    atomic_store(&stopping,0);
    for(int i=0;i<6;i++) {
        blocks[i]=malloc(4*1024*1024);if(!blocks[i])abort();
        if(pthread_create(&workers[i],NULL,allocate,&blocks[i]))abort();
    }
}
void fork_stress_stop(void) {
    atomic_store(&stopping,1);
    for(int i=0;i<6;i++){pthread_join(workers[i],NULL);free(blocks[i]);}
}
void fork_stress_child(void) {
    void *p=malloc(1024*1024);if(!p)_Exit(2);
    p=realloc(p,2*1024*1024);if(!p)_Exit(3);free(p);
}
