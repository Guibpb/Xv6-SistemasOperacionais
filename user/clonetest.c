#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096

int global = 1;

void
worker(void *arg)
{
  printf("filho antes: %d\n", global);
  int x = *(int*)arg;

  global += x;

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
    printf("malloc falhou\n");
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

  //printf("raw_stack = %p\n", raw_stack);
  //printf("stack alinhada = %p\n", stack);

  printf("pai antes: global = %d\n", global);

  pid = clone(worker, &arg, stack);

  if(pid < 0){
    printf("clone falhou\n");
    exit(1);
  }

  if(join(pid) < 0){
    printf("join falhou\n");
    exit(1);
  }

  printf("clone criou thread com pid %d\n", pid);
  printf("pai depois: global = %d\n", global);

  if(global == 43)
    printf("clone/join funcionou\n");
  else
    printf("erro: memoria nao foi compartilhada\n");

  exit(0);
}