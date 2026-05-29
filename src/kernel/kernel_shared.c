#include "kernel_shared.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>


KernelShared *g_kernel = NULL;


static KernelShared *ks_local_create(void) {
    KernelShared *k = calloc(1, sizeof(KernelShared));
    if (!k) return NULL;
    k->total_pages = (int)MEM_TOTAL_PAGES;
    k->free_pages = k->total_pages - (int)MEM_KERNEL_PAGES;
    for (int i = 0; i < (int)MEM_KERNEL_PAGES; i++)
        k->page_bitmap[i / 8] |= (uint8_t)(1U << (i % 8));
    k->current_idx = -1;
    k->next_pid = 1;
    k->initialized = 1;
    pthread_mutex_init(&k->lock, NULL);
    return k;
}


KernelShared *kernel_shared_create(void) {
    KernelShared *k = mmap(NULL, sizeof(KernelShared),
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (k == MAP_FAILED) return NULL;
    memset(k, 0, sizeof(KernelShared));
    k->total_pages = (int)MEM_TOTAL_PAGES;
    k->free_pages = k->total_pages - (int)MEM_KERNEL_PAGES;
    for (int i = 0; i < (int)MEM_KERNEL_PAGES; i++)
        k->page_bitmap[i / 8] |= (uint8_t)(1U << (i % 8));
    k->current_idx = -1;
    k->next_pid = 1;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&k->lock, &attr);
    pthread_mutexattr_destroy(&attr);

    k->initialized = 1;
    g_kernel = k;
    return k;
}

KernelShared *kernel_shared_attach(void) {
    return g_kernel; 
}

void kernel_shared_destroy(KernelShared *k) {
    if (!k) return;
    pthread_mutex_destroy(&k->lock);
    munmap(k, sizeof(KernelShared));
}

int kernel_local_init(void) {
    if (g_kernel) return 0;
    g_kernel = ks_local_create();
    return g_kernel ? 0 : -1;
}

void kernel_local_shutdown(void) {
    if (!g_kernel) return;
    pthread_mutex_destroy(&g_kernel->lock);
    free(g_kernel);
    g_kernel = NULL;
}
