#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#ifndef NULL
  #define NULL ((void*)0)
#endif

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct LinkedList queue[NQUEUE];
} ptable;

// 프로세스를 해당 레벨에 맞는 큐의 첫번째에 저장하는 함수
void enqueue_process(struct proc* p) {
    int level = p->q_level;
    struct LinkedList* queue = &ptable.queue[level];

    struct Node* newNode = (struct Node*)kalloc();
    if (newNode == NULL) {
        panic("Failed to allocate memory for new node");
    }
    newNode->process = p;
    newNode->next = NULL;

    if (queue->head == NULL) {
        queue->head = newNode;
    } else {
        struct Node* temp = queue->head;
        queue->head = newNode;
        newNode->next = temp;
    }

    queue->size++;
}

// 해당 프로세스를 큐에서 제거하는 함수
void dequeue_process(struct proc* p) {
    int level = p->q_level;
    struct LinkedList* queue = &ptable.queue[level];

    struct Node* current = queue->head;
    struct Node* prev = NULL;

    // 큐를 순회하여 삭제할 프로세스를 찾음
    while (current != NULL) {
        if (current->process == p) {
            // 프로세스를 찾았을 때
            if (prev == NULL) {
                // 삭제할 노드가 head인 경우
                queue->head = current->next;
            } else {
                // 삭제할 노드가 중간 또는 마지막인 경우
                prev->next = current->next;
            }
            // 노드 메모리 해제
            kfree((char*)current);
            queue->size--;
            return; // 프로세스를 찾았으므로 함수 종료
        }
        prev = current;
        current = current->next;
    }
}


void sortQueue(struct LinkedList* queue) {
    if (queue->head == NULL || queue->head->next == NULL) {
        return;  // 리스트가 비어있거나 노드가 하나뿐인 경우 정렬 불필요
    }
    
    struct Node* sorted = NULL; // 정렬된 리스트의 시작 포인터
    
    // 기존 리스트에서 노드를 하나씩 떼어서 정렬된 리스트에 삽입
    while (queue->head != NULL) {
        struct Node* current = queue->head;
        queue->head = queue->head->next; // 원본 리스트에서 노드 제거
        
        // 정렬된 리스트에 현재 노드 삽입
        if (sorted == NULL || current->process->io_wait_time > sorted->process->io_wait_time) {
            // 새 노드를 정렬된 리스트의 맨 앞에 삽입
            current->next = sorted;
            sorted = current;
        } else {
            // 정렬된 리스트 내에서 적절한 위치 찾기
            struct Node* temp = sorted;
            while (temp->next != NULL && temp->next->process->io_wait_time >= current->process->io_wait_time) {
                temp = temp->next;
            }
            current->next = temp->next;
            temp->next = current;
        }
    }
    
    queue->head = sorted; // 정렬된 리스트를 queue의 head로 지정
}


void aging(void) {
    struct proc *p;

    // 모든 우선순위 큐를 순회
    for (int level = 1; level < NQUEUE; level++) {
        struct LinkedList *queue = &ptable.queue[level];
        struct Node *node = queue->head;

        // 현재 큐의 모든 프로세스를 확인
        while (node != NULL) {
            p = node->process;
            // cprintf("pid: %d, io_wait_time: %d, cpu_wait: %d, level: %d\n", p->pid, p->io_wait_time, p->cpu_wait, p->q_level);

            // 프로세스가 RUNNABLE 상태이고 cpu_wait이 임계값 이상인 경우
            if (p->state == RUNNABLE && p->cpu_wait >= 250) {
                // 현재 프로세스가 최상위 큐가 아니라면 상위 큐로 이동
                if (p->q_level > 0) {
                    struct Node *next_node = node->next; // 현재 노드의 다음 노드를 미리 저장
                    dequeue_process(p);

                    // 상위 큐로 이동 및 관련 변수 초기화
                    p->q_level--; // 한 단계 높은 큐로 이동
                    p->cpu_burst = 0;
                    p->cpu_wait = 0;
                    p->io_wait_time = 0;
                    p->state = RUNNABLE;

                    // 상위 큐에 삽입
                    enqueue_process(p);

                    #ifdef DEBUG
                        cprintf("PID: %d Aging\n", p->pid);
                    #endif

                    // 다음 노드로 이동
                    node = next_node;
                    continue; // 현재 노드가 이동되었으므로 다음 반복으로 이동
                }
            }
            // 다음 노드로 이동
            node = node->next;
        }
        sortQueue(queue);
    }
}



static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // 이부분을 수정해야해
  // 수정한 부분, 추가한 변수 초기화
  if (p->pid <= 2) {
    p->q_level=3;
    p->cpu_wait=0;
    p->cpu_burst=0;
    p->cpu_used=-2;
    p->io_wait_time=0;
    p->end_time=-1;
    // cprintf("PID: %d created.(init, shell)\n", p->pid);
  } else {
    #ifdef DEBUG
      if (p->pid>3) {
        cprintf("PID: %d created.\n", p->pid);
      }
    #endif
    p->q_level=0;
    p->cpu_wait=0;
    p->cpu_burst=0;
    p->io_wait_time=0;
    p->cpu_used=0;
    p->end_time=100000;
  }
  enqueue_process(p);
  // 여기까지

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  // cprintf("Process %d is exiting with state %d, killed flag: %d\n", myproc()->pid, myproc()->state, myproc()->killed);

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // 수정할 부분
  // 이부분에서 acquire 나고 있음
  // acquire(&ptable.lock);
  dequeue_process(curproc);
  curproc->q_level = -1;

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  
  release(&ptable.lock);
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  // struct proc *p;
  // struct cpu *c = mycpu();
  // c->proc = 0;
  
  // for(;;){
  //   // Enable interrupts on this processor.
  //   sti();

  //   // Loop over process table looking for process to run.
  //   acquire(&ptable.lock);
  //   for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
  //     if(p->state != RUNNABLE)
  //       continue;

  //     // Switch to chosen process.  It is the process's job
  //     // to release ptable.lock and then reacquire it
  //     // before jumping back to us.
  //     c->proc = p;
  //     switchuvm(p);
  //     p->state = RUNNING;

  //     swtch(&(c->scheduler), p->context);
  //     switchkvm();

  //     // Process is done running for now.
  //     // It should have changed its p->state before coming back.
  //     c->proc = 0;
  //   }
  //   release(&ptable.lock);
  // }


    struct proc *p = NULL;
    struct cpu *c = mycpu();
    c->proc = 0;

    for(;;){
        // 인터럽트 활성화
        sti();

        acquire(&ptable.lock);

        // 높은 우선순위 큐부터 탐색
        for (int level = 0; level < NQUEUE; level++) {
            struct LinkedList *queue = &ptable.queue[level];
            struct Node *node = queue->head;

            // 큐 내에서 RUNNABLE 프로세스 탐색
            while (node != NULL) {
                if (node->process->state == RUNNABLE) {
                    p = node->process; // 실행할 프로세스 선택
                    break;
                }
                node = node->next;
            }

            if (p != NULL) {
                break; // RUNNABLE 프로세스 발견 시 외부 루프 탈출
            }
        }

        if (p != NULL) {
            c->proc = p;
            switchuvm(p);
            p->state = RUNNING;
            p->cpu_wait = 0;
            swtch(&(c->scheduler), p->context);
            switchkvm();
            c->proc = 0;
            p = NULL; // 다음 스케줄링을 위해 초기화
        }
        release(&ptable.lock);
    }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
    acquire(&ptable.lock);          // ptable.lock 획득
    struct proc *p = myproc();      // 현재 프로세스 가져오기
    p->state = RUNNABLE;            // 프로세스 상태를 RUNNABLE로 변경
    p->cpu_burst=0;
    p->cpu_wait = 0;
    p->io_wait_time = 0;

    // 큐에서 제거하고 한 단계 낮은 큐에 추가
    dequeue_process(p);
    if (p->q_level < NQUEUE - 1) {  // 큐 레벨이 최대치보다 낮으면
        p->q_level += 1;             // 우선순위 레벨을 낮춤
    }
    enqueue_process(p);              // 프로세스를 큐에 다시 추가

    sched();                          // 스케줄러 호출하여 컨텍스트 스위칭 수행
    release(&ptable.lock); 
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
