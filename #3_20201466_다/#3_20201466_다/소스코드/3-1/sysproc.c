#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

// ptable extern 선언
extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

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


int sys_ssusbrk(void) {
    int size, delay_ticks;
    if(argint(0, &size) < 0 || argint(1, &delay_ticks) < 0)
        return -1;

    struct proc *p = myproc();
    uint prev_sz = p->sz;

    // 기능 1: 지연된 메모리 할당 (size > 0)
    if(size > 0){
        // 유효성 검사: size는 0보다 크고, PGSIZE의 배수여야 함
        if(size % PGSIZE != 0)
            return -1;

        uint after_sz = p->sz + size;
        if(after_sz >= KERNBASE)
            return -1;

        // 가상 메모리 크기만 증가
        p->sz = after_sz;
        return prev_sz; // 이전 끝 주소 반환
    }
    // 기능 2: 지연된 메모리 해제 (size < 0)
    else if(size < 0){
        // 유효성 검사: size는 PGSIZE의 배수여야 함, dticks는 양수여야 함
        int dealloc_size = -size; // 음수를 양수로 변환
        if(dealloc_size % PGSIZE != 0 || delay_ticks <= 0)
            return -1;

        // 현재 ticks 값을 가져옴
        acquire(&tickslock);
        uint current_ticks = ticks;
        release(&tickslock);

        acquire(&ptable.lock);
        if(p->dealloc_bool){
            // 이전 해제 요청이 있을 경우 크기 누적 및 ticks 업데이트
            p->dealloc_size += dealloc_size;
            p->dealloc_ticks = current_ticks + delay_ticks;
        } else {
            // 새로운 해제 요청
            p->dealloc_size = dealloc_size;
            p->dealloc_ticks = current_ticks + delay_ticks;
            p->dealloc_bool = 1;
        }
        release(&ptable.lock);

        // 해제 요청 시점 출력
        struct rtcdate r;
        cmostime(&r);
        cprintf("Memory deallocation request (%d): %d-%d-%d %d:%d:%d\n",
                delay_ticks, r.year, r.month, r.day, r.hour, r.minute, r.second);

        return p->sz - p->dealloc_size; // 해제될 메모리의 시작 주소 반환
    }

    return -1; // size가 0인 경우 에러 반환
}

int sys_memstat(void) {
    memstat();
    return 0;
}