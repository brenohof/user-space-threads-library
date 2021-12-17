#include <stdio.h>
#include <stdlib.h>
#include "fiber.h"

void* threadFunction()
{
    printf("Rotina da thread %p\n", fiber_self());
    printf("Olá mundo! Vou somar dois valores e retorno:D\n");
    
    void * a = malloc(sizeof(int));
    printf("primeiro valor: ");
    scanf("%d", (int*) a);

    printf("Adeus :C\n");
    
    fiber_exit(&a);
}

int main(int argc, char const *argv[])
{
    fiber_t fid1 = NULL;
    if(fiber_create(&fid1, &threadFunction, NULL) == -1) 
        perror("cannot create a fiber\n");
    
    printf("Criou a fiber 1 = %p\n", fid1);

    void * p = malloc(sizeof(int));
    fiber_join(fid1, p);

    printf("%d", *(int *) p);

    return 0;
}
