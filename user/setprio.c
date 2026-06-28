#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  if(argc != 3){
    printf("Uso correto: setprio <pid> <nova_prioridade>\n");
    exit(1);
  }

  // A função atoi() converte o texto que o usuário digitou em números inteiros
  int pid = atoi(argv[1]);
  int priority = atoi(argv[2]);

  if(priority < 1 || priority > 10){
    printf("Erro: Prioridade inválida\n");
    exit(1);
  }

  int retorno = set_priority(pid, priority);

  if(retorno < 0) {
    printf("Erro: Processo com PID %d não encontrado.\n", pid);
  } else {
    printf("Sucesso: Prioridade do PID %d alterada para %d.\n", pid, priority);
  }

  exit(0);
}