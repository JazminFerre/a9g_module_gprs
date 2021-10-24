
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"


#include "api_os.h"
#include "api_debug.h"
#include "api_event.h"
#include "api_sms.h"
#include "api_hal_uart.h"

/////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////configuration//////////////////////////////////////////////////
#define PHONE_NUMBER "+541130166340"
#define MAIN_TASK_STACK_SIZE    (2048 * 2)
#define MAIN_TASK_PRIORITY      0
#define MAIN_TASK_NAME          "SMS Test Task"

static HANDLE mainTaskHandle = NULL;
static uint8_t flag = 0;
#define FIXED_SMS_NOTIFICATION "Uhola jaz"
void SMSInit()
{
    if (!SMS_SetFormat(SMS_FORMAT_TEXT, SIM0))
    {
        Trace(1, "sms set format error");
        return;
    }
    SMS_Parameter_t smsParam = {
        .fo = 17,
        .vp = 167,
        .pid = 0,
        .dcs = 8, //0:English 7bit, 4:English 8 bit, 8:Unicode 2 Bytes
    };
    if (!SMS_SetParameter(&smsParam, SIM0))
    {
        Trace(1, "sms set parameter error");
        return;
    }
    if (!SMS_SetNewMessageStorage(SMS_STORAGE_SIM_CARD))
    {
        Trace(1, "sms set message storage fail");
        return;
    }
}

void UartInit()
{
    UART_Config_t config = {
        .baudRate = UART_BAUD_RATE_115200,
        .dataBits = UART_DATA_BITS_8,
        .stopBits = UART_STOP_BITS_1,
        .parity = UART_PARITY_NONE,
        .rxCallback = NULL,
    };
    UART_Init(UART1, config);
}


void Init()
{
    UartInit();
    SMSInit();
}

/*
void SendSmsUnicode()
{
    Trace(1,"sms start send unicode message");
    if(!SMS_SendMessage(TEST_PHONE_NUMBER,unicodeMsg,sizeof(unicodeMsg),SIM0))
    {
        Trace(1,"sms send message fail");
    }
}

void SendSmsGbk()
{
    uint8_t* unicode = NULL;
    uint32_t unicodeLen;

    Trace(1,"sms start send GBK message");

    if(!SMS_LocalLanguage2Unicode(gbkMsg,sizeof(gbkMsg),CHARSET_CP936,&unicode,&unicodeLen))
    {
        Trace(1,"local to unicode fail!");
        return;
    }
    if(!SMS_SendMessage(TEST_PHONE_NUMBER,unicode,unicodeLen,SIM0))
    {
        Trace(1,"sms send message fail");
    }
    OS_Free(unicode);
}

void SendUtf8()
{
    uint8_t* unicode = NULL;
    uint32_t unicodeLen;

    Trace(1,"sms start send UTF-8 message");
    Trace(1,"hola jaz 3  %s", TEST_PHONE_NUMBER);
    if(!SMS_LocalLanguage2Unicode(utf8Msg,strlen(utf8Msg),CHARSET_UTF_8,&unicode,&unicodeLen))
    {
        Trace(1,"local to unicode fail!");
        Trace(1,"hola jaz4");
        return;
    }
    Trace(1,"hola jaz z %s", unicode);
    if(!SMS_SendMessage(TEST_PHONE_NUMBER,unicode,unicodeLen,SIM0))
    {
        Trace(1,"sms send message fail");
        Trace(1,"hola jaz 5");
    }
    OS_Free(unicode);
}
*/

void SendSMS(uint8_t message[])
{
    uint8_t *unicode = NULL;
    uint32_t unicodeLen;
    Trace(1, "sms start send UTF-8 message");
    if (!SMS_LocalLanguage2Unicode(message, strlen(message), CHARSET_UTF_8, &unicode, &unicodeLen))
    {
        Trace(1, "local to unicode fail!");
        return;
    }
    if (!SMS_SendMessage(PHONE_NUMBER, unicode, unicodeLen, SIM0))
    {
        Trace(1, "sms send message fail");
    }
    OS_Free(unicode);
}

/*
void ServerCenterTest()
{
    uint8_t addr[32];
    uint8_t temp;
    SMS_Server_Center_Info_t sca;
    sca.addr = addr;
    SMS_GetServerCenterInfo(&sca);
    Trace(1,"server center address:%s,type:%d",sca.addr,sca.addrType);
    temp = sca.addr[strlen(sca.addr)-1];
    sca.addr[strlen(sca.addr)-1] = '0';
    if(!SMS_SetServerCenterInfo(&sca))
        Trace(1,"SMS_SetServerCenterInfo fail");
    else
        Trace(1,"SMS_SetServerCenterInfo success");
    SMS_GetServerCenterInfo(&sca);
    Trace(1,"server center address:%s,type:%d",sca.addr,sca.addrType);
    sca.addr[strlen(sca.addr)-1] = temp;
    if(!SMS_SetServerCenterInfo(&sca))
        Trace(1,"SMS_SetServerCenterInfo fail");
    else
        Trace(1,"SMS_SetServerCenterInfo success");
}
*/
void EventDispatch(API_Event_t* pEvent)
{
    switch(pEvent->id)
    {
        case API_EVENT_ID_NO_SIMCARD:
    {
        Trace(10, "!!!!NO SIM CARD!!!!");
        break;
    }
    case API_EVENT_ID_SYSTEM_READY:
    {
        Trace(1, "system initialize complete");
        flag |= 1;
        break;
    }
    case API_EVENT_ID_NETWORK_REGISTERED_HOME:
    case API_EVENT_ID_NETWORK_REGISTERED_ROAMING:
    {
        Trace(2, "network register success");
        flag |= 2;
        break;
    }
    case API_EVENT_ID_SMS_SENT:
    {
        Trace(2, "Send Message Success");
        break;
    }
    case API_EVENT_ID_SMS_RECEIVED:
    {
        Trace(2, "SMS received");
    }
    case API_EVENT_ID_SMS_LIST_MESSAGE:
    {
        break;
    }
    case API_EVENT_ID_SMS_ERROR:
    {
        Trace(10, "SMS error occured! cause:%d", pEvent->param1);
        break;
    }
    case API_EVENT_ID_GPS_UART_RECEIVED:
    {
        Trace(1, "received GPS data,length:%d, data:%s,flag:%d", pEvent->param1, pEvent->pParam1, flag);
        break;
    }
    default:
        break;
    }

    //system initialize complete and network register complete, now can send message
    // If GPRS and System initialized OK.
    if (flag == 3)
    {
        SMS_Storage_Info_t storageInfo;
        Init();
        Trace(1, "Ready and able hola jaz");
        SendSMS(FIXED_SMS_NOTIFICATION);
        /*ServerCenterTest();*/
        SMS_GetStorageInfo(&storageInfo, SMS_STORAGE_SIM_CARD);
        Trace(1, "sms storage sim card info, used:%d,total:%d", storageInfo.used, storageInfo.total);
        SMS_GetStorageInfo(&storageInfo, SMS_STORAGE_FLASH);
        Trace(1, "sms storage flash info, used:%d,total:%d", storageInfo.used, storageInfo.total);
        if (!SMS_DeleteMessage(5, SMS_STATUS_ALL, SMS_STORAGE_SIM_CARD)) Trace(1, "delete sms fail");
        else Trace(1, "delete sms success");
        SMS_ListMessageRequst(SMS_STATUS_ALL, SMS_STORAGE_SIM_CARD);
        flag = 0;
    }
}



void SMSTest(void* pData)
{
    API_Event_t* event=NULL;
    while(1)
    {
        if(OS_WaitEvent(mainTaskHandle, (void**)&event, OS_TIME_OUT_WAIT_FOREVER))
        {
            EventDispatch(event);
            OS_Free(event->pParam1);
            OS_Free(event->pParam2);
            OS_Free(event);
        }
    }
}

void sms_Main()
{
    mainTaskHandle = OS_CreateTask(SMSTest,
        NULL, NULL, MAIN_TASK_STACK_SIZE, MAIN_TASK_PRIORITY, 0, 0, MAIN_TASK_NAME);
    OS_SetUserMainHandle(&mainTaskHandle);
}

