숭실대학교 OS 과제

# xv6 SSU Scheduler

운영체제(xv6) 위에 구현하는 **동적 우선순위 조정 다단계 피드백 큐(MLFQ) 스케줄러** 과제입니다.

---

## 목표

- xv6의 기존 라운드-로빈 스케줄러를 분석하고
- **4단계 MLFQ + 동적 우선순위 조정** 스케줄러로 교체
- CPU 집중 / I/O 집중 프로세스 간 공정성 개선

---

## 큐 구성

| 레벨 | Time Quantum | 우선순위 | 비고 |
|------|-------------|----------|------|
| Q0   | 10 tick      | 최고     | 신규 프로세스 진입 |
| Q1   | 20 tick      | ↓        |  |
| Q2   | 40 tick      | ↓        |  |
| Q3   | 80 tick      | 최저     | idle / init / shell 고정 |

---

## 주요 메커니즘

### 1. 프로세스 구조 변경 (`struct proc`)
`proc` 구조체에 아래 필드 추가:

| 필드 | 설명 |
|------|------|
| `q_level` | 현재 소속 큐 레벨 (0~3) |
| `cpu_burst` | 현재 큐에서 사용한 CPU 시간 |
| `cpu_wait` | RUNNABLE 상태 후 해당 큐 대기 시간 |
| `io_wait_time` | 해당 큐에서 SLEEPING 상태 시간 |
| `end_time` | 프로세스 종료 목표 tick (설정 안 하면 -1) |

### 2. 큐 이동 규칙
- **강등**: Time Quantum 내 실행 미완료 → 한 단계 낮은 큐로 이동
- **에이징**: 250 tick 이상 낮은 우선순위에서 대기 → 한 단계 높은 큐로 승격
- **I/O 완료**: I/O 바운드 프로세스는 높은 우선순위(Q0) 유지에 유리

### 3. `scheduler()` 수정
- Q0 → Q1 → Q2 → Q3 순으로 RUNNABLE 프로세스 탐색
- 가장 높은 우선순위 큐에서 먼저 선택
- 매 tick마다 에이징 조건 검사 및 큐 이동 처리

### 4. 프로세스 초기화
- `idle`, `init`, shell 프로세스 → **Q3 고정** (다른 큐로 이동 불가)
- shell 이후 생성된 모든 프로세스 → **Q0에서 시작**

---

## 시스템 콜: `set_proc_info()`

스케줄링 테스트를 위한 프로세스 초기값 설정 시스템 콜.

````c
int set_proc_info(int q_level, int cpu_burst, int cpu_wait_time,
                  int io_wait_time, int end_time);
````

- 호출 시 각 필드를 인자 값으로 초기화
- `end_time` 미설정 시 `-1` (프로그램 정상 종료까지 실행)

---

## 테스트 프로그램 (`scheduler_test`)

- 최대 **자식 프로세스 3개** 생성 (shell 제외 스케줄 테스트 프로세스 1개 + fork 3개)
- 각 자식마다 `set_proc_info(0, 0, 0, 0, 500)` 형식으로 초기값 지정
- 표준 출력: `"start scheduler_test"` / `"end of scheduler_test"`
- 모든 자식 종료 후 테스트 프로그램 종료

---

## 과제 수행 내용

1. **기존 xv6 스케줄러 분석**
   - `scheduler()`, `sched()`, `swtch()` 등 함수 단위 상세 분석
   - 함수 콜 그래프 및 순서도 포함
   - 소스코드 주석 추가

2. **SSU Scheduler 구현**
   - `struct proc` 필드 추가
   - 4단계 MLFQ 큐 구성 및 `scheduler()` 수정
   - 에이징 / 강등 메커니즘 구현
   - `set_proc_info()` 시스템 콜 추가

3. **테스트 및 성능 분석**
   - 스케줄링 테스트 프로그램 작성 및 실행
   - CPU 집중 / I/O 집중 프로세스 혼합 테스트
   - 응답 시간, 처리량 등 성능 지표 분석

---

## end_time 예시

`end_time = 300 tick`으로 설정된 프로세스의 큐 이동 예:

````
Q0(10 tick) → Q1(20 tick) → Q2(40 tick) → Q3(80 tick) → Q3(80 tick) → Q3(70 tick) = 300 tick
````

---

## 참고

- 기반 OS: [xv6-riscv](https://github.com/mit-pdos/xv6-riscv)
- 스케줄링 알고리즘: MLFQ (Multi-Level Feedback Queue)
