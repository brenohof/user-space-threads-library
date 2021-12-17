#include <stdio.h>
#include "fiber.h"

void* rotina1()
{
    printf("Rotina da thread %p\n", fiber_self());
    printf("Olá mundo! Só vou esperar um pouco :D\n");
    
    int i = 0;
    while (++i < 1000000000);

    printf("Ovo finalizar :C\n");
    
    fiber_exit(NULL);
}

void* rotina2()
{
    printf("Rotina da thread %p\n", fiber_self());
    printf("Olá mundo! Eu vou somar 2 valores :D\n");
    
    int a, b;
    printf("primeiro valor: ");
    scanf("%d", &a);

    printf("segundo valor: ");
    scanf("%d", &b);

    printf("Soma = %d - Adeus :C\n", a + b);
    
    fiber_exit(NULL);
}

void* rotina3()
{
    printf("Rotina da thread %p\n", fiber_self());
    printf("Olá mundo! vou printar 1 valor várias vezes :D\n");
    
    int a = 10;

    for (int i  = 0; i < 10; ++i)
        printf("Valor = %d \n", a);

    printf("3 - Acabou minha função :C, Chorastes?\n");
    
    fiber_exit(NULL);
}

int main(int argc, char const *argv[])
{
    int N = 3;
    void * arr[] = {&rotina1, &rotina2, &rotina3};
        fiber_t fid = NULL;
    
    for (int i = 0;  i < N; ++i) {
        
        if(fiber_create(&fid, arr[i], NULL) == -1) 
            perror("cannot create a fiber\n");
    
        printf("Criou a fiber %d = %p\n", i, fid);

        fiber_join(fid, NULL);
    }

    

    return 0;
}
