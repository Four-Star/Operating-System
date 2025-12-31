#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct LinkedList queue[NQUEUE];
} ptable;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      // cprintf("ticks: %d\n", ticks);
      wakeup(&ticks);
      release(&tickslock);
      // 수정
      acquire(&ptable.lock);
      struct proc *p = myproc();
      if (p) {
        // cpu_burst, cpu_wait, io_wait_time 조정
        p->cpu_burst+=1;
        p->cpu_wait = 0;
        p->io_wait_time = 0;
      }

      struct proc *other;
      // cpu_wait, io_wait_time 조정
      for (other=ptable.proc; other<&ptable.proc[NPROC]; other++) {
        if (other != p && other->state==RUNNABLE) {
          other->cpu_wait++;
        }
        if (other != p && other->state==SLEEPING) {
          other->io_wait_time++;
        }
      }
      release(&ptable.lock);
      
      // 프로세스가 끝난 것을 endtime으로 조정
      if (p && p->end_time!=-1 && p->cpu_burst>=p->end_time-p->cpu_used) {
        p->cpu_used += p->cpu_burst;
        #ifdef DEBUG
          cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n",p->pid, p->cpu_burst, p->q_level, p->cpu_used, p->end_time);
          cprintf("PID: %d, used %d ticks. terminated\n", p->pid, p->cpu_used);
        #endif
        dequeue_process(myproc());
        p->killed = 1;
      }
      // 여기까지
    }

    // 에이징 추가
    acquire(&ptable.lock);
    aging();
    release(&ptable.lock);
    
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  // yield를 호출하는 구문, cpu_burst가 time slice를 넘기면 그 때 yield를 호출함
  struct proc *p = myproc();
  if(p && tf->trapno == T_IRQ0+IRQ_TIMER) {
    if (p->cpu_burst>=(1<<(p->q_level))*10) {
      p->cpu_used += p->cpu_burst;
      acquire(&tickslock);
      #ifdef DEBUG
        if (p->pid>2)
          cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n",p->pid, p->cpu_burst, p->q_level, p->cpu_used, p->end_time);
      #endif
      release(&tickslock);
      yield();    
    }
  }

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
