#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  //Semáforo da região crítica com n vagas
  int vagas = 2;
  int sem_id = sem_init(vagas);
  
  //Semáforo do print
  int sem_tela = sem_init(1); 

  printf("Teste do Semaforo Contador, com %d vagas\n", vagas);

  for(int i = 0; i < 4; i++) {
    int pid = fork();
    
    if(pid == 0) {
      //Pega o lock do print
      sem_wait(sem_tela);
      printf("Filho %d --- Tentando adquirir o lock\n", getpid());
      sem_post(sem_tela); // Solta a tela
      
      //Tenta entrar na região crítica
      sem_wait(sem_id); 
      
      sem_wait(sem_tela);
      printf("Filho %d --- Entrou na regiao critica\n", getpid());
      sem_post(sem_tela);
      
      pause(30); //Simular tempo de processo
      
      sem_wait(sem_tela);
      printf("Filho %d --- Saindo da regiao critica.\n", getpid());
      sem_post(sem_tela);
      
      //Libera a vaga
      sem_post(sem_id); 
      
      exit(0);
    }
  }

  for(int i = 0; i < 4; i++) {
    wait(0);
  }

  sem_destroy(sem_id);
  sem_destroy(sem_tela);
  
  printf("Teste concluido. Semaforos destruidos.\n");
  exit(0);
}