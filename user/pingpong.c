#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int p2c[2], c2p[2];
  char buf[1]; // Buffer para trocar o byte

  // Inicializa os pipes
  if(pipe(p2c) < 0 || pipe(c2p) < 0){
    printf("Erro ao criar pipes\n");
    exit(1);
  }

  int pid = fork();

  if(pid < 0){
    printf("Erro no fork\n");
    exit(1);
  }

  if(pid == 0){
    if(read(p2c[0], buf, 1) != 1){
      printf("Filho: erro de leitura\n");
      exit(1);
    }

    printf("%d: received ping\n", getpid());

    // O filho escreve de volta no pipe c2p (ponta de escrita é o índice 1)
    if(write(c2p[1], buf, 1) != 1){
      printf("Filho: erro de escrita\n");
      exit(1);
    }

    close(p2c[0]);
    close(c2p[1]);
    
    exit(0);

  } else {
    if(write(p2c[1], "x", 1) != 1){
      printf("Pai: erro de escrita\n");
      exit(1);
    }

    // O pai agora tenta ler o "pong" do filho no pipe c2p
    // Ele ficará bloqueado aqui até o filho enviar o byte
    if(read(c2p[0], buf, 1) != 1){
      printf("Pai: erro de leitura\n");
      exit(1);
    }

    printf("%d: received pong\n", getpid());

    // Fecha os descritores de arquivo após o uso
    close(p2c[1]);
    close(c2p[0]);

    exit(0);
  }
}