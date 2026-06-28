#include "kernel/types.h"
#include "user/user.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"

// Função que representa cada estágio do filtro
void
sieve(int pleft[2])
{
  int prime;
  int n;

  // Fecha a ponta de escrita do pipe que vem da esquerda.
  // Este processo apenas LÊ da esquerda.
  close(pleft[1]);

  // Tenta ler o primeiro número do pipe.
  // Pela lógica matemática, o primeiro número que chega num estágio
  // É SEMPRE UM PRIMO (pois passou pelos filtros anteriores).
  if(read(pleft[0], &prime, sizeof(int)) == 0){
    // Se o pipe fechou e não veio nada, terminamos.
    close(pleft[0]);
    exit(0);
  }

  // Imprime o primo encontrado
  printf("prime %d\n", prime);

  // Cria um novo pipe para falar com o próximo filho (vizinho da direita)
  int pright[2];
  pipe(pright);

  if(fork() == 0){
    // --- PROCESSO FILHO (Próximo Estágio) ---
    
    // O filho não precisa ler do pipe do avô (pleft), 
    // ele vai ler do pipe do pai (pright).
    close(pleft[0]); 
    
    // O filho herda o pright. Ele vai ser o "lado direito" do pai.
    // Chamada recursiva para ser o próximo filtro.
    sieve(pright);
    
  } else {
    // --- PROCESSO PAI (Filtro Atual) ---

    // O pai não vai ler do pipe que ele acabou de criar, só escrever.
    close(pright[0]);

    // Loop de filtragem:
    // Lê números vindos da esquerda...
    while(read(pleft[0], &n, sizeof(int)) != 0){
      // ...se NÃO for múltiplo do meu primo...
      if(n % prime != 0){
        // ...passa para a direita (para o filho).
        write(pright[1], &n, sizeof(int));
      }
    }

    // Acabou a entrada da esquerda.
    close(pleft[0]);
    
    // Fecha a saída para a direita. Isso avisa o filho que acabou (EOF).
    close(pright[1]);

    // Espera o filho terminar (para garantir a ordem e limpar zumbis)
    wait(0);
    
    exit(0);
  }
}

int
main(int argc, char *argv[])
{
  int p[2];
  pipe(p);

  if(fork() == 0){
    // O primeiro filho inicia a cadeia de filtros
    sieve(p);
  } else {
    // Processo Principal: Gerador de Números
    close(p[0]); // Só escreve

    // Alimenta o pipeline com 2 a 35
    for(int i = 2; i <= 35; i++){
      write(p[1], &i, sizeof(int));
    }

    // Fecha o pipe de escrita. Isso envia EOF para o primeiro filho.
    close(p[1]);

    // Espera a cadeia inteira terminar
    wait(0);
    exit(0);
  }
  return 0;
}