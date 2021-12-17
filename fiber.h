typedef void * fiber_t;

int fiber_create(fiber_t *fiber, void *(*start_routine) (void *), void *arg);

int fiber_join(fiber_t fiber, void **retval);

int fiber_destroy(fiber_t fiber);

fiber_t fiber_self();

void fiber_exit(void *retval);
