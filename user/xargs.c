#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[]) {
  // Verifica se pelo menos um comando foi passado
  if (argc < 2) {
    fprintf(2, "Usage: xargs <command> [args...]\n");
    exit(1);
  }

  char *xargs_argv[MAXARG];
  char buf[512];
  int i;

  // 1. Copia os argumentos originais do xargs para o novo array
  // Exemplo: "xargs grep hello" -> xargs_argv[0] = "grep", xargs_argv[1] = "hello"
  for (i = 1; i < argc; i++) {
    xargs_argv[i - 1] = argv[i];
  }

  int buf_idx = 0;
  char c;

  // 2. Lê a entrada padrão (stdin) um caractere por vez
  while (read(0, &c, 1) > 0) {
    if (c == '\n') {
      // 3. Chegou no fim da linha! Finaliza a string substituindo \n por \0
      buf[buf_idx] = '\0';
      
      // Adiciona a linha lida como o ÚLTIMO argumento
      xargs_argv[argc - 1] = buf;
      xargs_argv[argc] = 0; // O array do exec precisa terminar em NULL

      // 4. Cria o processo filho e executa
      if (fork() == 0) {
        exec(xargs_argv[0], xargs_argv);
        // Se o exec falhar (ex: comando não existe), imprime o erro e sai
        fprintf(2, "xargs: exec %s failed\n", xargs_argv[0]);
        exit(1);
      }
      
      // O pai DEVE esperar o filho terminar antes de ler a próxima linha
      wait(0);
      
      // Reseta o índice do buffer para ler a próxima linha do zero
      buf_idx = 0; 
    } else {
      // Se não for \n, continua guardando o caractere no buffer
      buf[buf_idx++] = c;
    }
  }

  exit(0);
}