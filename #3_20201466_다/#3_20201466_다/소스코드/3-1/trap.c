#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "date.h"      // date.h 헤더 파일 포함

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;
// ptable extern 선언
extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
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
      wakeup(&ticks);
      release(&tickslock);
    }

    // 모든 프로세스를 순회하며 메모리 해제 여부를 체크
    acquire(&ptable.lock);
    struct proc *p;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if((p->state == SLEEPING || p->state == RUNNING || p->state == RUNNABLE) && 
          p->dealloc_bool && ticks >= p->dealloc_ticks){
        // 메모리 해제 수행
        uint newsz = p->sz - p->dealloc_size;
        if(deallocuvm(p->pgdir, p->sz, newsz)){
          // 해제 요청 시점 출력
          struct rtcdate r;
          cmostime(&r);
          cprintf("Memory deallocation excute: %d-%d-%d %d:%d:%d\n",
                  r.year, r.month, r.day, r.hour, r.minute, r.second);
          memstat();
          // 필요에 따라 프로세스 종료 또는 다른 작업 수행
          p->killed = 1;
        }
      }
    }
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
  case T_PGFLT:
    uint fault_addr = rcr2(); // 페이지 폴트가 발생한 가상 주소
    uint rest = fault_addr%PGSIZE;
    uint addr = fault_addr - rest; // 페이지 폴트가 발생한 가상주소의 시작 주소
    struct proc *q = myproc();

    // 물리 메모리 페이지 할당
    char *mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      q->killed = 1;
      break;
    }
    memset(mem, 0, PGSIZE);

    // 페이지 테이블에 매핑
    if(mappages(q->pgdir, (char*)addr, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      kfree(mem);
      p->killed = 1;
      break;
    }
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
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
