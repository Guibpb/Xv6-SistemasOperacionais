#include "kernel/types.h"
#include "user/user.h"

static void
fail(char *msg)
{
  printf("cowmini: FAIL: %s\n", msg);
  exit(1);
}

int main(void)
{
  int pid;
  int xstatus;
  int *p;

  p = (int *)sbrk(4096); // cria 1 pagina de memoria nova e retorna o endereço

  int pid_pai = getpid();
   
  if((uint64)p == (uint64)-1) // erro no sbrk
    fail("sbrk falhou");

  *p = 123; // processo pai escreve no endereço inicial de sua pagina
  printf("antes do fork: *p = %d\n", *p);

  pid = cowfork();
  if(pid < 0)
    fail("fork falhou");

  if(pid == 0){
    int meu_pid = getpid();

    printf("conteudo da pagina do filho antes de escrever = %d\n", *p); // filho apenas copiou o endereço de memoria do pai
    printf("\nPid pai = %d\nPid filho = %d\n\n", pid_pai, meu_pid);


    pause(50);

    *p = 999; // tenta escrever na pagina, como esta no COW dá erro e aloca outra pagina

    printf("conteudo da pagina do filho depois de escrever = %d\n", *p); // conteudo da pagina nova do filho
    pause(50);


    exit(0);
  }

  wait(&xstatus);

  printf("conteudo da pagina do pai = %d\n", *p); // endereço de memoria do pai

  if(*p != 123) // o pai mantem o mesmo endereço porem o filho nao, para o COW funcionar
    fail("pai foi alterado, COW nao funcionou");

  printf("Copy-on-Write funcionou\n");
  exit(0);
}