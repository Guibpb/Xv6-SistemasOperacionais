#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Array para traduzir o número do estado para texto
// Baseado no enum procstate do kernel/proc.h
static char *states[] = {
  [0] "UNUSED",
  [1] "USED",
  [2] "SLEEP ",
  [3] "RUNNABLE", // Runnable
  [4] "RUNNING", 
  [5] "ZOMBIE"
};

int main(int argc, char *argv[]) {
  struct uproc up[64];
  int n = getprio(up);

  printf("PID\tPRIO\tFILA\t\tSTATE\tNAME\n");
  printf("---------------------------------------------------\n");

  for(int i = 0; i < n; i++) {
    // Pega a string do estado. Se for um número inválido, mostra "?"
    char *state_str = (up[i].state >= 0 && up[i].state <= 5) ? states[up[i].state] : "???";
    
    // Define a string da fila baseada no seu controle
    char *fila_str = up[i].is_active ? "ATV (1)" : "INA (0)";

    printf("%d\t%d\t%s\t\t%s\t%s\n", 
            up[i].pid, 
            up[i].priority, 
            fila_str, 
            state_str, 
            up[i].name);
  }
  
  exit(0);
}