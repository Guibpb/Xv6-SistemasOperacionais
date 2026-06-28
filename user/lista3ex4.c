#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NTHREADS 3

typedef struct {
  int limite;  // valor em centavos
  int mutex;   // semáforo binário para proteger o limite
} CreditCard;

typedef struct {
  CreditCard *cartao;
  int *requisicoes;
  int quantidade_requisicoes;
  int id;
  uint seed;
} CardPayment;

/*
 * Equivale ao construtor CreditCard(double limite).
 */
int
creditCardInit(CreditCard *cartao, int limite_inicial)
{
  cartao->limite = limite_inicial;
  cartao->mutex = sem_init(1);

  if (cartao->mutex < 0) {
    return -1;
  }

  return 0;
}

/*
 * Equivale ao withdraw(valor).
 *
 * Retorna:
 * 1 = pagamento aprovado
 * 0 = pagamento recusado
 */
int
withdraw(CreditCard *cartao, int valor, int id_thread)
{
  sem_wait(cartao->mutex);

  /*
   * Esta comparação e o desconto acontecem juntos,
   * protegidos pelo semáforo.
   *
   * Assim, duas threads não conseguem gastar o mesmo
   * dinheiro ao mesmo tempo.
   */
  if (valor <= cartao->limite) {
    cartao->limite -= valor;

    printf("Thread %d: pagamento de %d centavos APROVADO. Limite restante: %d\n",
           id_thread, valor, cartao->limite);

    sem_post(cartao->mutex);
    return 1;
  }

  printf("Thread %d: pagamento de %d centavos RECUSADO. Limite atual: %d\n",
         id_thread, valor, cartao->limite);

  sem_post(cartao->mutex);
  return 0;
}

/*
 * Equivale ao getBalance().
 */
int
getBalance(CreditCard *cartao)
{
  int saldo;

  sem_wait(cartao->mutex);

  saldo = cartao->limite;

  sem_post(cartao->mutex);

  return saldo;
}

void
creditCardDestroy(CreditCard *cartao)
{
  sem_destroy(cartao->mutex);
}

/*
 * Gerador simples de número pseudoaleatório.
 */
uint
aleatorio(uint *seed)
{
  *seed = (*seed * 1103515245) + 12345;
  return *seed;
}

/*
 * Equivale ao run() da classe CardPayment em Java.
 *
 * Cada thread recebe um array de pagamentos e tenta
 * fazer todos eles no mesmo cartão.
 */
void
cardPayment(void *arg)
{
  CardPayment *pagamento = (CardPayment *)arg;

  for (int i = 0; i < pagamento->quantidade_requisicoes; i++) {
    int tempo;

    /*
     * Espera um tempo aleatório antes de cada pagamento.
     */
    tempo = 10 + (aleatorio(&pagamento->seed) % 50);
    pause(tempo);

    withdraw(pagamento->cartao,
             pagamento->requisicoes[i],
             pagamento->id);
  }

  exit(0);
}

int
main(int argc, char *argv[])
{
  CreditCard cartao;

  int pids[NTHREADS];
  char *raw_stacks[NTHREADS];
  char *stacks[NTHREADS];
  CardPayment pagamentos[NTHREADS];

  /*
   * Cada array representa as compras feitas por uma thread.
   * Valores em centavos.
   */
  int requisicoes1[] = {2500, 1800, 3000};
  int requisicoes2[] = {4000, 1500, 2000};
  int requisicoes3[] = {1000, 3500, 2200};

  /*
   * Limite inicial: 10.000 centavos = R$ 100,00.
   */
  if (creditCardInit(&cartao, 10000) < 0) {
    printf("Erro ao criar semaforo do cartao\n");
    exit(1);
  }

  pagamentos[0].cartao = &cartao;
  pagamentos[0].requisicoes = requisicoes1;
  pagamentos[0].quantidade_requisicoes = 3;
  pagamentos[0].id = 1;
  pagamentos[0].seed = 100;

  pagamentos[1].cartao = &cartao;
  pagamentos[1].requisicoes = requisicoes2;
  pagamentos[1].quantidade_requisicoes = 3;
  pagamentos[1].id = 2;
  pagamentos[1].seed = 200;

  pagamentos[2].cartao = &cartao;
  pagamentos[2].requisicoes = requisicoes3;
  pagamentos[2].quantidade_requisicoes = 3;
  pagamentos[2].id = 3;
  pagamentos[2].seed = 300;

  /*
   * Prepara uma stack para cada thread criada com clone().
   */
  for (int i = 0; i < NTHREADS; i++) {
    uint64 addr;

    raw_stacks[i] = malloc(PGSIZE * 2);

    if (raw_stacks[i] == 0) {
      printf("malloc falhou\n");
      exit(1);
    }

    addr = (uint64)raw_stacks[i];

    if (addr % PGSIZE != 0) {
      addr += PGSIZE - (addr % PGSIZE);
    }

    stacks[i] = (char *)addr;
  }

  /*
   * Cria as threads.
   */
  for (int i = 0; i < NTHREADS; i++) {
    pids[i] = clone(cardPayment, &pagamentos[i], stacks[i]);

    if (pids[i] < 0) {
      printf("clone falhou\n");
      exit(1);
    }
  }

  /*
   * Espera todas terminarem.
   */
  for (int i = 0; i < NTHREADS; i++) {
    if (join(pids[i]) < 0) {
      printf("join falhou\n");
      exit(1);
    }

    free(raw_stacks[i]);
  }

  printf("\nLimite final: %d centavos\n", getBalance(&cartao));

  creditCardDestroy(&cartao);

  exit(0);
}