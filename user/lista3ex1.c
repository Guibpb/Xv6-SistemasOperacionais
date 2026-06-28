#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096

typedef struct {
  int semRegiao;
  int semTela;
  int numero;
} Args;

void tarefa(void *arg) {
  Args *a = (Args *)arg;

  //trava o print
  sem_wait(a->semTela);
  printf("Thread %d tentando adquirir o lock\n", a->numero);
  sem_post(a->semTela);

  //tenta entrar no semaforo
  sem_wait(a->semRegiao);

  sem_wait(a->semTela);
  printf("Thread %d entrou na regiao critica\n", a->numero);
  sem_post(a->semTela);

  //tempo de execucao
  pause(40);

  sem_wait(a->semTela);
  printf("Thread %d saiu da regiao critica\n", a->numero);
  sem_post(a->semTela);

  //sai do semaforo
  sem_post(a->semRegiao);

  exit(0);
}

int main(int argc, char *argv[]) {
  int vagas = 2;
  int semRegiao;
  int semTela;
  int numThreads = 4;

  int pids[numThreads];
  char *raw_stacks[numThreads];
  char *stacks[numThreads];
  Args args[numThreads];

  semRegiao = sem_init(vagas);
  semTela = sem_init(1);

  if (semRegiao < 0 || semTela < 0) {
    printf("Erro ao criar semaforos\n");
    exit(1);
  }

  printf("Teste do semaforo com %d vagas, para %d threads\n\n", vagas, numThreads);

  //aloca todas as pilhas para as threads
  for (int i = 0; i < numThreads; i++) {
    uint64 addr;

    raw_stacks[i] = malloc(PGSIZE * 2);

    if (raw_stacks[i] == 0) {
      printf("malloc falhou\n");
      exit(1);
    }

    addr = (uint64)raw_stacks[i];

    if (addr % PGSIZE != 0)
      addr = addr + (PGSIZE - addr % PGSIZE);

    stacks[i] = (char *)addr;

    args[i].semRegiao = semRegiao;
    args[i].semTela = semTela;
    args[i].numero = i + 1;
  }

  //cria as threads
  for (int i = 0; i < numThreads; i++) {
    pids[i] = clone(tarefa, &args[i], stacks[i]);

    if (pids[i] < 0) {
      printf("clone falhou\n");
      exit(1);
    }
  }

  //da join em todas
  for (int i = 0; i < numThreads; i++) {
    if (join(pids[i]) < 0) {
      printf("join falhou\n");
      exit(1);
    }

    free(raw_stacks[i]);
  }

  sem_destroy(semRegiao);
  sem_destroy(semTela);

  printf("\nTeste finalizado.\n");

  exit(0);
}