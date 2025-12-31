//
#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

//추가
#include "syscall.h"
#include "fs.h"
#include "fcntl.h"
#include "spinlock.h"
#include "file.h"
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2



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

// 추가
int
sys_lseek(void) {
    int fd, whence;         // 파일 디스크립터와 기준점(whence)을 저장할 변수 선언
    uint off;               // 설정할 오프셋(offset) 값을 저장할 변수
    struct file *f;         // 파일 포인터를 저장할 변수

    // 시스템 호출 인자에서 0번째 인자인 파일 디스크립터(fd)를 가져옴
    if(argint(0, &fd) < 0)
        return -1;

    // 시스템 호출 인자에서 1번째 인자인 오프셋(offset)을 가져옴
    if(argint(1, (int*)&off) < 0)
        return -1;

    // 시스템 호출 인자에서 2번째 인자인 기준점(whence)을 가져옴
    if(argint(2, &whence) < 0)
        return -1;

    // 해당 파일 디스크립터가 열려 있는지 확인하고, 열려 있는 파일 포인터를 f에 저장
    if((f = myproc()->ofile[fd]) == 0) // myproc()->ofile은 현재 프로세스의 열린 파일 리스트
        return -1;

    // 해당 파일이 실제 파일(inode 파일)인지 확인 (FD_INODE 타입이어야 함)
    if(f->type != FD_INODE)
        return -1;

    uint new_off = 0;
    uint original_size = f->ip->size;
    // int has_newline = 0;              // 파일 끝에 개행 문자가 있는지 여부를 저장할 변수

    // 파일 끝이 개행 문자(\n)로 끝나는지 확인
    if (original_size > 0) {
        char last_char;

        // 파일 끝에서 1바이트를 읽어와서 개행 문자인지 확인
        ilock(f->ip);  // inode 잠금 (inode 읽기 작업 보호)
        if (readi(f->ip, &last_char, original_size - 1, 1) == 1 && last_char == '\n') {
            // has_newline = 1;  // 파일 끝에 개행 문자가 있음
            original_size--;  // 개행 문자를 무시한 크기로 설정
        }
        iunlock(f->ip);  // inode 잠금 해제
    }

    // whence 값에 따라 파일의 오프셋 위치를 설정함
    switch(whence) {
        case SEEK_SET:      // 파일의 시작 위치로부터 오프셋 설정
            new_off = off;   // 오프셋을 그대로 설정
            break;

        case SEEK_CUR:      // 현재 위치로부터 오프셋 이동
            new_off = f->off + off;  // 현재 오프셋에 입력받은 오프셋 값을 더함
            break;

        case SEEK_END:      // 파일의 끝 위치로부터 오프셋 이동
            new_off = original_size + off;  // 개행 문자를 제외한 위치를 기준으로 계산
            break;

        default:            // 잘못된 whence 값이 들어온 경우
            return -1;      // -1을 반환하여 에러 처리
    }

    // 새로운 오프셋이 파일 크기보다 큰 경우 널 문자(\0)로 채우기
    if (new_off > original_size) {
        // 파일의 끝으로 이동한 후 널 문자 채우기
        char zero = '\0';
        char newline = '\n';
        for (uint i = original_size; i < new_off; i++) {
            f->off = i;  // 파일 오프셋을 설정
            if (filewrite(f, &zero, 1) != 1) {  // 1바이트씩 \0 기록
                return -1;
            }
        }
        f->off = new_off;
        if (filewrite(f, &newline, 1) != 1) {  // 1바이트씩 \0 기록
            return -1;
        }
        f->off = new_off;  // 현재 새로운 오프셋 위치로 이동
        original_size = new_off;  // 파일 크기를 새로운 오프셋으로 업데이트
    }

    // 파일의 끝에 개행 문자가 없는 경우 개행 문자 추가
    if (original_size > 0) {
        char last_char;
        f->off = original_size - 1;  // 파일 끝에서 한 문자 전 위치로 이동

        if (fileread(f, &last_char, 1) == 1) {  // 마지막 문자 읽기
            if (last_char != '\n') {  // 마지막 문자가 개행 문자가 아닌 경우
                char newline = '\n';
                f->off = original_size;  // 파일 끝으로 이동
                if (filewrite(f, &newline, 1) != 1) {  // 개행 문자 추가
                    return -1;
                }
            }
        }
    }
    
    f->off = new_off;  // 새로운 오프셋 설정
    // 설정된 오프셋이 음수인 경우 0으로 변경하여 안전한 위치로 설정
    if(f->off < 0) {
        f->off = 0;
    }

    return f->off;          // 설정된 새로운 오프셋 반환
}
