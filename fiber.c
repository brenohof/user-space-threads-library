#include <stdio.h>
#include <ucontext.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>

typedef void *fiber_t; // tipo para ID de fiber

#define FIBER_STACK_SIZE 1024 * 64

#define TIME_SLICE_SEC 0
#define TIME_SLICE_USEC 20000

#define STATE_READY 0
#define STATE_BLOCKED 1
#define STATE_FINISHED 2

/**
 * @struct Waiting
 * 
 * @brief  Estrutura que armazena as fibers em espera do join de outra fiber.
 * 
 * @param id        identificador da fiber que está sendo aguardada .
 * @param next      ponteiro para a próxima fiber na lista de espera.
*/
typedef struct Waiting
{
    fiber_t id;           // identificador da fiber.
    struct Waiting *next; // Ponteiro para o próximo nodo
} Waiting;

/**
 * @struct Fiber
 * 
 * @brief  Estrutura de uma fiber (thread no espaço do usuário). Implementa a lista
 * circular para o algoritmo de preempção round robin.
 * 
 * @param next      ponteiro para outra estrutura na lista.
 * @param context   ponteiro para o contexto de execução da fiber.
 * @param status    estado atual da fiber; STATE_READY a fiber está pronta para ser
 * executada; STATE_BLOCKED a fiber está em espera; STATE_FINISHED fiber finalizada
 * @param retval    ponteiro que armazena o endereço do valor de retorno.
 * @param join_rval ponteiro que armazena o endereço do valor  de retorno  da fiber
 * que está sendo aguardada.
 * @param waitList  lista de fibers que estão aguardando essa fiber.
*/
typedef struct Fiber
{
    struct Fiber *next;      // próxima fiber da lista
    ucontext_t context;      // contexto da fiber
    int status;              // status da fiber
    void *retval;            // valor de retorno da fiber
    void *join_rval;         // valor de retorno da fiber que ela estava esperando
    struct Fiber *joinFiber; // ponteiro para a fiber que essa fiber está esperando
    Waiting *waitList;       // lista de head que estão esperando essa fiber
} Fiber;

/**
 * @struct Fiber_List
 * 
 * @brief  Estrutura que armazena as  fibers. Representa uma lista  circular para o
 * algoritmo de preempção round robin.
 * 
 * @param head      ponteiro para a primeira fiber da lista; para a cabeça.
 * @param tail      ponteiro para a última fiber da lista; para a cauda.
 * @param running   ponteiro para a fiber em execução.
 * @param size      quantidade de elementos inseridos na lista.
*/
typedef struct Fiber_List
{
    Fiber *head;    // primeira fiber(cabeça) da lista.
    Fiber *tail;    // última fiber(cauda) da lista.
    Fiber *running; // fiber sendo executada no momento
    int size;       // tamanho da lista
} Fiber_List;

// Lista de fibers
Fiber_List *fiber_list = NULL;

// Contexto do escalonamento e da thread principal
ucontext_t scheduler_ctx, parent_ctx;

// Timer do escalonador
struct itimerval timer;

/**
 * @name   preempt()
 * 
 * @brief  Handler do sinal SIGVTALRM lançado  pelo timer  quando expirado. Salva o
 * contexto da fiber atual e troca para o contexto do escalonador.
 * 
 * @return identificador da fiber.
*/
void preempt()
{
    /**
     * swapcontext(ucontext_t *oucp, const ucontext_t *ucp);
     * 
     * Salva o contexto atual na variável  apontada por oucp; Atribui  contexto de 
     * execução  que é  apontado  pela  variável ucp.  Em outras palavras, troca o 
     * contexto atual (oucp) pelo contexto em ucp.
    */
    if (swapcontext(&fiber_list->running->context, &scheduler_ctx) == -1)
    {
        perror("swapcontext failed at preempt.");
        return;
    }
}

/**
 * @name   fiber_self()
 * 
 * @brief  retorna o identificador da fiber em execução.
 * 
 * @return identificador da fiber.
*/
fiber_t fiber_self()
{
    return fiber_list->running;
}

/**
 * @name   stop_timer()
 * 
 * @brief  Para o timer.
*/
void stop_timer()
{
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;

    /**
     * setitimer(int which, struct itimerval * new, struct itimerval * old);
     * 
     * Atribui o valor do interval timer;
     * 
     * ITIMER_VIRTUAL especifica que deve  decrementar o timer com base no tempo de
     * execução do processo. Sua expiração gera um sinal do tipo SIGVTALRM.
    */

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) == -1)
    {
        perror("Ocorreu um erro no setitimer() da stop_timer");
        return;
    }
}

/**
 * @name   start_timer()
 * 
 * @brief  Inicia o timer com os valores de time slice.
*/
void start_timer()
{
    /**
     * it_interval  é uma estrutura  que representa  o valor que  será  colocado no
     * it_value após a expiração;
     * tv_sec são os segundos até a expiração do timer;
     * tv_usec são os microsegundos até a expiração do timer;
    */

    timer.it_value.tv_sec = TIME_SLICE_SEC;
    timer.it_value.tv_usec = TIME_SLICE_USEC;

    timer.it_interval.tv_sec = TIME_SLICE_SEC;
    timer.it_interval.tv_usec = TIME_SLICE_USEC;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) == -1)
    {
        perror("Ocorreu um erro no setitimer() da stop_timer");
        return;
    }
}

/**
 * @name   release_fibers(Waiting *waitingList)
 * 
 * @brief  Libera todas as fibers da lista de espera para que sejam executadas.
 * 
 * @param waitingList - lista de espera das fibers.
*/
void release_fibers(Waiting *waitingList)
{
    // Enquanto houver fiber esperando
    while (waitingList != NULL)
    {
        // Recebe o próximo nodo da lista
        Waiting *waitingNode = waitingList->next;
        // Procura a fiber com o id do nodo atual da waitingList
        Fiber *waitingFiber = waitingList->id;
        // Se a fiber existir e estiver esperando
        if (waitingFiber != NULL && waitingFiber->status == STATE_BLOCKED)
        {
            // Libera a fiber
            waitingFiber->status = STATE_READY;
            // Guarda o retval
            waitingFiber->join_rval = waitingFiber->joinFiber->retval;
        }
        // Libera o nodo no topo
        free(waitingList);
        // Vai para o próximo nodo
        waitingList = waitingNode;
    }
}

/**
 * @name   pop(Fiber *fiber)
 * 
 * @brief  Libera a memória da fiber e remove ela da lista.
 * 
 * @param fiber - fiber que será desalocada.
 * 
 * @return próxima fiber na lista.
*/
Fiber *pop(Fiber *fiber)
{

    if (fiber == NULL)
        return NULL;

    if (fiber_list->size == 0)
        return NULL;

    if (fiber->status != STATE_FINISHED)
        return NULL;

    Fiber *prev_fiber = fiber_list->head;
    Fiber *next_fiber = fiber->next;

    while (fiber != prev_fiber->next)
    {
        prev_fiber = prev_fiber->next;
    }

    prev_fiber->next = fiber->next;

    if (fiber == fiber_list->head)
        fiber_list->head = fiber->next;

    if (fiber == fiber_list->tail)
        fiber_list->tail = prev_fiber;

    free(fiber->context.uc_stack.ss_sp);
    free(fiber);
    fiber = NULL;

    fiber_list->size--;

    if (fiber_list->size == 0)
    {
        prev_fiber = NULL;
        next_fiber = NULL;
    }

    return next_fiber;
}

/**
 * @name   scheduler()
 * 
 * @brief  Função responsável por fazer a preempção das fibers, para implementar  o
 * algoritmo round robin  a  estrutura de lista e fiber  tem o comportamento de uma 
 * lista circular. Essa função verifica  o estado da fiber e  age de acordo. Caso o
 * estado da fiber  seja STATE_READY, seu  contexto será setado como  o contexto de 
 * execução. Quando a lista estiver vazia suas estruturas serão desalocadas.
*/
void scheduler()
{
    stop_timer();

    Fiber *nextFiber = fiber_list->running->next;

    // Enquanto não encontrar uma fiber pronta para ser executada
    while (nextFiber->status != STATE_READY)
    {
        // Caso a fiber já tenha terminado
        if (nextFiber->status == STATE_FINISHED)
        {
            // Liberando as head esperando esta(caso existam)
            release_fibers(nextFiber->waitList);
            // Destruindo essa fiber e obtendo a próxima(caso exista)
            nextFiber = pop(nextFiber);
            // Se a pop retornar NULL
            if (nextFiber == NULL)
            {
                // Caso não haja mais nenhuma fiber na lista
                if (fiber_list->size == 0)
                {
                    free(fiber_list);                   // Liberando a lista de head
                    free(scheduler_ctx.uc_stack.ss_sp); // Liberando a pilha do escalonador
                    exit(0);                            // Terminando o programa
                }
                // Caso contrário, algum erro ocorreu
                exit(-1);
            }
        }
        // Caso a thread atual esteja num join
        if (nextFiber->status == STATE_BLOCKED)
        {
            // Caso a thread que ela está esperando não estiver encerrada
            if (nextFiber->joinFiber->status != STATE_FINISHED)
                nextFiber = nextFiber->next; // Pula a thread que está esperando
            // Caso a thread que ela está esperando tenha terminado
            else
                nextFiber->status = STATE_READY; // Definir o status como STATE_READY
        }
    }

    // Definindo a próxima fiber selecionada como a fiber atual
    fiber_list->running = nextFiber;

    // Redefinindo o timer para o tempo normal
    timer.it_value.tv_sec = TIME_SLICE_SEC;
    timer.it_value.tv_usec = TIME_SLICE_USEC;

    start_timer();

    // Definindo o contexto atual como o da próxima fiber
    if (setcontext(&nextFiber->context) == -1)
    {
        perror("setcontext failed at scheduler");
        return;
    }
}

/**
 * @name   init_fiber_list()
 * 
 * @brief  Inicialzia a lista de fibers. Insere a estrutura da  thread principal na
 * lista. Inicializa o contexto do escalonador.
 * 
*/
int init_fiber_list()
{
    fiber_list = malloc(sizeof(Fiber_List)); // alocação da lista de fibers.

    if (fiber_list == NULL)
    {
        perror("list malloc failed at init_fiber_list.");
        return -1;
    }

    Fiber *parentFiber = malloc(sizeof(Fiber));
    if (parentFiber == NULL)
    {
        perror("malloc failed at init_fiber_list.");
        return -1;
    }

    parentFiber->context = parent_ctx;
    parentFiber->next = parentFiber;
    parentFiber->status = STATE_READY;

    fiber_list->head = parentFiber;
    fiber_list->tail = parentFiber;
    fiber_list->size = 1;
    fiber_list->running = parentFiber;

    if (getcontext(&scheduler_ctx) == -1)
    {
        perror("getcontext failed at init_fiber_list.");
        return -1;
    }

    scheduler_ctx.uc_link = &parent_ctx;
    scheduler_ctx.uc_stack.ss_sp = malloc(FIBER_STACK_SIZE);
    scheduler_ctx.uc_stack.ss_size = FIBER_STACK_SIZE;
    scheduler_ctx.uc_stack.ss_flags = 0;
    if (scheduler_ctx.uc_stack.ss_sp == NULL)
    {
        perror("stack malloc failed at init_fiber_list.");
        return -1;
    }

    makecontext(&scheduler_ctx, scheduler, 0);

    return 0;
}

/**
 * @name   push(Fiber *fiber)
 * 
 * @brief  Insere a fiber no final da lista de fibers. Caso a lista seja nula chama
 * a função init_fiber_list().
 * 
 * @param  fiber ponteiro para fiber que será inserida.
*/
void push(Fiber *fiber)
{
    if (fiber_list == NULL)
        init_fiber_list();

    fiber->next = fiber_list->head;
    fiber_list->tail->next = fiber;
    fiber_list->tail = fiber;
    fiber_list->size++;
}

/**
 * @name   init_fiber_attr(Fiber *new_node)
 * 
 * @brief  Inicializa as variáveis da fiber.
 * 
 * @param  new_node ponteiro para fiber para inicializiar seus valores.
*/
void init_fiber_attr(Fiber *new_node)
{
    new_node->next = NULL;
    new_node->status = STATE_READY;
    new_node->retval = NULL;
    new_node->join_rval = NULL;
    new_node->joinFiber = NULL;
    new_node->waitList = NULL;
}

/**
 * @name   fiber_create(fiber_t *fiber, void *(*start_routine)(void *), void *arg)
 * 
 * @brief  Cria uma fiber (thread no user-space).
 * 
 * @param  fiber identificador que será retornado por referência.
 * @param  start_routine rotina que será executada.
 * @param  arg argumento que será passados para a rotina.
 * 
 * @return 0 para sucesso; -1 para falha.
*/
int fiber_create(fiber_t *fiber, void *(*start_routine)(void *), void *arg)
{
    stop_timer();

    ucontext_t context;

    Fiber *new_node;

    if (fiber == NULL)
        return -1;

    new_node = (Fiber *)malloc(sizeof(Fiber));

    if (new_node == NULL)
    {
        perror("malloc failed at fiber_create.");
        return -1;
    }

    /*
     * A função getcontext(ucontext_t *ucp) inicializa a estrutura apontada por ucp 
     * para o contexto atual da thread que fez a chamada. O tipo  ucontext_t para o 
     * qual ucp aponta define o  contexto do  usuário e inclui o  conteúdo do atual 
     * contexto de  execução como  registradores,  a  máscara de sinal e a pilha de 
     * execução atual. 
    */

    if (getcontext(&context) == -1)
    {
        perror("getcontext failed at fiber_create");
        return -1;
    }

    /*
     * ucontext_t * uc_link ponteiro para o contexto que será retomado  quando este
     * contexto retornar;
     * O cabeçalho <signal.h> define o tipo stack_t como uma  estrutura que  inclui 
     * pelo menos os seguintes membros:
    */

    context.uc_link = &scheduler_ctx;
    context.uc_stack.ss_sp = malloc(FIBER_STACK_SIZE);
    context.uc_stack.ss_size = FIBER_STACK_SIZE;
    context.uc_stack.ss_flags = 0;

    if (context.uc_stack.ss_sp == 0)
    {
        perror("stack malloc failed at fiber_create.");
        return -1;
    }

    /*
     * A função makecontext(ucontext_t *ucp, (void *func)(), int argc, ..) modifica
     * o contexto especificado por ucp,  que foi  inicializado usando getcontext().
     * Quando  este  contexto é  retomado  usando  swapcontext()  ou  setcontext(), 
     * a execução  do  programa  continua chamando func, passando os argumentos que
     * seguem argc na chamada da makecontext().
    */

    makecontext(&context, (void (*)(void))start_routine, 1, arg);

    new_node->context = context;
    init_fiber_attr(new_node);

    push(new_node);

    *fiber = new_node;

    start_timer();

    return 0;
}

/**
 * @name   fiber_join(fiber_t fiber, void **retval)
 * 
 * @brief  Coloca a fiber  atual em espera  para execução até o fim de outra fiber.
 * 
 * @param  retval endereço para onde será colocado o valor de retorno  da  fiber.
 * Caso seja nulo será ignorado.
 * 
 * @return 0 para sucesso; -1 para falha.
*/
int fiber_join(fiber_t fiber, void **retval)
{

    int i = 0;

    Fiber *fiber_node = NULL;
    for (fiber_node = fiber_list->head; fiber != fiber_node && ++i <= fiber_list->size; fiber_node = fiber_node->next);

    if (i > fiber_list->size)
        return -1;

    // Se a fiber a ser esperada é a que está executando
    if (fiber_node == fiber_list->running)
        return -1;

    // Se a fiber que deveria terminar antes já terminou
    if (fiber_node->status == STATE_FINISHED)
    {
        release_fibers(fiber_node->waitList);
        return 0;
    }

    Waiting *waitingNode = malloc(sizeof(Waiting));

    if (waitingNode == NULL)
    {
        perror("malloc failed at fiber_join.");
        return -1;
    }

    // Atribuindo o id do nodo e inicializando seu ponteiro next+-
    waitingNode->id = fiber;
    waitingNode->next = NULL;

    // Parar o timer, área crítica
    stop_timer(NULL);

    // Adicionando um nodo na lista de espera da fiber a ser aguardada
    if (fiber_node->waitList == NULL)
    {
        fiber_node->waitList = (Waiting *)waitingNode;
    }
    else
    {
        Waiting *waitingTop = (Waiting *)fiber_node->waitList;
        fiber_node->waitList = (Waiting *)waitingNode;
        fiber_node->waitList->next = (Waiting *)waitingTop;
    }

    // Definindo a fiber que a fiber atual está esperando
    fiber_list->running->joinFiber = (Fiber *)fiber_node;

    // Marcando a fiber atual como esperando
    fiber_list->running->status = STATE_BLOCKED;

    // Trocando para o contexto do escalonador
    if (swapcontext(&fiber_list->running->context, &scheduler_ctx) == -1)
    {
        perror("swapcontext failed at fiber_join.");
        return -1;
    }

    // Recuperando o valor de retorno da fiber que estava sendo aguardada.
    // Caso NULL tenha sido passado como argumento para retval, nada mais é feito.
    // Caso a fiber que estava sendo aguardada por esta tenha sido destruída,
    // as rotinas de destruição já distribuíram os valores de retval corretamente
    // para os atributos join_rval das head que estavam aguardando-a.
    if (retval != NULL)
    {
        // Caso a joinFiber não tenha sido destruída ainda, o retval é recuperado diretamente dela
        if (fiber_list->running->join_rval == NULL && fiber_list->running->joinFiber != NULL)
            *retval = fiber_list->running->joinFiber->retval;
        // Caso contrário, o retval é recuperado do atributo join_rval da própria fiber que chamou
        // fiber_join()
        else
            *retval = fiber_list->running->join_rval;

        // Resetando os retvals da fiber
        fiber_list->running->retval = NULL;
        fiber_list->running->join_rval = NULL;
    }

    // Definindo o status da fiber atual como pronta para executar
    fiber_list->running->status = STATE_READY;

    return 0;
}

/**
 * @name   fiber_destory(fiber_t fiber)
 * 
 * @brief  Desaloca a fiber.
 * 
 * @param  fiber - identificador da fiber que deve ser desalocada.
 * 
 * @return 0 para sucesso; -1 para falha.
*/
int fiber_destroy(fiber_t fiber)
{
    int i = 0;
    Fiber *fiber_node = NULL;
    for (fiber_node = fiber_list->head; fiber != fiber_node && ++i <= fiber_list->size; fiber_node = fiber_node->next);

    if (i > fiber_list->size)
        return -1;
    
    if (pop(fiber_node) == NULL)
        return -1;
    
    return 0;
}

/**
 * @name   fiber_exit(void *retval;
 * 
 * @brief  Troca o status da fiber atual para STATE_FINISHED e  atribui o endereço.
 * para o valor de  retorno. Quando identificada pelo  scheduler  será desalocada e
 * nunca mais será executada.
*/
void fiber_exit(void *retval)
{
    fiber_list->running->retval = retval;
    fiber_list->running->status = STATE_FINISHED;
    preempt();
}

/**
 * @name   init_preempt()
 * 
 * @brief  Inicializa  as estruturas do  timer e sinalizador, quando o timer  tiver
 * expirado envia um sinal do tipo SIGVTALRM. A função preemp lida com o sinal.
 * 
*/
void init_preempt()
{
    /**
     * sigaction(int signum, struct sigaction act, struct sigaction oldact);
     * 
     * Examina e troca a ação tomada por um  processo  quando ocorrer um  sinal que
     * tenha o valor do signum, no caso SIGVTALRM. act é ação  que deve ser  tomada
     * quando o sinal ocorrer. oldact é a ação antiga.
    */

    struct sigaction new_s;
    new_s.sa_handler = &preempt;
    new_s.sa_flags = 0;

    if (sigaction(SIGVTALRM, &new_s, NULL) == -1)
    {
        perror("Ocorreu um erro no sigaction da init_preempt");
        return;
    }
}

/**
 * @brief É executada quando a biblioteca é carregada.
*/
__attribute__((constructor)) void init()
{
    init_fiber_list();
    init_preempt();
}