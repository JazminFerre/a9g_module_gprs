#include <string.h>
#include <stdio.h>
#include <api_os.h>
#include <api_event.h>
#include <api_socket.h>
#include <api_network.h>
#include <api_debug.h>
#include "api_hal_pm.h"
#include "api_info.h"
#include "api_hal_gpio.h"
#include "api_call.h"
#include "api_audio.h"
#include "demo_call.h"
#include <api_gps.h>
#include <api_hal_uart.h>
#include "buffer.h"
#include "gps_parse.h"
#include "math.h"
#include "gps.h"
#include "api_sms.h"

/*******************************************************************/
#define SERVER_IP   "app-argus-server.herokuapp.com"
#define SERVER_PORT 80
/*******************************************************************/


#define MAIN_TASK_STACK_SIZE    (2048 * 2)
#define MAIN_TASK_PRIORITY      0
#define MAIN_TASK_NAME          "Socket Test Task"

#define TEST_TASK_STACK_SIZE    (2048 * 2)
#define TEST_TASK_PRIORITY      0
#define TEST_TASK_NAME          "Test Task"

static HANDLE socketTaskHandle = NULL;
static HANDLE testTaskHandle = NULL;
static HANDLE semStart = NULL;
bool isGpsOn = true;
bool isDialSuccess = false;
bool isCallComing = false;
bool ackCall = false;
bool makeCall = false;
bool setAlarm1 = false;
bool setAlarm2 = false;
bool networkID = true;
bool isGPSFixed = false;
Network_PDP_Context_t context = { // PERSONAL
                    .apn        ="datos.personal.com",
                    .userName   = "datos",
                    .userPasswd = "datos"
                };
Network_PDP_Context_t context1 = { //CLARO
                    .apn        ="claro.pe",
                    .userName   = "claro",
                    .userPasswd = "claro"
                };
Network_PDP_Context_t context2 = {  // MOVISTAR 
                    .apn        ="wap.gprs.unifon.com.ar",
                    .userName   = "wap",
                    .userPasswd = "wap"
                };
Network_PDP_Context_t context3 = { //TUENTI
                    .apn        ="internet.movil",
                    .userName   = "internet",
                    .userPasswd = "internet"
                };

void EventDispatch(API_Event_t* pEvent)
{   uint8_t dtmf = '0';
    switch(pEvent->id)
    {
        case API_EVENT_ID_NO_SIMCARD:
            Trace(10,"!!NO SIM CARD%d!!!!",pEvent->param1);
            break;
        case API_EVENT_ID_SYSTEM_READY:
            Trace(1,"system initialize complete");
            break;
        case API_EVENT_ID_NETWORK_REGISTER_SEARCHING:
            Trace(2,"network register searching");
            break;
        case API_EVENT_ID_NETWORK_REGISTER_DENIED:
            Trace(2,"network register denied");
            break;
        case API_EVENT_ID_NETWORK_REGISTER_NO:
            Trace(2,"network register no");
            break;
        case API_EVENT_ID_NETWORK_REGISTERED_HOME:
        case API_EVENT_ID_NETWORK_REGISTERED_ROAMING:
        {
            uint8_t status;
            Trace(2,"network register success");
            bool ret = Network_GetAttachStatus(&status);
            if(!ret)
                Trace(1,"get attach staus fail");
            Trace(1,"attach status:%d",status);
            if(status == 0)
            {
                ret = Network_StartAttach();
                if(!ret)
                {
                    Trace(1,"network attach fail");
                }
            }
            else
            {
                Network_StartActive(context);
            }
            break;
        }
        case API_EVENT_ID_NETWORK_ATTACHED:
            Trace(2,"network attach success");
            Network_StartActive(context);
            break;
        case API_EVENT_ID_NETWORK_ACTIVATED:
            Trace(2,"network activate success");
            OS_ReleaseSemaphore(semStart);
            break;
        case API_EVENT_ID_NETWORK_CELL_INFO:
        {
            uint8_t number = pEvent->param1;
            Network_Location_t* location = pEvent->pParam1;
            Trace(2,"http network cell infomation,serving cell number:1, neighbor cell number:%d",number-1);
            
            for(int i=0;i<number;++i)
            {
                Trace(2,"http cell %d info:%d%d%d,%d%d%d,%d,%d,%d,%d,%d,%d",i,
				location[i].sMcc[0], location[i].sMcc[1], location[i].sMcc[2], 
				location[i].sMnc[0], location[i].sMnc[1], location[i].sMnc[2],
				location[i].sLac, location[i].sCellID, location[i].iBsic,
                location[i].iRxLev, location[i].iRxLevSub, location[i].nArfcn);
                Trace(2,"http %d",location[i].sCellID);
            }
            networkID = true;
            break;
        }
        case API_EVENT_ID_GPS_UART_RECEIVED:
            GPS_Update(pEvent->pParam1,pEvent->param1);
            break;
        case API_EVENT_ID_UART_RECEIVED:
            if(pEvent->param1 == UART1)
            {
                uint8_t data[pEvent->param2+1];
                data[pEvent->param2] = 0;
                memcpy(data,pEvent->pParam1,pEvent->param2);
                Trace(1,"uart received data,length:%d,data:%s",pEvent->param2,data);
                if(strcmp(data,"close") == 0)
                {
                    Trace(1,"close gps");
                    GPS_Close();
                    isGpsOn = false;
                }
                else if(strcmp(data,"open") == 0)
                {
                    Trace(1,"open gps");
                    GPS_Open(NULL);
                    isGpsOn = true;
                }
            }
            break;
        case API_EVENT_ID_CALL_INCOMING:   //param1: number type, pParam1:number
            isCallComing = true;
            while(!ackCall && isCallComing)
            {
                Trace(1,"make a DTMF:%c",dtmf);
                CALL_DTMF(dtmf,CALL_DTMF_GAIN_m3dB,5,15,false);
                OS_Sleep(3000);
                dtmf ++;
                if(dtmf == '10') dtmf = '0';
            }
            ackCall = false;
            isCallComing = false;
            break;
        case API_EVENT_ID_CALL_ANSWER:  
            Trace(1,"answer success");
            break;
        default:
            break;
    }
}

int Http_Post(const char* domain, int port,const char* path, char* retBuffer, int* bufferLen)
{
    bool flag = false;
    uint16_t recvLen = 0;
    uint8_t ip[16];
    int retBufferLen = *bufferLen;
    //connect server
    memset(ip,0,sizeof(ip));
    if(DNS_GetHostByName2(domain,ip) != 0)
    {
        Trace(1,"get ip error");
        return -1;
    }
    Trace(1,"get ip success:%s -> %s",domain,ip);
    char* servInetAddr = ip;
    snprintf(retBuffer,retBufferLen,"POST %s HTTP/1.1\r\nHost: %s\r\n\r\n",path,domain);
    char* pData = retBuffer;
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(fd < 0){
        Trace(1,"socket fail");
        return -1;
    }
    Trace(1,"fd:%d",fd);

    struct sockaddr_in sockaddr;
    memset(&sockaddr,0,sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    inet_pton(AF_INET,servInetAddr,&sockaddr.sin_addr);

    int ret = connect(fd, (struct sockaddr*)&sockaddr, sizeof(struct sockaddr_in));
    if(ret < 0){
        Trace(1,"socket connect fail");
        return -1;
    }
    Trace(1,"socket connect success");
    Trace(1,"send request:%s",pData);
    ret = send(fd, pData, strlen(pData), 0);
    if(ret < 0){
        Trace(1,"socket send fail");
        return -1;
    }
    Trace(1,"socket send success");

    struct fd_set fds;
    struct timeval timeout={12,0};
    FD_ZERO(&fds);
    FD_SET(fd,&fds);
    while(!flag)
    {
        ret = select(fd+1,&fds,NULL,NULL,&timeout);
        switch(ret)
        {
            case -1:
                Trace(1,"select error");
                flag = true;
                break;
            case 0:
                Trace(1,"select timeout");
                flag = true;
                break;
            default:
                if(FD_ISSET(fd,&fds))
                {
                    Trace(1,"select return:%d",ret);
                    memset(retBuffer+recvLen,0,retBufferLen-recvLen);
                    ret = recv(fd,retBuffer+recvLen,retBufferLen-recvLen,0);
                    Trace(1,"ret:%d",ret);
                    recvLen += ret;
                    if(ret < 0)
                    {
                        Trace(1,"recv error");
                        flag = true;
                        break;
                    }
                    else if(ret == 0)
                    {
                        Trace(1,"ret == 0");
                        flag = true;
                        break;
                    }
                    else if(ret < 1352)
                    {
                        Trace(1,"recv len:%d,data:%s",recvLen,retBuffer);
                        *bufferLen = recvLen;
                        close(fd);
                        return recvLen;
                    }                  
                    
                }
                break;
        }
    }
    close(fd);
    return -1;
}

void send_Data2Server(char* path)
{
    char buffer[2048];
    int len = sizeof(buffer);
    //perform http post
    if(Http_Post(SERVER_IP,SERVER_PORT,path,buffer,&len) < 0)
    {
        Trace(1,"http Post fail");
    }
    else
    {
        //show response message though tracer(pay attention:tracer can not show all the word if the body too long)
        Trace(1,"http post success,ret:%s",buffer);
        char* index0 = strstr(buffer,"\r\n\r\n");
        char temp = index0[4];
        index0[4] = '\0';
        Trace(1,"http response header:%s",buffer);
        index0[4] = temp;
        Trace(1,"http response body:%s",index0+4);
    }
}

void OnPinFalling(GPIO_INT_callback_param_t* param)
{
    switch(param->pin)
    {
        case GPIO_PIN2: // El pin02 realiza la llamada al numero por defecto
            AUDIO_MicOpen();
            AUDIO_SpeakerOpen();
            if(isCallComing){
                ackCall = true;
                CALL_Answer();
                break;
            }
            else{
                if(setAlarm1){
                    setAlarm2 = true;
                    break;
                }
                else{
                    makeCall = true;
                    //GPIO_LEVEL statusNow;
                    //GPIO_Get(GPIO_PIN2,&statusNow);
                    //Trace(1,"http gpio2 status now:%d",statusNow);
                    //CALL_Dial(DIAL_NUMBER);
                    break;
                }
            }
            break;       
        case GPIO_PIN3: // El pin03 realiza la llamada al numero por defecto
            ackCall = true;
            setAlarm1 = true;
            CALL_HangUp();
            AUDIO_MicClose();
            AUDIO_SpeakerClose();
            break;
        default:
            break;
    }
}

void init_GPIO()
{
    GPIO_config_t gpioINT = { // Configuro el PIN02 en interrupcion por flanco descendente, con nivel por defecto bajo.
        .mode               = GPIO_MODE_INPUT_INT,
        .pin                = GPIO_PIN2,
        .defaultLevel       = GPIO_LEVEL_LOW,
        .intConfig.debounce = 500,
        .intConfig.type     = GPIO_INT_TYPE_FALLING_EDGE,
        .intConfig.callback = OnPinFalling
    };
    GPIO_config_t gpioINT2 = { // Configuro el PIN03 en interrupcion por flanco descendente, con nivel por defecto bajo.
        .mode               = GPIO_MODE_INPUT_INT,
        .pin                = GPIO_PIN3,
        .defaultLevel       = GPIO_LEVEL_LOW,
        .intConfig.debounce = 500,
        .intConfig.type     = GPIO_INT_TYPE_FALLING_EDGE,
        .intConfig.callback = OnPinFalling
    }; 
    // Inicio las interrupciones

    GPIO_Init(gpioINT); 
    GPIO_Init(gpioINT2);
}

void init_GPS(GPS_Info_t* gpsInfo, uint8_t * buffer)
{  
    //open GPS hardware(UART2 open either)
    GPS_Init();
    GPS_Open(NULL);
    //wait for gps start up, or gps will not response command
    while(gpsInfo->rmc.latitude.value == 0)OS_Sleep(1000);

    // set gps nmea output interval
    for(uint8_t i = 0;i<5;++i) 
    {
        bool ret = GPS_SetOutputInterval(10000);
        Trace(1,"set gps ret:%d",ret);
        if(ret)
            break;
        OS_Sleep(1000);
    }

    if(!GPS_GetVersion(buffer,150))Trace(1,"get gps firmware version fail");
    else Trace(1,"gps firmware version:%s",buffer);
    if(!GPS_SetOutputInterval(1000)) Trace(1,"set nmea output interval fail");
}

void init_UART(){
 //open UART1 to print NMEA infomation
    UART_Config_t config = {
        .baudRate = UART_BAUD_RATE_115200,
        .dataBits = UART_DATA_BITS_8,
        .stopBits = UART_STOP_BITS_1,
        .parity   = UART_PARITY_NONE,
        .rxCallback = NULL,
        .useEvent   = true
    };
    UART_Init(UART1,config);
}

void get_Coordinates(GPS_Info_t* gpsInfo, uint8_t * buffer, double* latitude, double* longitude){
 if(isGpsOn)
        {
            uint8_t isFixed = gpsInfo->gsa[0].fix_type > gpsInfo->gsa[1].fix_type ?gpsInfo->gsa[0].fix_type:gpsInfo->gsa[1].fix_type;
            char* isFixedStr= malloc(sizeof(isFixedStr));            
            if(isFixed == 2){
                isGPSFixed = true;
                isFixedStr = "2D fix";
            }
            else if(isFixed == 3)
            {   
                isGPSFixed = true;
                if(gpsInfo->gga.fix_quality == 1)
                    isFixedStr = "3D fix";
                else if(gpsInfo->gga.fix_quality == 2)
                    isFixedStr = "3D/DGPS fix";
            }
            else{
                isFixedStr = "no fix";
                isGPSFixed = false;
            }
            //convert unit ddmm.mmmm to degree(°) 
            int temp = (int)(gpsInfo->rmc.latitude.value/gpsInfo->rmc.latitude.scale/100);
            *latitude = temp+(double)(gpsInfo->rmc.latitude.value - temp*gpsInfo->rmc.latitude.scale*100)/gpsInfo->rmc.latitude.scale/60.0;
            temp = (int)(gpsInfo->rmc.longitude.value/gpsInfo->rmc.longitude.scale/100);
            *longitude = temp+(double)(gpsInfo->rmc.longitude.value - temp*gpsInfo->rmc.longitude.scale*100)/gpsInfo->rmc.longitude.scale/60.0;
            snprintf(buffer,sizeof(buffer),"http GPS fix mode:%d, BDS fix mode:%d, fix quality:%d, satellites tracked:%d, gps sates total:%d, is fixed:%s, coordinate:WGS84, Latitude:%f, Longitude:%f, unit:degree,altitude:%f",gpsInfo->gsa[0].fix_type, gpsInfo->gsa[1].fix_type,
                                                                gpsInfo->gga.fix_quality,gpsInfo->gga.satellites_tracked, gpsInfo->gsv[0].total_sats, isFixedStr, latitude,longitude,gpsInfo->gga.altitude);
            //show in tracer
            Trace(1,"http GPS fix mode:%d, BDS fix mode:%d, fix quality:%d, satellites tracked:%d, gps sates total:%d, is fixed:%s",gpsInfo->gsa[0].fix_type, gpsInfo->gsa[1].fix_type, gpsInfo->gga.fix_quality,gpsInfo->gga.satellites_tracked, gpsInfo->gsv[0].total_sats, isFixedStr);
            //send to UART1
            UART_Write(UART1,buffer,strlen(buffer));
            UART_Write(UART1,"\r\n\r\n",4);
            free(isFixedStr);
        }
}

void loop_function(uint8_t *imei,GPS_Info_t* gpsInfo, uint8_t * buffer){

    uint8_t percent;                                // variables para el calculo de bateria   
    uint16_t v;                                     // variables para el calculo de bateria 
    double* latitude = malloc(sizeof(latitude));    // vartiables para el gps
    double* longitude = malloc(sizeof(longitude));  // vartiables para el gps 
    char* path=malloc(sizeof(path));

    while(1)
    {
        v = PM_Voltage(&percent);
        if(v <= 15){
            sprintf(path,"/module/action/%s/low-battery",imei);
            //send_Data2Server(path);
            OS_Sleep(3000);
        }
        if(makeCall){
            sprintf(path,"/module/action/%s/call",imei);
            Trace(1,"http %s",path);
            //send_Data2Server(path);
            makeCall = false;
            OS_Sleep(3000);
        }
        if(setAlarm2){
            sprintf(path,"/module/action/%s/alarm",imei);
            Trace(1,"http %s",path);
            //send_Data2Server(path);
            setAlarm1 =false;
            setAlarm2 = false;
            OS_Sleep(3000);
        }    
        if(isGPSFixed){
            get_Coordinates(gpsInfo, buffer,latitude,longitude);       
            sprintf(path,"/module/save-location/%s/%f/%f/%d",imei,*latitude,*longitude, percent);
            //send_Data2Server(path);
            free(latitude);
            free(longitude); 
        }
        else{
            if(!Network_GetCellInfoRequst()){
            sprintf(path,"/module/noGPS/noCELL/%s",imei);
            Trace(1,"http %s",path);
            //send_Data2Server(path);
            }
        }
        free(path);
        OS_Sleep(60000);
        setAlarm1 = false;
        ackCall = false;
    }
}

void init_MainTask(void* param)
{    

    // ***********************************DECLARACION DE VARIABLES
    GPS_Info_t* gpsInfo = Gps_GetInfo();            // vartiables para el gps
    uint8_t buffer[300];                            // vartiables para el gps
    uint8_t imei[16];                               // variables para el IMEI
   
    // ************************************************** ESPERAR A LA CONEXION 2G
    semStart = OS_CreateSemaphore(0);
    if(OS_WaitForSemaphore(semStart,3000000) == false){ // espera por 5 min para poder conectarse a la red
        Trace(1, "No se pudo conectar a la red en 5min");
    }
    OS_DeleteSemaphore(semStart);

    // *******************************************************INICIALIZACIONES
    isDialSuccess = false;
    isCallComing = false;
    ackCall = false;
    makeCall = false;
    setAlarm1 = false;
    setAlarm2 = false;
    init_GPIO();
    init_UART();
    init_GPS(gpsInfo, buffer);

    //¨************************************************IMEI
    memset(imei,0,sizeof(imei));
    INFO_GetIMEI(imei);

    //************************************************PRIMERA CONEXION
    char* path=malloc(sizeof(path));                // path
    sprintf(path,"/module/save/%s",imei);
    //send_Data2Server(path);
    free(path);
    OS_Sleep(3000);

    //**************************************************ITERACIÓN de funcionamiento
    loop_function(imei,gpsInfo,buffer);


}

void socket_MainTask(void *pData)
{
    API_Event_t* event=NULL;

    testTaskHandle = OS_CreateTask(init_MainTask,
        NULL, NULL, TEST_TASK_STACK_SIZE, TEST_TASK_PRIORITY, 0, 0, TEST_TASK_NAME);

    while(1)
    {
        if(OS_WaitEvent(socketTaskHandle, (void**)&event, OS_TIME_OUT_WAIT_FOREVER))
        {
            EventDispatch(event);
            OS_Free(event->pParam1);
            OS_Free(event->pParam2);
            OS_Free(event);
        }
    }
}

void socket_Main()
{
    socketTaskHandle = OS_CreateTask(socket_MainTask,
        NULL, NULL, MAIN_TASK_STACK_SIZE, MAIN_TASK_PRIORITY, 0, 0, MAIN_TASK_NAME);
    OS_SetUserMainHandle(&socketTaskHandle);
}
