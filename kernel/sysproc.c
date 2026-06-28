#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

extern struct proc proc[NPROC];
extern int set_priority(int pid, int priority);

#define MAX_SEM 10

struct sem {
  struct spinlock lock;
  int count;
  int active; //1 se estiver em uso, 0 se estiver livre
};

struct sem semaphores[MAX_SEM];

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_cowfork(void)
{
  return kcowfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_set_priority(void)
{
  int pid, priority;

  // argint(0, ...) pega o 1º argumento (pid)
  // argint(1, ...) pega o 2º argumento (priority)
  if(argint(0, &pid) < 0 || argint(1, &priority) < 0)
    return -1;

  // Vamos chamar a função real que vai fazer o trabalho no proc.c
  return set_priority(pid, priority); 
}

// Inicializa um semáforo com um valor e retorna seu ID (ou -1 se der erro)
uint64
sys_sem_init(void) {
  int initial_count;
  if(argint(0, &initial_count) < 0)
    return -1;

  for(int i = 0; i < MAX_SEM; i++) {
    if(semaphores[i].active == 0) {
      initlock(&semaphores[i].lock, "semaphore");
      semaphores[i].count = initial_count;
      semaphores[i].active = 1;
      return i; // Retorna o ID do semáforo
    }
  }
  return -1; // Semáforos esgotados
}

// Operação P (Wait / Down)
uint64
sys_sem_wait(void) {
  int id;
  if(argint(0, &id) < 0 || id < 0 || id >= MAX_SEM || semaphores[id].active == 0)
    return -1;

  struct sem *s = &semaphores[id];

  acquire(&s->lock);
  while(s->count <= 0) {
    // O sleep solta o lock do semáforo e dorme.
    // Quando acorda (via wakeup), ele readquire o lock automaticamente.
    sleep(s, &s->lock);
  }
  s->count--;
  release(&s->lock);

  return 0;
}

// Operação V (Post / Up)
uint64
sys_sem_post(void) {
  int id;
  if(argint(0, &id) < 0 || id < 0 || id >= MAX_SEM || semaphores[id].active == 0)
    return -1;

  struct sem *s = &semaphores[id];

  acquire(&s->lock);
  s->count++;
  wakeup(s); // Acorda todos os processos dormindo no canal 's'
  release(&s->lock);

  return 0;
}

// (Opcional) Função para destruir/liberar o semáforo
uint64
sys_sem_destroy(void) {
  int id;
  if(argint(0, &id) < 0 || id < 0 || id >= MAX_SEM)
    return -1;
  
  semaphores[id].active = 0;
  return 0;
}

uint64
sys_vmprint(void)
{
  int pid;
  
  // Pega o primeiro argumento (índice 0) passado pelo usuário e salva em 'pid'
  if(argint(0, &pid) < 0)
    return -1;

  // Repassa a responsabilidade de buscar e imprimir para o proc.c
  return proc_vmprint(pid);
}

uint64
sys_clone(void)
{
  uint64 fcn;
  uint64 arg;
  uint64 stack;

  argaddr(0, &fcn);
  argaddr(1, &arg);
  argaddr(2, &stack);

  return clone(fcn, arg, stack);
}

uint64
sys_join(void)
{
  int pid;

  argint(0, &pid);

  return join(pid);
}