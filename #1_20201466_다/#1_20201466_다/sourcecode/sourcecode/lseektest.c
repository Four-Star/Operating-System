#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int main(int argc, char *argv[]) {
    int fd;
    char buf[512]; // 파일 내용을 읽을 버퍼

    if(argc < 4) {
        printf(1, "usage: lseek_test <filename> <offset> <string>\n");
        exit();
    }

    // 파일 열기
    fd = open(argv[1], O_RDWR);
    if(fd < 0) {
        printf(2, "Error opening file: %s\n", argv[1]);
        exit();
    }

    // 파일의 기존 내용 출력 (Before 상태)
    printf(1, "Before: ");
    int n;
    while((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n); // 화면에 출력
    }

    // 오프셋 이동
    // SEEK_SET
    if(lseek(fd, atoi(argv[2]), SEEK_SET) < 0) {
        printf(2, "Error using lseek\n");
        close(fd);
        exit();
    }

    // SEEK_CUR
    // if(lseek(fd, 3, SEEK_CUR) < 0) {
    //     printf(2, "Error using lseek\n");
    //     close(fd);
    //     exit();
    // }

    // // SEEK_END
    // if(lseek(fd, atoi(argv[2]), SEEK_END) < 0) {
    //     printf(2, "Error using lseek\n");
    //     close(fd);
    //     exit();
    // }

    // 새로운 문자열을 파일에 쓰기
    if(write(fd, argv[3], strlen(argv[3])) < 0) {
        printf(2, "Error writing to file\n");
        close(fd);
        exit();
    }

    // 파일 포인터를 파일 시작 위치로 이동
    if(lseek(fd, 0, SEEK_SET) < 0) {
        printf(2, "Error resetting file pointer\n");
        close(fd);
        exit();
    }

    // 변경 후 파일 내용 출력 (After 상태)
    printf(1, "After: ");
    while((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n); // 화면에 출력
    }

    // 파일 닫기
    close(fd);
    exit();
}
