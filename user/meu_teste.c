#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int pid = fork();

  if(pid < 0){
    printf("Erro no fork\n");
    exit(1);
  }

  if(pid == 0){
    printf("Thread filho, PID: %d\n", getpid());
    pause(10); 
    printf("Encerrando thread filho\n");
    exit(0);
  } else {
    wait(0); 
    printf("Thread pai, processo com PID %d criado\n", pid);
    
    printf("Encerrando thread pai\n");
    exit(0);
  }
}