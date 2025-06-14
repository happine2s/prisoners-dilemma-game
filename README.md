# 죄수의 딜레마 게임
2025년 1학기 네트워크프로토콜설계 팀 프로젝트 입니다.

이 프로젝트는 **Finite State Machine (FSM)** 기반의 통신 프로토콜 위에 설계된 **인터랙티브 게임 시스템**입니다. 아래 데모 영상에서 프로젝트의 전체 실행 흐름과 게임 진행 과정을 확인하실 수 있습니다.

<a href="https://youtu.be/TR1Dkx2DsnQ?si=IhATI3chYl9zCUOx" target="_blank">
  <img src="https://img.youtube.com/vi/TR1Dkx2DsnQ/0.jpg"/>
</a>

## 📝 프로젝트 개요
L2, L3 통신 계층을 직접 설계 및 구현하고 그 위에 두 명의 플레이어가 참여하는 심리 게임 **죄수의 딜레마** 시나리오를 구현하였습니다.

주요 목표는:
- 통신 프로토콜 계층의 구조를 직접 설계하고
- 안정적인 통신 FSM 설계를 구현하며
- FSM 기반 상태 전이를 활용한 서버-클라이언트 기반 게임 시스템을 구체화하는 것입니다.

---

## 🎮 전체 프로젝트 시나리오

### 1. 게임 준비
- 두 명의 플레이어가 연결되면 게임이 시작됩니다.
- 두 번째 플레이어를 기다린다가 대기 상태 유지

### 2. 협력/배신 게임
- 게임 시작 시 아래 시나리오 메시지 출력:
    ```
    ----------------------------------------------------------------
    <배신의 방 – 시험 기간의 죄>

    당신은 의식을 잃은 채 어딘가로 끌려왔고, 눈을 떴을 땐 낯선 방 안에 있었습니다.
    맞은편엔 또 한 명의 낯선 인물이 앉아 있습니다. 당신은 그 사람을 전혀 알지 못합니다.
    그리고 곧, 천장에서 들려오는 냉정한 기계음이 공간을 울립니다.
    ----------------------------------------------------------------
    [ System Message ]
    🧠 당신은 '시험 기간에 과제를 준 죄'로 구속되었습니다. 현재 형량은 10.0년입니다.
    "지금부터 '배신의 방' 게임을 시작합니다."
    "두 사람 모두 선택을 내려야 합니다. 선택지는 다음 두 가지입니다."
    ----------------------------------------------------------------
    1. 협력
    .두 사람이 모두 협력을 선택할 경우, 각자 형량의 1/3만큼 감형됩니다.
    2. 배신
    .한 사람이 협력, 다른 한 사람이 배신을 선택할 경우,
    .  - 협력을 고른 사람은 형량이 2배가 됩니다.
    .  - 배신을 고른 사람은 형량의 1/2만큼 감형됩니다.
    .모든 사람이 배신을 선택할 경우,
    .  - 두 사람 모두 형량이 3/2배가 됩니다.
    ----------------------------------------------------------------
    ```

- **형량 계산 규칙**

    | 플레이어 선택 결과 | 결과 |
    |----------------|------------------------------|
    | 둘 다 협력 | 형량 × 2/3 |
    | 둘 다 배신 | 형량 × 3/2 |
    | 한 명만 배신 | 협력한 플레이어: 형량 ×2 <br>배신한 플레이어: 형량 × 1/2 |

- 선택 제한 시간: 30초  
- 제한시간 초과 시 자동으로 `협력` 선택

### 3. 예측 기회

- 협력/배신 게임 이후 추가 미션 부여

    ```
    ----------------------------------------------------------------
    [System] 추가 미션이 도착했습니다.
    "당신은 상대방의 다음 선택을 예측할 기회가 1회 주어졌습니다."
    "예측에 성공할 경우 형량의 1/3만큼 감형됩니다.
    .하지만 예측에 실패할 경우, 오히려 형량이 4/3배가 됩니다."
    "예측은 단 한 번만 가능합니다. 진행하시겠습니까(Y/N)?: "
    ----------------------------------------------------------------
    ```

- **예측 성공/실패 계산**

    | 예측 결과 | 형량 변화 |
    |--------|-------------|
    | 성공 | 형량 × 1/3 |
    | 실패 | 형량 × 4/3 |

- 예측 기회는 각 플레이어당 1회 제공

### 4. 게임 종료 조건

- 매 턴 종료 후 종료 조건 검사
- 한 명이라도 **형량 ≤ 1년** 이 되면 게임 종료
- 예:
    ```
    📣 당신의 형량은 0.6년입니다.
    🎉 당신은 형량이 1년 이하가 되어 석방되었습니다! 게임에서 승리했습니다!
    ```

---
## ⚙️ 시스템 아키텍처

본 프로젝트는 통신 계층과 게임 FSM이 완전히 분리되어 모듈화 설계되었습니다

- **L3 (Network Layer):** 사용자 입력 관리 및 상위 FSM 구현
- **L2 (Data Link Layer):** 신리성 있는 데이터 송수신, ARQ 기반 재전송 프로토콜 구현
- **PHY Layer:** 저수준 물리 계층 (시뮬레이션 또는 실제 하드웨어 연동 가능)

### 주요 FSM main 파일 설명

| 파일명                                 | 역할 설명                                                                          |
| ----------------------------------- | ------------------------------------------------------------------------------ |
| **L2\_FSMmain.cpp / L2\_FSMmain.h** | L2 (데이터링크계층) FSM의 핵심 상태전이 로직을 구현합니다. 데이터 송수신, ARQ 재전송, ACK 관리, 이벤트 기반 상태전이 포함. |
| **L3\_FSMmain.cpp / L3\_FSMmain.h** | L3 (네트워크계층) FSM의 상위 사용자 입력 처리, 메시지 전송, 게임 FSM 제어 로직을 담당합니다.                    |
| **main.cpp**                        | 프로그램 진입점 (entry point) 입니다. 전체 시스템 초기화, L2 및 L3 FSM을 초기화하고 메인 루프에서 FSM을 실행합니다. |

---

## 🚀 실행 방법

### 준비물

- mbed 기반 개발보드 (NUCLEO, LPC1768 등)
- C/C++ 빌드 개발환경 IDE
- Serial Terminal (CoolTerm 등)

### 빌드 및 시행(Mac 기준)

1. mbed 보드 컴퓨터와 유선 연결
2. 본 레포지티 클론:
    ```bash
    https://github.com/happine2s/prisoners-dilemma-game.git
    ```
3. 개발환경 IDE에서 프로젝트 열기
4. 루트 디렉토리에서 터미널 실행 및 명령어 실행
    ```bash
    make
    ```
    또는
    ```bash
    make clean && make
    ```
5. BUILD 폴더에서 `myProtocol.bin` 파일을 mbed 보드에 복사/붙여넣기
6. Serial 포트로 접속 후 게임 플레이

---
## 📌 구현된 FSM 개요

### L3 FSM 상태
| 상태           | 설명          |
| ------------ | ----------- |
| `INITIAL_WAITING` |  플레이어 A, B 대기  |
| `SELECTION`  | 협력/배신 게임 상태 |
| `CHECKING`  | 선택 결과 계산 및 예측 게임 여부 확인 |
| `PREDICTION` | 예측 게임 상태    |
| `GAME_OVER`  | 게임 종료       |

### L2 FSM 상태
| 상태     | 설명                  |
| ------ | ------------------- |
| `IDLE` | 송수신 대기              |
| `TX`   | 데이터 송신 중            |
| `ACK`  | ACK 수신 대기 및 재전송 FSM |

- 이벤트 기반 비트마스크 구조
- ARQ 재전송 프로토콜

---
## 📜 사용 기술
- C/C++ 기반 FSM 프로그래밍
- mbed OS 플랫폼
- ARQ 기반 신뢰성 보장 통신 프로토콜
- 상태 기반 게임 로직 설계
