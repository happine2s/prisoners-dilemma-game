#include "L3_FSMevent.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "L3_LLinterface.h"
#include "protocol_parameters.h"
#include "mbed.h"

// FSM state -------------------------------------------------
#define L3STATE_INITIAL_WAITING 0
#define L3STATE_SELECTION 1
#define L3STATE_CHECKING 2
#define L3STATE_PREDICTION 3

// state variables
static uint8_t main_state = L3STATE_INITIAL_WAITING;
static uint8_t prev_state = main_state;

// 상태별 메시지 출력 여부를 저장할 플래그
static bool selection_msg_printed = false;

// 사용자 및 상대방의 게임 동의 상태
static bool ready_to_play = false;
static bool peer_ready = false;
static bool prompt_sent = false; // Y/N 질문을 이미 보냈는지 여부

// SDU (input)
static uint8_t originalWord[1030];
static uint8_t wordLen = 0;
static uint8_t sdu[1030];

// serial port interface
static Serial pc(USBTX, USBRX);
static uint8_t myDestId;

// 사용자 입력 처리
static void L3service_processInputWord(void)
{
    char c = pc.getc();

    // ✅ INITIAL_WAITING 상태에서 Y/N 입력 받기
    if (main_state == L3STATE_INITIAL_WAITING && !ready_to_play)
    {
        if (c == 'Y' || c == 'y')
        {
            ready_to_play = true;
            const char *msg = "READY";
            strcpy((char *)sdu, msg);
            L3_LLI_dataReqFunc(sdu, strlen(msg), myDestId);
            pc.printf("[System] 게임 시작을 동의했습니다. 상대방을 기다리는 중...\n");
        }
        else if (c == 'N' || c == 'n')
        {
            pc.printf("[System] 게임을 거절했습니다. 프로그램을 종료하거나 다시 시작하세요.\n");
        }
        return;
    }

    // 이후 상태에서의 일반 메시지 입력 처리
    if (!L3_event_checkEventFlag(L3_event_dataToSend))
    {
        if (c == '\n' || c == '\r')
        {
            originalWord[wordLen++] = '\0';
            L3_event_setEventFlag(L3_event_dataToSend);
        }
        else
        {
            originalWord[wordLen++] = c;
            if (wordLen >= L3_MAXDATASIZE - 1)
            {
                originalWord[wordLen++] = '\0';
                L3_event_setEventFlag(L3_event_dataToSend);
                pc.printf("\n max reached! word forced to be ready :::: %s\n", originalWord);
            }
        }
    }
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
        debug_if(DBGMSG_L3, "[L3] State transition from %i to %i\n", prev_state, main_state);
        prev_state = main_state;
    }

    switch (main_state)
    {
    case L3STATE_INITIAL_WAITING:
    {
        // ✅ Y/N 질문 출력 (한 번만)
        if (!prompt_sent)
        {
            pc.printf("게임을 시작하시겠습니까? (Y/N): ");
            prompt_sent = true;
        }

        // ✅ 상대방 메시지 수신 처리
        if (L3_event_checkEventFlag(L3_event_msgRcvd))
        {
            uint8_t *dataPtr = L3_LLI_getMsgPtr();
            uint8_t size = L3_LLI_getSize();

            uint8_t localCopy[1030];
            memcpy(localCopy, dataPtr, size);
            localCopy[size] = '\0';

            L3_event_clearEventFlag(L3_event_msgRcvd);

            debug("[L3] 수신 메시지: %s\n", localCopy);

            if (strcmp((char *)localCopy, "READY") == 0)
            {
                peer_ready = true;
                pc.printf("[System] 상대방도 게임 시작에 동의했습니다.\n");
            }
        }

        // ✅ 양쪽 모두 준비 완료 → 상태 전이
        if (ready_to_play && peer_ready)
        {
            pc.printf("✅ 양쪽 모두 게임 시작에 동의했습니다. 게임을 시작합니다.\n");
            main_state = L3STATE_SELECTION;
        }

        break;
    }

    case L3STATE_SELECTION:
    {
        if (!selection_msg_printed)
        {
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

            selection_msg_printed = true;
        }
        break;
    }

    case L3STATE_CHECKING:
    {
        debug("[L3] CHECKING state\n");
        main_state = L3STATE_PREDICTION;
        break;
    }

    case L3STATE_PREDICTION:
    {
        debug("[L3] PREDICTION state\n");
        main_state = L3STATE_INITIAL_WAITING;
        break;
    }

    default:
        break;
    }
}