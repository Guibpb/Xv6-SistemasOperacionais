#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define PGSIZE 4096
#define MAX_JOBS 16

typedef struct {
  int fd_spool;
  int sem_impressora; // só um job imprime por vez
  int sem_tela;       // evita mistura nos printf
} PrintSpooler;

typedef struct {
  PrintSpooler *spooler;
  char *job_name;
  int id;
  uint seed;
} Tarefa;

/* Gera um número pseudoaleatório para cada thread. */
uint
aleatorio(uint *seed)
{
  *seed = (*seed * 1103515245) + 12345;
  return *seed;
}

/*
 * Equivale ao openPrintSpooler() do Java.
 * Retorna 1 em caso de sucesso e 0 em caso de erro.
 */
int
openPrintSpooler(PrintSpooler *spooler, char *spool_file)
{
  spooler->fd_spool = -1;
  spooler->sem_impressora = -1;
  spooler->sem_tela = -1;

  /*
   * Garante que o arquivo comece vazio.
   * Se ele não existir, unlink apenas falha e seguimos normalmente.
   */
  unlink(spool_file);

  spooler->fd_spool = open(spool_file, O_CREATE | O_WRONLY);

  if (spooler->fd_spool < 0) {
    printf("Erro ao abrir arquivo de spool: %s\n", spool_file);
    return 0;
  }

  /*
   * Capacidade 1:
   * apenas uma thread pode imprimir por vez.
   */
  spooler->sem_impressora = sem_init(1);

  /*
   * Apenas para organizar mensagens no terminal.
   */
  spooler->sem_tela = sem_init(1);

  if (spooler->sem_impressora < 0 || spooler->sem_tela < 0) {
    printf("Erro ao criar semaforos\n");

    close(spooler->fd_spool);

    if (spooler->sem_impressora >= 0)
      sem_destroy(spooler->sem_impressora);

    if (spooler->sem_tela >= 0)
      sem_destroy(spooler->sem_tela);

    return 0;
  }

  return 1;
}

/*
 * Copia o arquivo de entrada para o arquivo de spool.
 * A escrita ocorre ao encontrar '\n', portanto é simulada linha por linha.
 */
void
copiarArquivoLinhaPorLinha(int fd_entrada, int fd_spool)
{
  char linha[128];
  char c;
  int tamanho_linha = 0;

  while (read(fd_entrada, &c, 1) == 1) {
    linha[tamanho_linha++] = c;

    /*
     * Escreve quando chega ao fim de uma linha.
     * Se uma linha for grande demais, ela é escrita em partes.
     */
    if (c == '\n' || tamanho_linha == sizeof(linha)) {
      write(fd_spool, linha, tamanho_linha);
      tamanho_linha = 0;
    }
  }

  /*
   * Caso o arquivo não termine com '\n'.
   */
  if (tamanho_linha > 0) {
    write(fd_spool, linha, tamanho_linha);
  }
}

/*
 * Equivale ao printJob(String jobName) do Java.
 */
void
printJob(PrintSpooler *spooler, char *job_name, int id)
{
  int fd_job;

  sem_wait(spooler->sem_tela);
  printf("Thread %d solicitou a impressao de %s\n", id, job_name);
  sem_post(spooler->sem_tela);

  /*
   * Este é o ponto principal:
   * se outra thread já estiver imprimindo, esta fica bloqueada.
   */
  sem_wait(spooler->sem_impressora);

  sem_wait(spooler->sem_tela);
  printf("Spooler: iniciando impressao de %s\n", job_name);
  sem_post(spooler->sem_tela);

  fd_job = open(job_name, O_RDONLY);

  if (fd_job < 0) {
    sem_wait(spooler->sem_tela);
    printf("Erro: nao foi possivel abrir %s\n", job_name);
    sem_post(spooler->sem_tela);

    sem_post(spooler->sem_impressora);
    return;
  }

  /*
   * Cabeçalho para separar os arquivos dentro do spool.
   */
  write(spooler->fd_spool, "\n===== INICIO: ", 17);
  write(spooler->fd_spool, job_name, strlen(job_name));
  write(spooler->fd_spool, " =====\n", 7);

  copiarArquivoLinhaPorLinha(fd_job, spooler->fd_spool);

  write(spooler->fd_spool, "\n===== FIM: ", 14);
  write(spooler->fd_spool, job_name, strlen(job_name));
  write(spooler->fd_spool, " =====\n", 7);

  close(fd_job);

  sem_wait(spooler->sem_tela);
  printf("Spooler: terminou impressao de %s\n", job_name);
  sem_post(spooler->sem_tela);

  /*
   * Libera a impressora para o próximo job.
   */
  sem_post(spooler->sem_impressora);
}

/*
 * Equivale ao closePrintSpooler() do Java.
 * Só deve ser chamado depois de todos os joins.
 */
void
closePrintSpooler(PrintSpooler *spooler)
{
  close(spooler->fd_spool);

  sem_destroy(spooler->sem_impressora);
  sem_destroy(spooler->sem_tela);
}

/*
 * Equivale ao run() da sua classe Tarefa em Java.
 */
void
cliente(void *arg)
{
  Tarefa *t = (Tarefa *)arg;
  int tempo;

  /*
   * Espera um período aleatório antes de pedir impressão.
   */
  tempo = 20 + (aleatorio(&t->seed) % 80);
  pause(tempo);

  printJob(t->spooler, t->job_name, t->id);

  exit(0);
}

int
main(int argc, char *argv[])
{
  PrintSpooler spooler;

  int pids[MAX_JOBS];
  char *raw_stacks[MAX_JOBS];
  char *stacks[MAX_JOBS];
  Tarefa tarefas[MAX_JOBS];

  int num_jobs;

  /*
   * Exemplo:
   * spoolertest spool.txt arq1.txt arq2.txt arq3.txt
   */
  if (argc < 3) {
    printf("Uso: spoolertest arquivo_spool job1.txt [job2.txt ...]\n");
    exit(1);
  }

  num_jobs = argc - 2;

  if (num_jobs > MAX_JOBS) {
    printf("Numero maximo de jobs: %d\n", MAX_JOBS);
    exit(1);
  }

  /*
   * O arquivo de spool é aberto antes das threads existirem.
   */
  if (!openPrintSpooler(&spooler, argv[1])) {
    exit(1);
  }

  /*
   * Prepara argumentos e uma stack própria para cada clone.
   */
  for (int i = 0; i < num_jobs; i++) {
    uint64 addr;

    raw_stacks[i] = malloc(PGSIZE * 2);

    if (raw_stacks[i] == 0) {
      printf("malloc falhou\n");
      exit(1);
    }

    addr = (uint64)raw_stacks[i];

    if (addr % PGSIZE != 0) {
      addr += PGSIZE - (addr % PGSIZE);
    }

    stacks[i] = (char *)addr;

    tarefas[i].spooler = &spooler;
    tarefas[i].job_name = argv[i + 2];
    tarefas[i].id = i + 1;
    tarefas[i].seed = 1000 + i * 97;
  }

  /*
   * Cria uma thread por arquivo a ser impresso.
   */
  for (int i = 0; i < num_jobs; i++) {
    pids[i] = clone(cliente, &tarefas[i], stacks[i]);

    if (pids[i] < 0) {
      printf("clone falhou\n");
      exit(1);
    }
  }

  /*
   * Espera todas as solicitações terminarem.
   */
  for (int i = 0; i < num_jobs; i++) {
    if (join(pids[i]) < 0) {
      printf("join falhou\n");
      exit(1);
    }

    free(raw_stacks[i]);
  }

  /*
   * Só fecha o spool depois que ninguém mais pode escrever nele.
   */
  closePrintSpooler(&spooler);

  printf("Todas as impressoes terminaram.\n");

  exit(0);
}