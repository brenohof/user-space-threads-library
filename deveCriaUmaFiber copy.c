#include <stdio.h>
#include "fiber.h"

void* threadFunction()
{
    printf("Rotina da thread %p\n", fiber_self());
    printf("Olá mundo! :D\n");
    
    // Loop só para aumentar a duração da rotina
    int i = 0;
    while (++i < 1000000000);

    printf("Adeus :C\n");
    
    fiber_exit(NULL);
}

int main(int argc, char const *argv[])
{
    fiber_t fid1 = NULL;
    if(fiber_create(&fid1, &threadFunction, NULL) == -1) 
        perror("cannot create a fiber\n");
    
    printf("Criou a fiber 1 = %p\n", fid1);

    fiber_join(fid1, NULL);

    return 0;
}
