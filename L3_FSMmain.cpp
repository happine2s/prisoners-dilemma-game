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

// state variables
static uint8_t main_state = L3STATE_INITIAL_WAITING;
static uint8_t prev_state = main_state;

// 라운드 변수
static int round_cnt = 0;

// 상호변형 메시지 출력 유물 플래그
static bool selection_msg_printed = false;
static bool ready_to_play = false;
static bool peer_ready = false;
static bool prompt_sent = false;

// SELECTION 상태 변수
static int my_choice = 0;
static int peer_choice = 0;
static bool peer_choice_received = false;
static bool result_printed = false;

static float sentence = 10.0;

// CHECKING 상태 변수
static bool used_prediction = false;            // 내 예측게임 참여 유물
static int my_prediction = 0;                   // 내 예측게임 Y/N
static int peer_prediction = 0;                 // 상대 예측게임 Y/N
static bool predict_input_done = false;         // 내 입력 완료 유물
static bool peer_prediction_input_done = false; // 상대 입력 완료 유물
// static bool predict_prompt_sent = false;        // 예측 관련 메시지 출력에 사용

// PREDICTION 상태 변수
static bool prediction_input_received = false;
static int prediction = 0;

// SDU (input)
static uint8_t sdu[1030];

// serial port interface
static Serial pc(USBTX, USBRX);
static uint8_t myDestId;

// 결과 출력 함수
static void checkAndShowResult()
{
    if (main_state == L3STATE_SELECTION)
    {
        if (my_choice > 0 && peer_choice > 0 && !result_printed)
        {
            pc.printf("\n[RESULT] 당신의 선택: %s\n", (my_choice == 1 ? "협력" : "배신"));
            pc.printf("[RESULT] 상대방의 선택: %s\n", (peer_choice == 1 ? "협력" : "배신"));

            if (my_choice == 1 && peer_choice == 1)
                sentence *= 1.0 / 3.0;
            else if (my_choice == 1 && peer_choice == 2)
                sentence *= 2.0;
            else if (my_choice == 2 && peer_choice == 1)
                sentence *= 0.5;
            else
                sentence *= 1.5;

            pc.printf("\n📣 당신의 형량은 %.1f년입니다.\n", sentence);
            result_printed = true;
        }
    }
    else if (main_state == L3STATE_PREDICTION)
    {
        if (prediction == peer_choice)
        {
            sentence *= (2.0f / 3.0f);
            pc.printf("\n🎯 예측 성공! 형량이 2/3로 줄어들립니다.\n");
        }
        else
        {
            sentence *= (4.0f / 3.0f);
            pc.printf("\n❌ 예측 실패! 형량이 4/3로 늘어날 것입니다.\n");
        }
        pc.printf("\n📣 현재 형량: %.1f년\n", sentence);
    }
}

// 사용자 입력 함수
static void L3service_processInputWord(void)
{
    char c = pc.getc();
    if (main_state == L3STATE_INITIAL_WAITING && !ready_to_play)
    {
        if (c == 'Y' || c == 'y')
        {
            ready_to_play = true;
            strcpy((char *)sdu, "READY");
            L3_LLI_dataReqFunc(sdu, strlen("READY"), myDestId);
            pc.printf("[System] 게임 시작을 동의했습니다. 상대방을 기다리는 중...\n");
        }
        else if (c == 'N' || c == 'n')
        {
            pc.printf("[System] 게임을 거절했습니다.\n");
        }
        return;
    }

    if (main_state == L3STATE_SELECTION && my_choice == 0)
    {
        if (c == '1' || c == '2')
        {
            my_choice = (c == '1') ? 1 : 2;
            sprintf((char *)sdu, "CHOICE:%d", my_choice);
            L3_LLI_dataReqFunc(sdu, strlen((char *)sdu), myDestId);
            if (peer_choice == 0)
                pc.printf("\n[System] 선택을 완료했습니다. 상대방을 기다리는 중...\n");
            else
                pc.printf("\n[System] 결과를 계산 중입니다...\n");
            checkAndShowResult();
        }
        else
        {
            pc.printf("\n[System] 잘못된 입력입니다. 1(협력) 또는 2(배신)을 입력하세요.\n");
        }
        return;
    }

    if (main_state == L3STATE_CHECKING && my_prediction == 0)
    {
        if (c == 'Y' || c == 'y')
        {
            my_prediction = true;
            strcmp((char *)sdu, "PREDICT_Y");
            L3_LLI_dataReqFunc(sdu, strlen("PREDICT_Y"), myDestId);
            predict_input_done = true;
        }
        else if (c == 'N' || c == 'n')
        {
            my_prediction = false;
            strcmp((char *)sdu, "PREDICT_N");
            L3_LLI_dataReqFunc(sdu, strlen("PREDICT_N"), myDestId);
            predict_input_done = true;
        }
    }

    if (main_state == L3STATE_PREDICTION)
    {
        if (c == '1' || c == '2')
        {
            prediction = (c == '1') ? 1 : 2;
            prediction_input_received = true;
            checkAndShowResult();
        }
        else
        {
            pc.printf("[System] 잘못된 입력입니다. 1(협력) 또는 2(배신)을 입력하세요.\n");
        }
    }
}

// 라운드 초기화
void resetForNextRound()
{
    my_choice = 0;
    peer_choice = 0;
    peer_choice_received = false;
    result_printed = false;
    selection_msg_printed = false;

    my_prediction = false;
    peer_prediction = false;
    predict_input_done = false;
    peer_prediction_input_done = false;
    prediction_input_received = false;
    prediction = 0;
}

// 초기화
void L3_initFSM(uint8_t destId)
{
    myDestId = destId;
    pc.attach(&L3service_processInputWord, Serial::RxIrq);
    pc.printf("Welcome to the dilemma game\n");
}

// 상태 머신 실행
void L3_FSMrun(void)
{
    if (prev_state != main_state)
    {
        debug_if(DBGMSG_L3, "\n[L3] State transition from %i to %i\n", prev_state, main_state);
        prev_state = main_state;
    }

    switch (main_state)
    {
    case L3STATE_INITIAL_WAITING:
    {
        if (!prompt_sent)
        {
            pc.printf("\n게임을 시작하시겠습니까? (Y/N): \n");
            prompt_sent = true;
        }

        if (L3_event_checkEventFlag(L3_event_msgRcvd))
        {
            uint8_t *dataPtr = L3_LLI_getMsgPtr();
            uint8_t size = L3_LLI_getSize();
            uint8_t fromId = L3_LLI_getSrcId();
            uint8_t localCopy[1030];
            memcpy(localCopy, dataPtr, size);
            localCopy[size] = '\0';
            L3_event_clearEventFlag(L3_event_msgRcvd);
            pc.printf("[DEBUG] 메시지 수신: '%s' from ID: %d\n", localCopy, fromId);
            if (strcmp((char *)localCopy, "READY") == 0)
            {
                peer_ready = true;
                pc.printf("[System] 상대방도 게임 시작에 동의했습니다.\n");
            }
        }

        if (ready_to_play && peer_ready)
        {
            main_state = L3STATE_SELECTION; // 상태 전이
        }
        break;
    }

    case L3STATE_SELECTION:
    {
        round_cnt += 1;
        if (!selection_msg_printed)
        {
            if (round_cnt == 1) // 1라운드
            {
                pc.printf("\n✅ 양쪽 모두 게임 시작에 동의했습니다. 게임을 시작합니다.\n");
                pc.printf("\n----------------------------------------------------------------\n");
                pc.printf("<배신의 방 – 시험 기간의 죄>\n\n");
                pc.printf("당신은 의식을 잃은 채 어딘가로 끌려왔고, 눈을 떴을 땐 낯선 방 안에 있었습니다.\n");
                pc.printf("맞은편엔 또 한 명의 낯선 인물이 앉아 있습니다. 당신은 그 사람을 전혀 알지 못합니다.\n");
                pc.printf("그리고 곧, 천장에서 들려오는 냉정한 기계음이 공간을 울립니다.\n");
                pc.printf("----------------------------------------------------------------\n");
                pc.printf("[ System Message ]\n");
                pc.printf(" 🧠 당신은 ‘시험 기간에 과제를 준 죄’로 구속되었습니다. 현재 형량은 10년입니다.\n");
                pc.printf(" \"지금부터 ‘배신의 방’ 게임을 시작합니다.\"\n");
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
            else
            {
                pc.printf("----------------------------------------------------------------\n");
                pc.printf("[ System Message ]\n");
                pc.printf("현재 형량은 %d년입니다.\n", sentence);
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

        if (L3_event_checkEventFlag(L3_event_msgRcvd))
        {
            // 받은 메시지를 안전하게 localCopy로 복사하고 널 종료자로 문자열 처리
            uint8_t *dataPtr = L3_LLI_getMsgPtr();
            uint8_t size = L3_LLI_getSize();
            uint8_t localCopy[1030];
            memcpy(localCopy, dataPtr, size);
            localCopy[size] = '\0';

            // 메시지 수신 이벤트 플래그 초기화 (다음 처리 위해 반드시 필요)
            L3_event_clearEventFlag(L3_event_msgRcvd);
            
            // 메시지가 "CHOICE:"로 시작하는 경우를 필터링
            if (strncmp((char *)localCopy, "CHOICE:", 7) == 0){

                // "CHOICE:" 이후의 숫자 값을 정수로 변환해서 상대방 선택값 peer_choice에 저장.
                peer_choice = atoi((char *)localCopy + 7);
                
                pc.printf("\n[System] 상대방이 선택을 완료했습니다.\n");
                // pc.printf("[DEBUG] 상대방 선택 저장됨: %d\n", peer_choice); // DEBUG
                if (peer_choice == 1 || peer_choice == 2)
                {
                    peer_choice_received = true;
                    // pc.printf("[DEBUG] 상대방 선택 수신: %d\n", peer_choice); // DEBUG
                }
            }
        }
        if (my_choice > 0 && peer_choice > 0 && main_state == L3STATE_SELECTION)
        {
            checkAndShowResult();
            main_state = L3STATE_CHECKING;
        }
        break;
    }

    case L3STATE_CHECKING:
    {
        static bool predict_propmt_sent = false;

        // 1. 게임 종료 조건 검사
        if (sentence < 1.0f)
        {
            pc.printf("\n🎉 당신은 형량이 1년 이하가 되어 석방되었습니다! 게임에서 승리했습니다!\n");
            main_state = L3STATE_INITIAL_WAITING; // 또는 종료 처리 루틴
            return;
        }

        // 2. 예측 게임 진행 여부
        if (!used_prediction)
        {
            if (!predict_propmt_sent)
            { // 예측 게임 규칙 설명
                pc.printf("----------------------------------------------------------------\n");
                pc.printf("\n");
                pc.printf("[System] 추가 미션이 도착했습니다.\n");
                pc.printf(" \"당신은 상대방의 선택을 예측할 기회가 1회 주어졌습니다.\"\n");
                pc.printf(" \"예측에 성공할 경우 형량의 1/3만큼 감형됩니다.\n");
                pc.printf("\t하지만 예측에 실패할 경우, 오히려 형량이 4/3배가 됩니다.\"\n");
                pc.printf(" \"예측은 단 한 번만 가능합니다. 진행하시겠습니까(Y/N)?:  \"\n");
                pc.printf("\n");
                pc.printf("----------------------------------------------------------------\n");

                predict_propmt_sent = true;
            }

            // 사용자 입력 처리
            if (pc.readable())
            {
                L3service_processInputWord();
            }

            // 상대방 선택
            if (L3_event_checkEventFlag(L3_event_msgRcvd))
            {
                uint8_t *dataPtr = L3_LLI_getMsgPtr();
                uint8_t size = L3_LLI_getSize();
                uint8_t localCopy[1030];
                memcpy(localCopy, dataPtr, size);
                localCopy[size] = '\0';
                L3_event_clearEventFlag(L3_event_msgRcvd);

                if (strcmp((char *)localCopy, "PREDICT_Y") == 0)
                {
                    peer_prediction = true;
                    peer_prediction_input_done = true;
                    pc.printf("\n[System] 상대방이 예측 게임에 도전합니다.\n");
                }
                else if (strcmp((char *)localCopy, "PREDICT_N") == 0) // 내 선택도 N이라면 -> 추가
                {
                    peer_prediction = false;
                    peer_prediction_input_done = true;
                    pc.printf("\n[System] 상대방이 예측 게임에 도전합니다.\n");
                }
            }

            // 두 명 모두 입력 완료 -> 분리시켜야 함
            if (predict_input_done && peer_prediction_input_done)
            {
                if (my_prediction || peer_prediction) // 둘 중 한 명이라도 Y를 눌렀다면
                {
                    pc.printf("\n[System] 예측 게임을 시작합니다...\n");
                    used_prediction = true;
                    main_state = L3STATE_PREDICTION; // 상태 전이
                }
                else
                {
                    pc.printf("\n[System] 예측 게임을 건너뜁니다. 다음 라운드를 시작합니다.\n");
                    resetForNextRound();
                    main_state = L3STATE_SELECTION; // 상태 전이
                }
            }
        }
        else // 이미 예측게임 기회를 사용한 경우 -> 플레이어 분리해야 함
        {
            pc.printf("\n[System] 다음 라운드를 시작합니다.\n");
            resetForNextRound();
            main_state = L3STATE_SELECTION; // 상태 전이
        }
        break;
    }

    case L3STATE_PREDICTION:
    {
        static bool prediction_prompt_printed = false;
        if (!prediction_prompt_printed)
        { // 예측 게임 규칙 출력
            pc.printf("----------------------------------------------------------------\n");
            pc.printf("\n");
            pc.printf("[System] 당신은 이제 상대방의 값을 예측해야합니다.\n");
            pc.printf("  \"상대방은 협력을 선택했을까요? 아니면 배신을 선택했을까요?\"\n");
            pc.printf("\n");
            pc.printf("----------------------------------------------------------------\n");
            pc.printf("\n");
            pc.printf("   1. 협력\n");
            pc.printf("   2. 배신\n");
            pc.printf("\n");
            pc.printf("----------------------------------------------------------------\n");

            prediction_prompt_printed = true;
        }
        if (!prediction_input_received)
        {
            if (pc.readable())
            {
                L3service_processInputWord();
            }

            // 상태 초기화
            if (prediction_input_received)
            {
                prediction_prompt_printed = false;
                main_state = L3STATE_CHECKING;
            }
        }
        break;
    }

    default:
        break;
    }
}