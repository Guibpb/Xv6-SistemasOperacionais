#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define MAX_CLIENTES 20

#define VAZIO   0
#define HOMEM   1
#define MULHER  2

typedef struct {
  int genero;  // 0 vazio, 1 homem, 2 mulher
  int atual;   // quantas pessoas estão dentro

  int esperando_homens;
  int esperando_mulheres;

  int mutex;          // protege genero, atual e contadores
  int fila_homens;    // homens bloqueados
  int fila_mulheres;  // mulheres bloqueadas
  int sem_tela;       // evita mistura nos printf
} Toalete;

typedef struct {
  int genero;
  int id;
  uint seed;
  Toalete *t;
} Tarefa;

uint
aleatorio(uint *seed)
{
  *seed = (*seed * 1103515245) + 12345;
  return *seed;
}

int toalete_init(Toalete *t) {
  t->genero = VAZIO;
  t->atual = 0;
  t->esperando_homens = 0;
  t->esperando_mulheres = 0;

  t->mutex = sem_init(1);
  t->fila_homens = sem_init(0);
  t->fila_mulheres = sem_init(0);
  t->sem_tela = sem_init(1);

  if (t->mutex < 0 || t->fila_homens < 0 || t->fila_mulheres < 0 || t->sem_tela < 0) {
    return -1;
  }

  return 0;
}

void toalete_destroy(Toalete *t) {
  sem_destroy(t->mutex);
  sem_destroy(t->fila_homens);
  sem_destroy(t->fila_mulheres);
  sem_destroy(t->sem_tela);
}

void mulherQuerEntrar(Toalete *t, int id) {
  sem_wait(t->sem_tela);
  printf("Mulher %d quer entrar\n", id);
  sem_post(t->sem_tela);

  sem_wait(t->mutex);

  while (t->genero == HOMEM) {
    t->esperando_mulheres++;

    sem_post(t->mutex);

    sem_wait(t->sem_tela);
    printf("Mulher %d: Ops! Tem homem\n", id);
    sem_post(t->sem_tela);

    // Equivale ao wait() do Java.
    sem_wait(t->fila_mulheres);

    // Ao acordar, precisa pegar novamente o mutex.
    sem_wait(t->mutex);

    t->esperando_mulheres--;
  }

  t->atual++;
  t->genero = MULHER;

  sem_post(t->mutex);

  sem_wait(t->sem_tela);
  printf("Mulher %d entrou\n", id);
  sem_post(t->sem_tela);
}

void homemQuerEntrar(Toalete *t, int id) {
  sem_wait(t->sem_tela);
  printf("Homem %d quer entrar\n", id);
  sem_post(t->sem_tela);

  sem_wait(t->mutex);

  while (t->genero == MULHER) {
    t->esperando_homens++;

    sem_post(t->mutex);

    sem_wait(t->sem_tela);
    printf("Homem %d: Ops! Tem mulher\n", id);
    sem_post(t->sem_tela);

    // Equivale ao wait() do Java.
    sem_wait(t->fila_homens);

    sem_wait(t->mutex);

    t->esperando_homens--;
  }

  t->atual++;
  t->genero = HOMEM;

  sem_post(t->mutex);

  sem_wait(t->sem_tela);
  printf("Homem %d entrou\n", id);
  sem_post(t->sem_tela);
}

void pessoaSai(Toalete *t, int id) {
  int genero_que_saiu;
  int ficou_vazio = 0;
  int quantidade_acordar = 0;
  int fila_acordar = -1;

  sem_wait(t->mutex);

  genero_que_saiu = t->genero;
  t->atual--;

  if (t->atual == 0) {
    t->genero = VAZIO;
    ficou_vazio = 1;

    if (genero_que_saiu == HOMEM) {
      quantidade_acordar = t->esperando_mulheres;
      fila_acordar = t->fila_mulheres;
    } else {
      quantidade_acordar = t->esperando_homens;
      fila_acordar = t->fila_homens;
    }
  }

  sem_post(t->mutex);

  sem_wait(t->sem_tela);

  if (genero_que_saiu == HOMEM) {
    printf("Homem %d saiu\n", id);
  } else {
    printf("Mulher %d saiu\n", id);
  }

  if (ficou_vazio) {
    printf("Nao tem mais ninguem!\n");
  }

  sem_post(t->sem_tela);

  /*
   * Equivale ao notifyAll() do Java.
   * Dá um "sinal" para cada pessoa do outro gênero
   * que estava bloqueada.
   */
  for (int i = 0; i < quantidade_acordar; i++) {
    sem_post(fila_acordar);
  }
}

void cliente(void *arg) {
  Tarefa *a = (Tarefa *)arg;

  /*
   * Igual ao seu Java:
   * gera um tempo aleatório e usa esse tempo antes
   * de entrar e antes de sair.
   */
  int tempo = 20 + (aleatorio(&a->seed) % 80);

  pause(tempo);

  if (a->genero == HOMEM) {
    homemQuerEntrar(a->t, a->id);
  } else {
    mulherQuerEntrar(a->t, a->id);
  }

  pause(tempo);

  pessoaSai(a->t, a->id);

  exit(0);
}

int main(int argc, char *argv[]) {
  Toalete toalete;

  int pids[MAX_CLIENTES];
  char *raw_stacks[MAX_CLIENTES];
  char *stacks[MAX_CLIENTES];
  Tarefa tarefas[MAX_CLIENTES];

  uint seed_principal = 12345;

  if (argc != 2) {
    printf("Uso: toalete numero_de_clientes\n");
    exit(1);
  }

  int num = atoi(argv[1]);

  if (num < 1 || num > MAX_CLIENTES) {
    printf("Numero de clientes deve estar entre 1 e %d\n", MAX_CLIENTES);
    exit(1);
  }

  if (toalete_init(&toalete) < 0) {
    printf("Erro ao criar semaforos\n");
    exit(1);
  }

  /*
   * Todas as stacks são alocadas antes dos clones.
   */
  for (int i = 0; i < num; i++) {
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

    tarefas[i].genero = (aleatorio(&seed_principal) % 2) + 1;
    tarefas[i].id = i + 1;
    tarefas[i].seed = 1000 + i * 97;
    tarefas[i].t = &toalete;
  }

  for (int i = 0; i < num; i++) {
    sem_wait(toalete.sem_tela);

    if (tarefas[i].genero == HOMEM) {
      printf("Homem %d criado.\n", tarefas[i].id);
    } else {
      printf("Mulher %d criada.\n", tarefas[i].id);
    }

    sem_post(toalete.sem_tela);

    pids[i] = clone(cliente, &tarefas[i], stacks[i]);

    if (pids[i] < 0) {
      printf("clone falhou\n");
      exit(1);
    }
  }

  for (int i = 0; i < num; i++) {
    if (join(pids[i]) < 0) {
      printf("join falhou\n");
      exit(1);
    }

    free(raw_stacks[i]);
  }

  toalete_destroy(&toalete);
  printf("\nTeste concluido.\n");

  exit(0);
}