#include "L3_FSMevent.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "L3_LLinterface.h"
#include "protocol_parameters.h"
#include "mbed.h"

// FSM state
#define L3STATE_INITIAL_WAITING 0
#define L3STATE_SELECTION 1
#define L3STATE_CHECKING 2
#define L3STATE_PREDICTION 3
#define L3STATE_GAME_OVER 99

// state variables
static uint8_t main_state = L3STATE_INITIAL_WAITING;
static uint8_t prev_state = main_state;

// 라운드 변수
static int round_cnt = 0;

// 상호작용 메시지 출력 플래그
static bool selection_msg_printed = false; // SELECTION 상태 진입 시 메시지 출력을 한 번만 하도록
static bool ready_to_play = false;         // 내가 게임 시작에 동의했는지
static bool peer_ready = false;            // 상대방이 게임 시작에 동의했는지
static bool prompt_sent = false;           // INITIAL_WAITING 프롬프트 출력을 한 번만 하도록

// SELECTION 상태 변수
static int my_choice = 0;                 // 나의 협력/배신 선택 (0:미정, 1:협력, 2:배신)
static int peer_choice = 0;               // 상대방의 협력/배신 선택 (0:미정, 1:협력, 2:배신)
static bool peer_choice_received = false; // 상대방 선택 수신 여부
static bool result_printed = false;       // 현재 라운드 결과 출력 여부

static float sentence = 10.0; // 현재 형량 (시작: 10년)

// CHECKING 상태 변수
static bool my_used_prediction = false;            // 내가 예측게임 기회를 사용했는지 (Y를 선택했을 때만 true 유지)
static bool peer_used_prediction = false;          // 상대가 예측게임 기회를 사용했는지 (Y를 선택했을 때만 true 유지)
static int my_prediction_yn_choice = 0;            // 내 예측게임 참여 Y/N (0:미정, 1:Y, 2:N)
static int peer_prediction_yn_choice = 0;          // 상대 예측게임 참여 Y/N (0:미정, 1:Y, 2:N)
static bool predict_yn_input_done = false;         // 내 Y/N 입력 완료 여부 (한 라운드 내에서)
static bool peer_prediction_yn_input_done = false; // 상대 Y/N 입력 완료 여부 (한 라운드 내에서)
static bool predict_propmt_sent = false;           // 예측 게임 Y/N 프롬프트 출력을 한 번만 하도록

// PREDICTION 상태 변수
static int prediction_value = 0;                     // 내가 예측한 값 (1:협력, 2:배신)
static bool prediction_input_received = false;       // 내 예측값 입력 완료 여부
static bool peer_prediction_result_received = false; // 상대방 예측값 수신 완료 여부 (PREDICTION 상태에서 받음)
static bool prediction_input_prompt_sent = false;    // 예측값 입력 프롬프트 출력을 한 번만 하도록
// 이 스코프 안에서만 유효한 플래그 (메시지 중복 전송 방지용)
static bool my_prediction_result_sent_in_this_state = false;

// 예측 결과 저장 변수들 (다음 라운드 결과 계산 시 사용)
static int stored_my_prediction_value = 0;      // 내가 저장한 예측값
static bool has_stored_my_prediction = false;   // 내 예측값이 저장되어 있는지 여부
static int stored_peer_prediction_value = 0;    // 상대방의 예측값
static bool has_stored_peer_prediction = false; // 상대방 예측값 저장 여부

// SDU (input)
static uint8_t sdu[1030];

// serial port interface
static Serial pc(USBTX, USBRX);
static uint8_t myDestId;


// 결과 출력 함수
static void checkAndShowResult()
{
    // SELECTION 상태에서만 결과 계산 및 출력
    if (main_state == L3STATE_SELECTION)
    {
        // 내 선택과 상대방 선택 모두 완료되고 아직 결과가 출력되지 않았다면
        if (my_choice > 0 && peer_choice > 0 && !result_printed)
        {
            pc.printf("\n[RESULT] 당신의 선택: %s\n", (my_choice == 1 ? "협력" : "배신"));
            pc.printf("[RESULT] 상대방의 선택: %s\n", (peer_choice == 1 ? "협력" : "배신"));

            // 내가 한 예측 결과 확인 (이전 라운드에서 예측했다면)
            if (has_stored_my_prediction)
            {
                if (stored_my_prediction_value == peer_choice) // 내 예측이 상대방의 실제 선택과 일치하면
                {
                    sentence *= (2.0f / 3.0f); // 형량 감소
                    pc.printf("\n🎯 예측 성공! 형량이 2/3로 줄어듭니다.\n");
                }
                else // 내 예측이 틀렸다면
                {
                    sentence *= (4.0f / 3.0f); // 형량 증가
                    pc.printf("\n❌ 예측 실패! 형량이 4/3로 늘어납니다.\n");
                }
                has_stored_my_prediction = false; // 예측 결과 사용 완료, 다음 라운드를 위해 초기화
            }

            // 상대방이 한 예측 결과 확인 (상대방이 예측했다면)
            if (has_stored_peer_prediction)
            {
                if (stored_peer_prediction_value == my_choice) // 상대방 예측이 나의 실제 선택과 일치하면
                {
                    pc.printf("\n[INFO] 상대방이 당신의 선택을 정확히 예측했습니다.\n");
                }
                else // 상대방 예측이 틀렸다면
                {
                    pc.printf("\n[INFO] 상대방이 당신의 선택 예측에 실패했습니다.\n");
                }
                has_stored_peer_prediction = false; // 상대방 예측 결과 사용 완료, 다음 라운드를 위해 초기화
            }

            // 기본 딜레마 게임 결과 계산 및 형량 반영
            if (my_choice == 1 && peer_choice == 1)      // 둘 다 협력
                sentence *= 1.0 / 3.0;                   // 크게 감형
            else if (my_choice == 1 && peer_choice == 2) // 나만 협력, 상대 배신
                sentence *= 2.0;                         // 나만 크게 증감
            else if (my_choice == 2 && peer_choice == 1) // 나만 배신, 상대 협력
                sentence *= 0.5;                         // 나만 크게 감형
            else                                         // 둘 다 배신
                sentence *= 1.5;                         // 둘 다 조금 증감

            pc.printf("\n📣 당신의 형량은 %.1f년입니다.\n", sentence);
            result_printed = true; // 결과 출력 완료 플래그 설정;
        }

    }
}

// 사용자 입력 처리 함수
static void L3service_processInputWord(void)
{
    char c = pc.getc(); // 시리얼 포트에서 문자 하나를 읽어옴

    // L3STATE_INITIAL_WAITING 상태 처리: 게임 시작 동의 여부 입력
    if (main_state == L3STATE_INITIAL_WAITING && !ready_to_play)
    {
        if (c == 'Y' || c == 'y')
        {
            ready_to_play = true;                               // 게임 시작 동의
            strcpy((char *)sdu, "READY");                       // "READY" 메시지 생성
            L3_LLI_dataReqFunc(sdu, strlen("READY"), myDestId); // 상대방에게 전송
            pc.printf("[System] 게임 시작을 동의했습니다. 상대방을 기다리는 중...\n");
        }
        else if (c == 'N' || c == 'n')
        {
            pc.printf("[System] 게임을 거절했습니다.\n");
        }
        return; // 입력 처리 후 함수 종료
    }

    // L3STATE_SELECTION 상태 처리: 협력/배신 선택 입력
    if (main_state == L3STATE_SELECTION && my_choice == 0) // 아직 내 선택을 하지 않았다면
    {
        if (c == '1' || c == '2')
        {
            my_choice = (c == '1') ? 1 : 2;                         // 내 선택 저장
            sprintf((char *)sdu, "CHOICE:%d", my_choice);           // 선택 메시지 생성
            L3_LLI_dataReqFunc(sdu, strlen((char *)sdu), myDestId); // 상대방에게 전송
            if (peer_choice == 0)                                   // 상대방 선택이 아직이면 대기 메시지
                pc.printf("\n[System] 선택을 완료했습니다. 상대방을 기다리는 중...\n");
            else // 상대방 선택도 이미 완료되었으면 결과 계산 중 메시지
                pc.printf("\n[System] 결과를 계산 중입니다...\n");
            checkAndShowResult(); // 내 선택 입력 후에도 결과 확인 시도 (상대방 선택이 미리 들어왔을 수도 있으므로)
        }
        else
        {
            pc.printf("\n[System] 잘못된 입력입니다. 1(협력) 또는 2(배신)을 입력하세요.\n");
        }
        return; // 입력 처리 후 함수 종료
    }

    // L3STATE_CHECKING 상태 처리: 예측 게임 참여 Y/N 선택
    // 내가 아직 Y/N 선택을 하지 않았고, 예측 기회를 아직 사용하지 않았다면
    if (main_state == L3STATE_CHECKING && my_prediction_yn_choice == 0 && !my_used_prediction)
    {
        if (c == 'Y' || c == 'y')
        {
            my_prediction_yn_choice = 1;                            // Y (참여)
            strcpy((char *)sdu, "PREDICT_Y");                       // "PREDICT_Y" 메시지 생성
            L3_LLI_dataReqFunc(sdu, strlen("PREDICT_Y"), myDestId); // 상대방에게 전송
            predict_yn_input_done = true;                           // 내 Y/N 입력 완료
            // my_used_prediction은 PREDICTION 상태로 진입할 때 (실제로 예측을 시작할 때) true로 설정
            pc.printf("[System] 예측 게임에 도전합니다.\n");
        }
        else if (c == 'N' || c == 'n')
        {
            my_prediction_yn_choice = 2;                            // N (거절)
            strcpy((char *)sdu, "PREDICT_N");                       // "PREDICT_N" 메시지 생성
            L3_LLI_dataReqFunc(sdu, strlen("PREDICT_N"), myDestId); // 상대방에게 전송
            predict_yn_input_done = true;                           // 내 Y/N 입력 완료
            // 'N'을 선택하면 기회는 소진되지 않고 다음 라운드로 이월됩니다.
            pc.printf("[System] 예측 게임을 거절합니다.\n");
        }
        return; // 입력 처리 후 함수 종료
    }

    // L3STATE_PREDICTION 상태 처리: 예측값 (1/2) 입력
    // 내가 예측 게임에 참여하기로 했을 때만 (my_prediction_yn_choice == 1) 예측값을 받음
    if (main_state == L3STATE_PREDICTION && my_prediction_yn_choice == 1 && !prediction_input_received)
    {
        if (c == '1' || c == '2')
        {
            prediction_value = (c == '1') ? 1 : 2;         // 예측값 저장
            prediction_input_received = true;              // 내 예측값 입력 완료
            stored_my_prediction_value = prediction_value; // 다음 라운드 결과 계산을 위해 예측값 저장
            has_stored_my_prediction = true;               // 내 예측값이 저장되었음을 표시
            pc.printf("[System] 예측을 완료했습니다.\n");
        }
        else
        {
            pc.printf("\n[System] 잘못된 입력입니다. 1(협력) 또는 2(배신)을 입력하세요.\n");
        }
        return; // 입력 처리 후 함수 종료
    }
}

// 라운드 초기화 (다음 라운드 시작 전 호출)
void resetForNextRound()
{
    // SELECTION 상태 변수 초기화
    my_choice = 0;
    peer_choice = 0;
    peer_choice_received = false;
    result_printed = false;
    selection_msg_printed = false; // 다음 라운드에서 선택 프롬프트 다시 출력

    // CHECKING 상태 변수 초기화 (매 라운드 예측 참여 여부 다시 물어볼 수 있도록)
    // my_used_prediction, peer_used_prediction은 여기서 초기화하지 않습니다.
    // 이는 플레이어 당 한 번의 예측 기회를 나타내므로 게임이 끝날 때까지 유지되어야 합니다.
    my_prediction_yn_choice = 0;           // Y/N 선택 초기화
    peer_prediction_yn_choice = 0;         // Y/N 선택 초기화
    predict_yn_input_done = false;         // 내 Y/N 입력 완료 상태 초기화
    peer_prediction_yn_input_done = false; // 상대 Y/N 입력 완료 상태 초기화
    predict_propmt_sent = false;           // 예측 게임 Y/N 프롬프트 다시 출력

    // PREDICTION 상태 변수 초기화
    prediction_value = 0;                    // 내 예측값 초기화
    prediction_input_received = false;       // 내 예측값 입력 완료 상태 초기화
    peer_prediction_result_received = false; // 상대방 예측값 수신 완료 상태 초기화
    prediction_input_prompt_sent = false;    // 예측값 입력 프롬프트 다시 출력
    // my_prediction_result_sent_in_this_state는 L3STATE_PREDICTION 상태 내부에서 초기화됩니다.
    my_prediction_result_sent_in_this_state = false;
    // pc.printf("[DEBUG] resetForNextRound() → 전송 플래그 초기화: %d\n", my_prediction_result_sent_in_this_state);

    if (my_used_prediction)
        my_prediction_yn_choice = 2;

    if (peer_used_prediction)
        peer_prediction_yn_choice = 2;
}

// FSM 초기화
void L3_initFSM(uint8_t destId)
{
    myDestId = destId;                                     // 상대방 ID 설정
    pc.attach(&L3service_processInputWord, Serial::RxIrq); // 시리얼 입력 인터럽트 설정
    pc.printf("Welcome to the dilemma game\n");            // 환영 메시지 출력
}

// FSM 실행 (메인 루프에서 지속적으로 호출)
void L3_FSMrun(void)
{
    // 상태 전이 시 디버그 메시지 출력
    if (prev_state != main_state)
    {
        debug_if(DBGMSG_L3, "\n[L3] State transition from %i to %i\n", prev_state, main_state);
        prev_state = main_state;
    }

    switch (main_state)
    {
    case L3STATE_INITIAL_WAITING: // 게임 시작 대기 상태
    {
        // 게임 시작 프롬프트 (한 번만 출력)
        if (!prompt_sent)
        {
            pc.printf("\n게임을 시작하시겠습니까? (Y/N): \n");
            prompt_sent = true;
        }

        // 메시지 수신 처리 (상대방의 "READY" 메시지)
        if (L3_event_checkEventFlag(L3_event_msgRcvd))
        {
            uint8_t *dataPtr = L3_LLI_getMsgPtr();
            uint8_t size = L3_LLI_getSize();
            uint8_t fromId = L3_LLI_getSrcId();
            uint8_t localCopy[1030]; // 수신 메시지 복사 버퍼
            memcpy(localCopy, dataPtr, size);
            localCopy[size] = '\0';                    // 널 종료 문자 추가
            L3_event_clearEventFlag(L3_event_msgRcvd); // 메시지 수신 플래그 클리어
            // pc.printf("[DEBUG] 메시지 수신: '%s' from ID: %d\n", localCopy, fromId);

            if (strcmp((char *)localCopy, "READY") == 0)
            {
                peer_ready = true; // 상대방이 준비 완료
                pc.printf("[System] 상대방도 게임 시작에 동의했습니다.\n");
            }
        }

        // 나와 상대방 모두 준비되면 SELECTION 상태로 전이
        if (ready_to_play && peer_ready)
        {
            main_state = L3STATE_SELECTION;
        }
        break;
    }

    case L3STATE_SELECTION: // 협력/배신 선택 상태
    {
        uint8_t *dataPtr = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();
        uint8_t localCopy[1030];
        memcpy(localCopy, dataPtr, size);
        localCopy[size] = '\0';

        if (L3_event_checkEventFlag(L3_event_msgRcvd)){
            if (strcmp((char *)localCopy, "GAME_OVER") == 0)
            {
                pc.printf("\n📢 상대방이 형량 1년 이하로 석방되어 게임이 종료되었습니다.\n");
                main_state = L3STATE_GAME_OVER;
                return;
            }
        }

        // 라운드 시작 메시지 출력 (라운드별로 다르게, 한 번만)
        if (!selection_msg_printed)
        {
            round_cnt += 1;     // 라운드 카운트 증가
            if (round_cnt == 1) // 첫 라운드 상세 안내
            {
                pc.printf("\n✅ 양쪽 모두 게임 시작에 동의했습니다. 게임을 시작합니다.\n");
                pc.printf("\n----------------------------------------------------------------\n");
                pc.printf("<배신의 방 – 시험 기간의 죄>\n\n");
                pc.printf("당신은 의식을 잃은 채 어딘가로 끌려왔고, 눈을 떴을 땐 낯선 방 안에 있었습니다.\n");
                pc.printf("맞은편엔 또 한 명의 낯선 인물이 앉아 있습니다. 당신은 그 사람을 전혀 알지 못합니다.\n");
                pc.printf("그리고 곧, 천장에서 들려오는 냉정한 기계음이 공간을 울립니다.\n");
                pc.printf("----------------------------------------------------------------\n");
                pc.printf("[ System Message ]\n");
                pc.printf(" 🧠 당신은 '시험 기간에 과제를 준 죄'로 구속되었습니다. 현재 형량은 %.1f년입니다.\n", sentence); // 초기 형량 표시
                pc.printf(" \"지금부터 '배신의 방' 게임을 시작합니다.\"\n");
                pc.printf(" \"두 사람 모두 선택을 내려야 합니다. 선택지는 다음 두 가지입니다.\"\n");
                pc.printf("\n----------------------------------------------------------------\n");
                pc.printf("  1. 협력\n");
                pc.printf("\t두 사람이 모두 협력을 선택할 경우, 각자 형량의 1/3만큼 감형됩니다.\n");
                pc.printf("  2. 배신\n");
                pc.printf("\t한 사람이 협력, 다른 한 사람이 배신을 선택할 경우,\n");
                pc.printf("\t  - 협력을 고른 사람은 형량이 2배가 됩니다.\n");
                pc.printf("\t  - 배신을 고른 사람은 형량의 1/2만큼 감형됩니다.\n");
                pc.printf("\t모든 사람이 배신을 선택할 경우,\n");
                pc.printf("\t  - 두 사람 모두 형량이 3/2배가 됩니다.\n");
                pc.printf("----------------------------------------------------------------\n");
            }
            else // 2라운드 이후 간략 안내
            {
                pc.printf("----------------------------------------------------------------\n");
                pc.printf("[ System Message ]\n");
                pc.printf("현재 형량은 %.1f년입니다.\n", sentence);
                pc.printf("1(협력) 또는 2(배신)을 선택해주세요\n");
                pc.printf("----------------------------------------------------------------\n");
                pc.printf("  1. 협력\n");
                pc.printf("\t두 사람이 모두 협력을 선택할 경우, 각자 형량의 1/3만큼 감형됩니다.\n");
                pc.printf("  2. 배신\n");
                pc.printf("\t한 사람이 협력, 다른 한 사람이 배신을 선택할 경우,\n");
                pc.printf("\t  - 협력을 고른 사람은 형량이 2배가 됩니다.\n");
                pc.printf("\t  - 배신을 고른 사람은 형량의 1/2만큼 감형됩니다.\n");
                pc.printf("\t모든 사람이 배신을 선택할 경우,\n");
                pc.printf("\t  - 두 사람 모두 형량이 3/2배가 됩니다.\n");
                pc.printf("----------------------------------------------------------------\n");
            }
            selection_msg_printed = true;
        }

        // 메시지 수신 처리 (상대방의 선택 또는 예측값)
        if (L3_event_checkEventFlag(L3_event_msgRcvd))
        {
            L3_event_clearEventFlag(L3_event_msgRcvd);

            if (strncmp((char *)localCopy, "CHOICE:", 7) == 0) // 상대방의 선택 메시지
            {
                peer_choice = atoi((char *)localCopy + 7);
                pc.printf("\n[System] 상대방이 선택을 완료했습니다.\n");
                if (peer_choice == 1 || peer_choice == 2)
                {
                    peer_choice_received = true;
                }
            }
            // PREDICTION 상태에서 상대방이 보낸 예측값을 SELECTION 상태에서 수신할 수 있음
            // 이 메시지는 다음 라운드 결과 계산 시 사용됨
            else if (strncmp((char *)localCopy, "PREDICTION:", 11) == 0)
            {
                stored_peer_prediction_value = atoi((char *)localCopy + 11); // 상대방 예측값 저장
                has_stored_peer_prediction = true;                           // 상대방 예측값 저장 플래그 설정
                pc.printf("\n[System] 상대방의 예측값을 수신했습니다.\n");
            }
        }

        // 내 선택과 상대방 선택 모두 완료 시 CHECKING 상태로 전이
        if (my_choice > 0 && peer_choice > 0)
        {
            checkAndShowResult();          // 현재 라운드 결과 계산 및 출력 (이때 저장된 예측 결과도 반영)
            main_state = L3STATE_CHECKING; // 예측 게임 참여 여부 확인 상태로 전이
        }
        break;
    }

    case L3STATE_CHECKING: // 예측 게임 참여 여부 확인 및 게임 결과 계산 상태
    {   

        uint8_t *dataPtr = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();
        uint8_t localCopy[1030];
        memcpy(localCopy, dataPtr, size);
        localCopy[size] = '\0';

        // 1. 게임 종료 조건 검사 (형량이 1년 미만이면 게임 종료)
        if (sentence < 1.0f)
        {
            pc.printf("\n🎉 당신은 형량이 1년 이하가 되어 석방되었습니다! 게임에서 승리했습니다!\n");
            strcpy((char *)sdu, "GAME_OVER");
            L3_LLI_dataReqFunc(sdu, strlen("GAME_OVER"), myDestId);
            main_state = L3STATE_GAME_OVER;
            return; // 게임 종료 시 더 이상 진행하지 않음
        }

        if (L3_event_checkEventFlag(L3_event_msgRcvd)){
            if (strcmp((char *)localCopy, "GAME_OVER") == 0)
            {
                pc.printf("\n📢 상대방이 형량 1년 이하로 석방되어 게임이 종료되었습니다.\n");
                main_state = L3STATE_GAME_OVER;
                return;
            }
        }

        // 2. 예측 게임 진행 여부 프롬프트 및 메시지 수신 처리
        // 나 또는 상대방 중 한 명이라도 아직 예측 게임 기회를 사용하지 않았다면 상호작용 진행
        if (!my_used_prediction || !peer_used_prediction)
        {
            if (!predict_propmt_sent) // 프롬프트가 아직 출력되지 않았다면
            {
                // 내가 예측 기회를 아직 사용하지 않았다면 나에게 질문
                if (!my_used_prediction)
                {
                    pc.printf("----------------------------------------------------------------\n");
                    pc.printf("\n[System] 추가 미션이 도착했습니다.\n");
                    pc.printf(" \"당신은 상대방의 다음 선택을 예측할 기회가 1회 주어졌습니다.\"\n");
                    pc.printf(" \"예측에 성공할 경우 형량의 1/3만큼 감형됩니다.\n");
                    pc.printf("\t하지만 예측에 실패할 경우, 오히려 형량이 4/3배가 됩니다.\"\n");
                    pc.printf(" \"예측은 단 한 번만 가능합니다. 진행하시겠습니까(Y/N)?: \"\n");
                    pc.printf("----------------------------------------------------------------\n");
                }
                else
                { // 내가 이미 기회를 사용했고 상대방이 아직 사용하지 않았다면 (상대방의 진행을 기다림)
                    pc.printf("\n[System] 당신은 예측 게임 기회를 이미 사용했습니다. 상대방을 기다리는 중...\n");
                }
                predict_propmt_sent = true; // 프롬프트 출력 완료 플래그 설정
            }

            // 메시지 수신 처리 (상대방의 예측 게임 Y/N 메시지)
            if (L3_event_checkEventFlag(L3_event_msgRcvd))
            {
                L3_event_clearEventFlag(L3_event_msgRcvd);

                if (strcmp((char *)localCopy, "PREDICT_Y") == 0) // 상대방이 Y를 선택
                {
                    peer_prediction_yn_choice = 1;        // Y로 설정
                    peer_prediction_yn_input_done = true; // 상대방 Y/N 입력 완료
                    peer_used_prediction = true;          // 상대방이 예측 기회를 사용했으므로 플래그 업데이트
                    pc.printf("\n[System] 상대방이 예측 게임에 도전합니다.\n");
                }
                else if (strcmp((char *)localCopy, "PREDICT_N") == 0) // 상대방이 N을 선택
                {
                    peer_prediction_yn_choice = 2;        // N으로 설정
                    peer_prediction_yn_input_done = true; // 상대방 Y/N 입력 완료
                    // 'N'을 선택했으므로 peer_used_prediction은 true로 설정하지 않음. 기회는 유지됨.
                    pc.printf("\n[System] 상대방이 예측 게임을 거절했습니다.\n");
                }
            }

            // 나와 상대방의 예측 게임 Y/N 선택이 모두 완료되었을 때
            // 나의 입력이 있었거나, 이미 예측 기회를 사용해서 더 이상 입력이 필요 없는 경우
            bool my_yn_decided = predict_yn_input_done || my_used_prediction;
            // 상대방의 메시지를 수신했거나, 상대방이 이미 예측 기회를 사용해서 더 이상 메시지가 필요 없는 경우
            bool peer_yn_decided = peer_prediction_yn_input_done || peer_used_prediction;

            if (my_yn_decided && peer_yn_decided)
            {
                // 예측 게임 진행 여부에 따라 다음 상태로 전이
                if (my_prediction_yn_choice == 1 || peer_prediction_yn_choice == 1) // 나 또는 상대방 중 한 명이라도 Y를 선택했다면
                {
                    pc.printf("\n[System] 예측 게임 상호작용을 시작합니다...\n");
                    // 내가 'Y'를 선택했다면 이제 나의 예측 기회를 사용한 것으로 표시
                    if (my_prediction_yn_choice == 1)
                    {
                        my_used_prediction = true;
                    }
                    main_state = L3STATE_PREDICTION; // 예측값 입력/대기 상태로 전이
                }
                else // 둘 다 N을 선택했거나, 한쪽이 이미 기회를 사용해서 더 이상 선택할 필요가 없는 경우
                {
                    pc.printf("\n[System] 예측 게임을 건너뜁니다. 다음 라운드를 시작합니다.\n");
                    resetForNextRound();            // 다음 라운드를 위한 모든 변수 초기화
                    main_state = L3STATE_SELECTION; // 다음 선택 게임으로 즉시 전이
                }
            }
        }
        else // 나도 상대방도 모두 예측 게임 기회를 사용했다면 (더 이상 예측 게임 없음)
        {
            pc.printf("\n[System] 예측 게임 기회를 모두 사용했습니다. 다음 라운드를 시작합니다.\n");
            resetForNextRound();            // 다음 라운드를 위한 모든 변수 초기화
            main_state = L3STATE_SELECTION; // 다음 선택 게임으로 즉시 전이
        }
        break;
    }

    case L3STATE_PREDICTION: // 예측값 입력 및 대기 상태
    {
        uint8_t *dataPtr = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();
        uint8_t localCopy[1030];
        memcpy(localCopy, dataPtr, size);
        localCopy[size] = '\0';
        
        if (L3_event_checkEventFlag(L3_event_msgRcvd)){
            if (strcmp((char *)localCopy, "GAME_OVER") == 0)
            {
                pc.printf("\n📢 상대방이 형량 1년 이하로 석방되어 게임이 종료되었습니다.\n");
                main_state = L3STATE_GAME_OVER;
                return;
            }
        }

        // 예측값 입력 프롬프트 출력 (나의 Y/N 선택에 따라 다르게, 한 번만)
        if (my_prediction_yn_choice == 1 && !prediction_input_prompt_sent) // 내가 'Y'를 선택했고 아직 프롬프트가 안 나왔다면
        {
            pc.printf("----------------------------------------------------------------\n");
            pc.printf("[System] 당신은 이제 상대방의 다음 선택을 예측해야 합니다.\n");
            pc.printf("  \"상대방은 협력을 선택할까요? 아니면 배신을 선택할까요?\"\n");
            pc.printf("  1. 협력   2. 배신\n");
            pc.printf("----------------------------------------------------------------\n");
            prediction_input_prompt_sent = true; // 프롬프트 출력 완료
        }
        else if (my_prediction_yn_choice == 2 && !prediction_input_prompt_sent) // 내가 'N'을 선택했다면 상대방을 기다림
        {
            pc.printf("----------------------------------------------------------------\n");
            pc.printf("[System] 상대방이 예측 게임에 참여 중입니다. 잠시 기다려주세요...\n");
            pc.printf("----------------------------------------------------------------\n");
            prediction_input_prompt_sent = true; // 프롬프트 출력 완료
        }

        // 내가 예측 게임에 'Y'를 선택하고 내 입력이 완료되었다면 상대방에게 내 예측값을 전송
        if (my_prediction_yn_choice == 1 && prediction_input_received && !my_prediction_result_sent_in_this_state)
        {
            // pc.printf("[DEBUG] 입력 완료: %d, 전송됨: %d\n", prediction_input_received, my_prediction_result_sent_in_this_state);

            sprintf((char *)sdu, "PREDICTION:%d", prediction_value); // 내 예측값 메시지 생성
            L3_LLI_dataReqFunc(sdu, strlen((char *)sdu), myDestId);  // 상대방에게 전송
            pc.printf("[System] 나의 예측값을 상대방에게 전송했습니다.\n");
            my_prediction_result_sent_in_this_state = true; // 전송 완료 플래그 설정
        }

        // 메시지 수신 처리 (상대방의 예측값)
        if (L3_event_checkEventFlag(L3_event_msgRcvd))
        {
            L3_event_clearEventFlag(L3_event_msgRcvd);

            if (strncmp((char *)localCopy, "PREDICTION:", 11) == 0) // 상대방이 예측값을 보냈다면
            {
                stored_peer_prediction_value = atoi((char *)localCopy + 11); // 상대방 예측값 저장
                has_stored_peer_prediction = true;                           // 상대방 예측값 저장 플래그 설정
                peer_prediction_result_received = true;                      // 상대방 예측값 수신 완료 플래그 설정
                pc.printf("\n[System] 상대방의 예측을 수신했습니다.\n");
            }
        }

        // 예측 사이클 완료 조건 확인:
        // 1. 내가 예측에 'Y'를 선택했으면 내 입력(prediction_input_received)과 전송(my_prediction_result_sent_in_this_state)이 완료되어야 하고,
        //    내가 'N'을 선택했으면 내 역할은 이미 완료된 것으로 간주 (my_prediction_yn_choice == 2)
        bool my_part_of_prediction_done = (my_prediction_yn_choice == 1 && prediction_input_received && my_prediction_result_sent_in_this_state) || (my_prediction_yn_choice == 2);

        // 2. 상대방이 예측에 'Y'를 선택했으면 상대방의 예측값 수신(peer_prediction_result_received)이 완료되어야 하고,
        //    상대방이 'N'을 선택했으면 상대방의 역할은 이미 완료된 것으로 간주 (peer_prediction_yn_choice == 2)
        bool peer_part_of_prediction_done = (peer_prediction_yn_choice == 1 && peer_prediction_result_received) || (peer_prediction_yn_choice == 2);

        if (my_part_of_prediction_done && peer_part_of_prediction_done) // 나와 상대방 모두 예측 관련 상호작용 완료
        {
            pc.printf("[System] 예측 게임이 완료되었습니다. 다음 라운드로 진행합니다.\n");
            resetForNextRound();                             // 다음 라운드를 위한 모든 변수 초기화
            main_state = L3STATE_SELECTION;                  // 다음 선택 게임으로 전이
            prediction_input_prompt_sent = false;            // 예측값 입력 프롬프트 플래그 재설정
            my_prediction_result_sent_in_this_state = false; // 예측값 전송 플래그 재설정
        }
        break;
    }

    case L3STATE_GAME_OVER: // 게임 종료 상태
        // 아무것도 안 함 (게임이 끝났으므로)
        break;

    default: // 알 수 없는 상태
        break;
    }
}