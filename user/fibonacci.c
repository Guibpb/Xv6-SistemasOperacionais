#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]){
    if(argc != 2){
    printf("Uso correto: fibonacci <num_max>\n");
    exit(1);
    }


    int limite = atoi(argv[1]);
    if(limite < 1){
        printf("Limite errado");
    }

    int pid = fork();

    if(pid < 0){
        printf("Erro no fork\n");
        exit(1);
    }

    if(pid == 0){
        printf("Fibonacci: \n0  ");
        int last2 = 0;
        int last1 = 0;
        int current = 1;

        while(current <= limite){
            printf("%d  ", current);

            last2 = current;
            current += last1;
            last1 = last2;
        }
        printf("\n");
    }
    else{
        wait(0);
        exit(0);
    }
}