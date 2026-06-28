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
  int pid1, pid2;

  int pid_pai1 = getpid();

  pid1 = fork();

  if(pid1 < 0)
    fail("fork falhou");

  if(pid1 == 0){
    int meu_pid1 = getpid();
    printf("1º fork: ");
    printf("\nPid pai = %d\nPid filho = %d\n\n", pid_pai1, meu_pid1);

    pause(50); //pausa por 5 seg
  }
  else{
    wait(0);
    exit(0);
  }

  int pid_pai2 = getpid();
  pid2 = cowfork();

  if(pid2 < 0)
    fail("cowfork falhou");

  if(pid2 == 0){
   int meu_pid2 = getpid();
    printf("2º fork(cowfork): ");
    printf("\nPid pai = %d\nPid filho = %d\n\n", pid_pai2, meu_pid2);

    pause(50); //pausa por 5 seg 
  }
  else{
    wait(0);
    exit(0);
  }

  
  exit(0);
}