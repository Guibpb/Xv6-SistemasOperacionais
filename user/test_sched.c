#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


void heavy_work(int p) {
    volatile long long count = 0;
    for (long long i = 0; i < 3000000000LL; i++) {
        count += i;
    }
    printf("task finalizada, prioridade: %d (PID %d)\n", p, getpid());
    exit(0);
}

int main() {
    int prios[] = {10, 0, 5, 1};
    int n = 4;

    printf("Criando %d processos com prioridades distintas...\n", n);

    for (int i = 0; i < n; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("Erro no fork\n");
            exit(1);
        }

        if (pid == 0) {
            // Filho
            set_priority(getpid(), prios[i]);
            
            // Sincronização: todos dormem para garantir que o escalonador
            // veja todos no estado RUNNABLE ao mesmo tempo antes de começar.
            pause(20 + (prios[i] * 5)); 
            
            printf("Processo PID %d pronto (Prioridade %d)\n", getpid(), prios[i]);
            heavy_work(prios[i]);
        }
    }

    // Pai espera todos terminarem
    for (int i = 0; i < n; i++) {
        wait(0);
    }

    exit(0);
}