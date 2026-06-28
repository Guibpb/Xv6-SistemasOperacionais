#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int value = 5;

int main(){
    int pid = fork();

    if(pid == 0){
        value += 15;
        printf("Child: pid = %d, value = %d\n", pid, value);
    }
    else{
        wait(0);
        printf("Parent: pid = %d, value = %d\n", pid, value);
        exit(0);
    }
}