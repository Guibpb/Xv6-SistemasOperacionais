#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

static struct proc* thread_leader(struct proc *p);
static void proc_freethreadpagetable(pagetable_t pagetable, uint64 sz);
static void kill_threads_of(struct proc *leader);
static void reap_threads_of(struct proc *leader);

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

int active_list = 0; // Controla qual "array" é o ativo no momento

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");

  for(p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    initlock(&p->memlock, "memlock");
    p->state = UNUSED;
    p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;

  if(p->pagetable){
    if(p->is_thread)
      proc_freethreadpagetable(p->pagetable, p->sz);
    else
      proc_freepagetable(p->pagetable, p->sz);
  }

  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->is_thread = 0;
  p->tg_leader = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->priority = 0;     //prioridade máxima
  p->is_kernel = 1;    //marcado como processo de sistema
  p->queue_list = 0;   //lista ativa
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz, newsz;
  struct proc *p = myproc();
  struct proc *leader = thread_leader(p);
  struct proc *t;

  acquire(&leader->memlock);

  sz = leader->sz;
  newsz = sz;

  if(n > 0){
    if(sz + n > TRAPFRAME){
      release(&leader->memlock);
      return -1;
    }

    newsz = uvmalloc(leader->pagetable, sz, sz + n, PTE_W);
    if(newsz == 0){
      release(&leader->memlock);
      return -1;
    }

    // mapear as novas páginas também nas threads
    for(t = proc; t < &proc[NPROC]; t++){
      acquire(&t->lock);

      if(t->is_thread && t->tg_leader == leader){
        for(uint64 a = PGROUNDUP(sz); a < newsz; a += PGSIZE){
          pte_t *pte = walk(leader->pagetable, a, 0);
          if(pte == 0 || (*pte & PTE_V) == 0)
            panic("growproc thread map");

          uint64 pa = PTE2PA(*pte);
          uint flags = PTE_FLAGS(*pte);

          if(mappages(t->pagetable, a, PGSIZE, pa, flags) != 0){
            release(&t->lock);
            release(&leader->memlock);
            return -1;
          }
        }

        t->sz = newsz;
      }

      release(&t->lock);
    }
  } else if(n < 0){
    newsz = sz + n;

    // primeiro remove das threads sem liberar as páginas físicas
    for(t = proc; t < &proc[NPROC]; t++){
      acquire(&t->lock);

      if(t->is_thread && t->tg_leader == leader){
        if(PGROUNDUP(newsz) < PGROUNDUP(sz)){
          uvmunmap(t->pagetable,
                   PGROUNDUP(newsz),
                   (PGROUNDUP(sz) - PGROUNDUP(newsz)) / PGSIZE,
                   0);
        }

        t->sz = newsz;
      }

      release(&t->lock);
    }

    // depois remove do líder liberando a memória física
    newsz = uvmdealloc(leader->pagetable, sz, newsz);
  }

  leader->sz = newsz;

  // mantém sz coerente também para o processo/thread atual
  for(t = proc; t < &proc[NPROC]; t++){
    acquire(&t->lock);
    if(t == leader || (t->is_thread && t->tg_leader == leader))
      t->sz = newsz;
    release(&t->lock);
  }

  release(&leader->memlock);
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  np->is_thread = 0;
  np->tg_leader = np;

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  if(p->is_thread == 0){
    kill_threads_of(p);
    reap_threads_of(p);
  }

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p && pp->is_thread == 0){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    intr_on();

    struct proc *highest_p = 0;
    int found_runnable_in_active = 0;
    
    // busca no array ativo
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      
      // checa se está executavel
      if(p->state == RUNNABLE && p->queue_list == active_list) {
        found_runnable_in_active = 1;
        
        if(highest_p == 0 || p->priority < highest_p->priority) {
            highest_p = p; 
        }
      }
      release(&p->lock);
    }

    if(highest_p != 0) {
      //re-adquire o lock do processo escolhido antes de executar
      acquire(&highest_p->lock);
      if(highest_p->state == RUNNABLE) { //confirmação de segurança
        highest_p->state = RUNNING;
        c->proc = highest_p;
        
        swtch(&c->context, &highest_p->context); //executa o processo
        
        //muda a lista
        highest_p->queue_list = 1 - active_list; 
        
        c->proc = 0;
      }
      release(&highest_p->lock);
      
    } else if (found_runnable_in_active == 0) {
      //troca de arrays
      active_list = 1 - active_list;
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("%d\t%s\t%s\tPrioridade: %d\n", p->pid, state, p->name, p->priority);
    printf("\n");
  }
}

int
set_priority(int pid, int priority)
{
  struct proc *p;

  //validação de segurança
  if(priority < 0 || priority > 10)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    //trava o processo para evitar condição de corrida
    acquire(&p->lock);
    if(p->pid == pid){
      p->priority = priority;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1; //processo com esse PID não foi encontrado
}

// Função criada para ler o estado do escalonador de prioridades
uint64
sys_getprio(void)
{
  struct proc *p;
  uint64 addr;       // Endereço de memória do usuário
  struct uproc up;   // Struct temporária para armazenar os dados
  int count = 0;     // Contador de processos ativos

  // argaddr(0, &addr) pega o argumento que o seu `ps.c` passou para a função.
  // No caso, é o endereço do array `struct uproc up[64]`.
  argaddr(0, &addr);

  // Varre a tabela inteira de processos
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    
    // Só queremos saber dos processos que estão existindo
    if(p->state != UNUSED){
      up.pid = p->pid;
      up.state = p->state;
      safestrcpy(up.name, p->name, sizeof(p->name));
      
      // --- ATENÇÃO AQUI: AJUSTE PARA OS NOMES DAS SUAS VARIÁVEIS ---
      up.priority = p->priority;     // Troque se a sua variável chamar "prio", etc.
      up.queue_list = p->queue_list;  // Troque para a variável que diz se ele está na fila ativa (1) ou expirada (0)

      if (p->queue_list == active_list) {
        up.queue_list = 1; // Se o índice dele bate com o índice ativo global, ele é Ativo
      } else {
        up.queue_list = 0; // Se for diferente, ele está na fila Inativa/Expirada
      }
      
      // A função copyout pega a struct 'up' (que preenchemos no kernel) 
      // e envia com segurança para a memória do usuário (o array do seu ps.c)
      if(copyout(myproc()->pagetable, addr + count * sizeof(struct uproc), (char *)&up, sizeof(struct uproc)) < 0) {
        release(&p->lock);
        return -1;
      }
      count++;
    }
    release(&p->lock);
  }
  
  // Retorna para o `ps.c` o número total de processos que ele encontrou
  return count; 
}

// Procura pelo processo com o PID fornecido e imprime suas páginas
int
proc_vmprint(int pid)
{
  struct proc *p;

  // Percorre a tabela de processos do Xv6
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock); // Tranca o processo para evitar que ele seja alterado enquanto lemos
    
    // Verifica se a "gaveta" está em uso e se o PID bate com o solicitado
    if(p->state != UNUSED && p->pid == pid){
      printf("Páginas de memória do processo: %s (PID: %d)\n", p->name, p->pid);
      
      // Salva o ponteiro da pagetable para podermos soltar o lock antes de imprimir
      pagetable_t pt = p->pagetable;
      release(&p->lock);

      if(pt != 0) {
        vmprint(pt); 
      }
      return 0; // Sucesso
    }
    
    release(&p->lock); // Destranca e tenta o próximo
  }
  
  return -1; // Se o loop terminar, significa que o PID não existe
}

int
kcowfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcowcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

static struct proc*
thread_leader(struct proc *p)
{
  if(p->is_thread && p->tg_leader)
    return p->tg_leader;
  return p;
}

static void
proc_freethreadpagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);

  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 0);

  freewalk(pagetable);
}

int
clone(uint64 fcn, uint64 arg, uint64 stack)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();
  struct proc *leader = thread_leader(p);

  if(stack == 0){
  printf("clone erro: stack nula\n");
  return -1;
  }

  if(stack % PGSIZE != 0){
    printf("clone erro: stack nao alinhada: %ld\n", stack);
    return -1;
  }

  if(walkaddr(p->pagetable, stack) == 0){
    printf("clone erro: inicio da stack nao mapeado: %ld\n", stack);
    return -1;
  }

  if(walkaddr(p->pagetable, stack + PGSIZE - 1) == 0){
    printf("clone erro: fim da stack nao mapeado: %ld\n", stack + PGSIZE - 1);
    return -1;
  }

  if((np = allocproc()) == 0)
    return -1;

  np->is_thread = 1;
  np->tg_leader = leader;

  acquire(&leader->memlock);

  // Compartilha as páginas de usuário, mas mantém TRAPFRAME separado.
  if(uvmshare(leader->pagetable, np->pagetable, leader->sz) < 0){
    release(&leader->memlock);
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  np->sz = leader->sz;

  release(&leader->memlock);

  // Copia registradores base.
  *(np->trapframe) = *(p->trapframe);

  // A nova thread começa em fcn.
  np->trapframe->epc = fcn;

  // Stack cresce para baixo.
  np->trapframe->sp = stack + PGSIZE;

  // Primeiro argumento da função em RISC-V vai em a0.
  np->trapframe->a0 = arg;

  // Endereço de retorno falso.
  // Se a função retornar em vez de chamar exit(), vai dar erro/trap.
  np->trapframe->ra = 0xffffffff;

  // Duplica descritores de arquivo como fork().
  for(i = 0; i < NOFILE; i++){
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  }

  np->cwd = idup(p->cwd);
  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = leader;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

int
join(int pid)
{
  struct proc *p = myproc();
  struct proc *leader = thread_leader(p);
  struct proc *np;
  int found;

  if(pid <= 0)
    return -1;

  acquire(&wait_lock);

  for(;;){
    found = 0;

    for(np = proc; np < &proc[NPROC]; np++){
      acquire(&np->lock);

      if(np->pid == pid){
        if(np->is_thread && np->tg_leader == leader){
          found = 1;

          if(np->state == ZOMBIE){
            int rpid = np->pid;
            freeproc(np);
            release(&np->lock);
            release(&wait_lock);
            return rpid;
          }
        }

        release(&np->lock);
        break;
      }

      release(&np->lock);
    }

    if(!found || killed(p)){
      release(&wait_lock);
      return -1;
    }

    sleep(leader, &wait_lock);
  }
}

static void
kill_threads_of(struct proc *leader)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    if(p == leader)
      continue;

    acquire(&p->lock);

    if(p->is_thread && p->tg_leader == leader){
      p->killed = 1;

      if(p->state == SLEEPING)
        p->state = RUNNABLE;
    }

    release(&p->lock);
  }
}

static void
reap_threads_of(struct proc *leader)
{
  struct proc *p;
  int live;

  acquire(&wait_lock);

  for(;;){
    live = 0;

    for(p = proc; p < &proc[NPROC]; p++){
      if(p == leader)
        continue;

      acquire(&p->lock);

      if(p->is_thread && p->tg_leader == leader){
        if(p->state == ZOMBIE){
          freeproc(p);
        } else {
          live = 1;
        }
      }

      release(&p->lock);
    }

    if(!live)
      break;

    sleep(leader, &wait_lock);
  }

  release(&wait_lock);
}