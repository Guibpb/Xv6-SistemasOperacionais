#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096

int global = 1;

void
worker(void *arg)
{
  int x = *(int*)arg;

  printf("Thread: global antes = %d\n", global);

  global += x;

  printf("Thread: global depois = %d\n", global);

  exit(0);
}

int
main(int argc, char *argv[])
{
  char *raw_stack;
  char *stack;
  uint64 addr;
  int arg = 42;
  int pid;

  raw_stack = malloc(PGSIZE * 2);

  if(raw_stack == 0){
    printf("Erro: falha na alocacao da stack.\n");
    exit(1);
  }

  /*
   * malloc nao garante alinhamento em pagina.
   * Entao alocamos 2 paginas e alinhamos manualmente.
   */
  addr = (uint64) raw_stack;

  if(addr % PGSIZE != 0)
    addr = addr + (PGSIZE - addr % PGSIZE);

  stack = (char*) addr;

  printf("Processo pai: global inicial = %d\n", global);

  pid = clone(worker, &arg, stack);

  if(pid < 0){
    printf("Erro: clone falhou.\n");
    exit(1);
  }

  if(join(pid) < 0){
    printf("Erro: join falhou.\n");
    exit(1);
  }

  printf("Processo pai: global apos o join = %d\n", global);

  if(global == 43)
    printf("Teste concluido: a memoria foi compartilhada entre pai e thread.\n");
  else
    printf("Teste falhou: a alteracao da thread nao foi refletida no pai.\n");

  exit(0);
}