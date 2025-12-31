#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "sysproc.h"
#include "spinlock.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct LinkedList queue[NQUEUE];
} ptable;

// extern struct {
//   struct spinlock lock;
//   struct LinkedList queue[NQUEUE];
// } MLFQ;

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_set_proc_info(void)
{
  int q_level, cpu_burst, cpu_wait_time, io_wait_time, end_time;

  // 유저 프로그램에서 넘긴 인자들을 받아옴
  if (argint(0, &q_level) < 0 ||
      argint(1, &cpu_burst) < 0 ||
      argint(2, &cpu_wait_time) < 0 ||
      argint(3, &io_wait_time) < 0 ||
      argint(4, &end_time) < 0) {
    return -1;
  }

  //현재 프로세스의 정보를 수정
  struct proc *p = myproc();

  // cprintf("11111111111\n");
  acquire(&ptable.lock);
  // 먼저 큐에서 프로세스를 제거
  dequeue_process(p);

  p->q_level = q_level;  // enqueue_process(p);
  p->cpu_burst = cpu_burst;
  p->cpu_wait = cpu_wait_time;
  p->io_wait_time = io_wait_time;
  p->end_time = end_time;
  p->cpu_used = 0;
  p->state = RUNNABLE;

  // RUNNABLE 상태인 경우에만 큐에 추가
  enqueue_process(p);
  release(&ptable.lock);

  #ifdef DEBUG
    cprintf("Set process %d's info complete\n", p->pid);
  #endif

  return 0;  // 성공
}