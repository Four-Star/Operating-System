#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
    // 첫 번째 테스트: set_proc_info를 통해 프로세스 상태 설정 후 스케줄링
    printf(1, "start scheduler_test\n");

    int pid = fork();
    if (pid == 0) {
        // 자식 프로세스는 세부 정보 설정
        // printf(1, "PID: %d created\n", getpid());
        set_proc_info(0, 0, 0, 0, 500); // q_level, cpu_burst, cpu_wait_time, io_wait_time, end_time 설정
        while(1) {}
    } else {
        wait(); // 자식 프로세스가 끝날 때까지 대기
        printf(1, "end of scheduler_test\n");
        exit();
    }
}