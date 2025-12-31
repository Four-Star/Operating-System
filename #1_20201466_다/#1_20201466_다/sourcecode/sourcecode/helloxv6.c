#include "types.h"   // 기본 데이터 타입들을 정의한 헤더 파일을 포함
#include "stat.h"    // 파일 상태 관련 구조체(stat 구조체 등)를 정의한 헤더 파일을 포함
#include "user.h"    // 사용자 모드에서 사용할 수 있는 시스템 콜과 관련된 함수들을 정의한 헤더 파일 포함

int main(int argc, char **argv) {
    // 1번 파일 디스크립터(stdout)에 Hello xv6 World 출력
    printf(1, "Hello xv6 World\n");
    // 프로그램 종료
    exit();
}