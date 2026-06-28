#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  // Verifica se o usuário passou exatamente 1 argumento (o PID)
  if(argc < 2){
    fprintf(2, "Uso correto: mempages <pid> <pid2> <pid3> ...\n");
    exit(1);
  }

  for(int i = 1; i < argc; i++){
    int pid = atoi(argv[i]); // Converte a string do argumento para inteiro

    printf("Processo %d: ", i);

    // Chama a syscall passando o PID. Se retornar menor que 0, deu erro.
    if(vmprint(pid) < 0){
      fprintf(2, "mempages: erro, processo com PID %d nao encontrado ou inativo.\n", pid);
    }

    printf("\n");
  }

  exit(0);
}