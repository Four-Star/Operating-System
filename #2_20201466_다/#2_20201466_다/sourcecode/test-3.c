#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
    // 첫 번째 테스트: set_proc_info를 통해 프로세스 상태 설정 후 스케줄링
    printf(1, "start scheduler_test\n");

    // 첫 번째 자식 프로세스 생성
    int pid_1 = fork();
    if (pid_1 == 0) {
        // 자식 프로세스인 경우
        set_proc_info(2, 0, 0, 0, 300);
        // 필요한 작업 수행
        while(1) {}
    } else if (pid_1 > 0) {
        // 부모 프로세스인 경우
        // 두 번째 자식 프로세스 생성
        int pid_2 = fork();
        if (pid_2 == 0) {
            // 자식 프로세스인 경우
            set_proc_info(2, 0, 0, 0, 300);
            // 필요한 작업 수행
            while(1) {}
        } else if (pid_2 > 0) {
            // 부모 프로세스인 경우
            // 세 번째 자식 프로세스 생성
            int pid_3 = fork();
            if (pid_3 == 0) {
                // 자식 프로세스인 경우
                set_proc_info(2, 0, 0, 0, 300);
                while(1) {}
            } else if (pid_3 > 0) {
                // 부모 프로세스는 자식이 종료될 때까지 기다림
                wait();
                wait();
                wait();
                printf(1, "end of scheduler_test\n");
                exit();
            }
        }
    }
}
