#include <zephyr.h>

extern void rust_test_main(void);
extern void rust_sem_thread(void *, void *, void *);

K_THREAD_DEFINE(sem_thread, 1024, rust_sem_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);

void test_main(void)
{
    rust_test_main();
}