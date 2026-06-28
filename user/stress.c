#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  // O 'volatile' impede que o compilador otimize e apague esse loop inútil
  volatile unsigned int x = 0; 
  
  printf("Iniciando processo de stress (PID %d)...\n", getpid());
  
  //loop infinito
  while(1) {
    x++; 
  }
  
  exit(0);
}