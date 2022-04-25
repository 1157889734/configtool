#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include "OnvifApi.h"
#include "OnvifInc.h"
#include "wsdd.nsmap"
#include "soapStub.h"
#include "wsseapi.h"

/* �߳̽���״̬��1��ʾ�߳��������й����У�0��ʾ�߳�û�����У���δ�������ѽ��� */
static int  s_iBeThreadRunningState = 0;

static CB_IP_FILTER s_fCBIpFilter = NULL;



//static struct soap * m_pstSoap = NULL; 

/* ����ͷ���ֹ��ܵ�uuid��־ */
static int s_DiscovryUuidCnt = 0;

static char * roStrUuidString1 = "uuid:4c1de2d1-dbd9-49ce-a467-35485648eac4";

static char * roStrUuidString2 = "uuid:4c1de2d1-dbd9-49ce-a467-21456ea21b35";

/* �������Ͷ�Ӧ���ַ���,�ַ���˳���Ӧapi���÷������͵�˳�� */
static char * m_OnvifApiRtnString[] = {
    "ONVIF API-Return OK",
    "ONVIF API-Input Param Error",
    "ONVIF API-SOAP Calling Error",
    "ONVIF API-Network Error",
    "ONVIF API-Authentication Error",
    "ONVIF API-Service Addr Error",
    "ONVIF API-Other Error",    
};

/* !!! ע��: ������������������Ҫһһ��Ӧ */
/* ����ͷ��Ϣ���� */
ST_ONVIF_CAMINFO_SET g_CamInfoSet[MAX_ONVIF_CAM_NUM];

/* Ԥ��λ��Ϣ */
ST_ONVIF_CAM_PRESET_INFO  g_CamPresetInfo[MAX_ONVIF_CAM_NUM][MAX_PRESETS_PER_CAM];

/* ����ͷ�����ַ */
static ST_ONVIF_CAM_SERVICES m_stCamServices[MAX_ONVIF_CAM_NUM];

/* ����ͷ��Ȩ���� */
static ST_ONVIF_CAM_AUTHORIZE m_stCamAuthorize[MAX_ONVIF_CAM_NUM];

static pthread_mutex_t s_CamParamMutex;

static int g_iProbeNum = 0;

struct in_addr g_if_req; 

// debug start by dingjq 2015/01/28
struct soap *CreateSoap(int timeout)
{
    struct soap * pSoap = NULL;
	
    pSoap = soap_new(); 
    if(pSoap == NULL)  
    {  
        return NULL;  
    }  

    /* namespaces������wsdd.nsmap�ļ��� */
    soap_set_namespaces(pSoap, namespaces);

    if (timeout <= 0)
    {
        pSoap->recv_timeout = 5;
        pSoap->send_timeout = 5;
        pSoap->connect_timeout = 5;
    }
    else
    {
        pSoap->recv_timeout = timeout;
        pSoap->send_timeout = timeout;
        pSoap->connect_timeout = timeout;
    }

    /* ��IP��ַ */

        pSoap->ipv4_multicast_if = (char*)soap_malloc(pSoap, sizeof(struct in_addr)); 
        if ( NULL == pSoap->ipv4_multicast_if )
        {
            printf("%s-%d: Malloc Error\n", __FUNCTION__, __LINE__);
            return -1;
        }
        memset(pSoap->ipv4_multicast_if, 0, sizeof(struct in_addr)); 
        memcpy(pSoap->ipv4_multicast_if, (char*)&g_if_req, sizeof(g_if_req));

    return pSoap;
}

void DestroySoap(struct soap * pSoap)
{
    soap_destroy(pSoap);  
    soap_end(pSoap);   
    soap_free(pSoap);  
}
// debug end by dingjq 2015/01/28

static int GetIPAddrFromHttpAddr(const char * strHttp, char * strIP)
{
    char * pstr1 = NULL;
    char * pstr2 = NULL;
    
    pstr1 = strtok(strHttp, ":////");
    if ( NULL == pstr1 ) 
    {
        return 0;
    }
    pstr2 = strtok(NULL, ":////");
    if ( NULL == pstr2 ) 
    {
        return 0;
    }
    strcpy(strIP, pstr2);
    return 1;
}


static int CamAuthorized(struct soap * pstSoap, int channel)
{
    char un[ONVIF_AUTHOR_STR_LEN] = {0x0};
    char pw[ONVIF_AUTHOR_STR_LEN] = {0x0};
    ONVIF_BOOL  bAuthor = ONVIF_FALSE;
    
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel < 0) )
    {
        return E_OAR_INPUT_ERR;
    }

    /* ����ռ�Ȩͷ����Ϣ */
    soap_wsse_delete_Security(pstSoap);
    
    memset(un, 0x0, ONVIF_AUTHOR_STR_LEN);
    memset(pw, 0x0, ONVIF_AUTHOR_STR_LEN);
    
    strcpy(un, m_stCamAuthorize[channel].acUserName);
    strcpy(pw, m_stCamAuthorize[channel].acPassWord);
    bAuthor = m_stCamAuthorize[channel].bAuthor;
    
    if ( ONVIF_TRUE == bAuthor )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "User Authorized\n");
        soap_wsse_add_UsernameTokenDigest(pstSoap, NULL, un, pw);
    }

    return E_OAR_OK;
}

static int SetAuthorUnPw(int channel, const char * un, const char * pw)
{
    strcpy(m_stCamAuthorize[channel].acUserName, un);
    strcpy(m_stCamAuthorize[channel].acPassWord, pw);

    return 0;
}

static int SetAuthorState(int channel, ONVIF_BOOL bEnable)
{
    if ( (strlen(m_stCamAuthorize[channel].acUserName) == 0) 
        || (strlen(m_stCamAuthorize[channel].acPassWord) == 0) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "SetAuthorState Error: UserName or PassWord -- strlen = 0\n");
        return -1;
    }
    
    DebugPrint(ONVIF_DEBUG_LEVEL, "Enable Authorized: channel[%d]\n", channel);
    m_stCamAuthorize[channel].bAuthor = bEnable;
    return 0;
}

/* ͨ����ȡ����ͷ��Ϣ�������ͷ�ļ�Ȩ��Ϣ */
static int CheckAuthorized(struct soap * pstSoap, const char * acCamHttpAddr, int channel)
{
    struct _tds__GetDeviceInformation stGetDeviceInformation;
    struct _tds__GetDeviceInformationResponse stGetDeviceInformationResponse;

    if ( strlen(acCamHttpAddr) == 0 )
    {
        return E_OAR_INPUT_ERR;
    }
    
    DebugPrint(ONVIF_DEBUG_LEVEL, "%s: addr = %s\n", __FUNCTION__, acCamHttpAddr);

    /* ����Ȩ��Ϣ֮ǰ��Ҫ���ü�Ȩ״̬ */
    SetAuthorState(channel, ONVIF_FALSE);
    
    memset(&stGetDeviceInformation, 0x0, sizeof(struct _tds__GetDeviceInformation));
    memset(&stGetDeviceInformationResponse, 0x0, sizeof(struct _tds__GetDeviceInformationResponse));
    soap_call___tds__GetDeviceInformation(pstSoap, acCamHttpAddr, NULL, 
                                          &stGetDeviceInformation, &stGetDeviceInformationResponse);
    if ( pstSoap->error )
    {
        soap_wsse_add_UsernameTokenDigest(pstSoap, NULL, m_stCamAuthorize[channel].acUserName, m_stCamAuthorize[channel].acPassWord);
        soap_call___tds__GetDeviceInformation(pstSoap, acCamHttpAddr, NULL, 
                                          &stGetDeviceInformation, &stGetDeviceInformationResponse);
        if ( pstSoap->error )
        {
		    /* �������������Ҫ�����Ȩͷ����Ϣ�������Ӱ����һ������ͷ�ļ�Ȩ�����ж� */
            soap_wsse_delete_Security(pstSoap);
            ONVIF_ERROR_PRINT(pstSoap);
            DebugPrint(ONVIF_DEBUG_LEVEL, "Check Authorized Error. Addr=%s\n", m_stCamServices[channel].acCamHttpAddr);
            return E_OAR_AUTHR_ERR;
        }
        /* ʹ�ܸ�ͨ������ͷ�ļ�Ȩ���� */
        if ( 0 != SetAuthorState(channel, ONVIF_TRUE))
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "Check Authorized Error. \n");
            return E_OAR_OTHER_ERR;
        }
    }

    /* �������������Ҫ�����Ȩͷ����Ϣ�������Ӱ����һ������ͷ�ļ�Ȩ�����ж� */
    soap_wsse_delete_Security(pstSoap);

    DebugPrint(ONVIF_DEBUG_LEVEL, "********************   Camera Information (%d)   ********************\n", channel);
    DebugPrint(ONVIF_DEBUG_LEVEL, "** Manufacturer     : %s\n", stGetDeviceInformationResponse.Manufacturer);
    DebugPrint(ONVIF_DEBUG_LEVEL, "** Model            : %s\n", stGetDeviceInformationResponse.Model);
    DebugPrint(ONVIF_DEBUG_LEVEL, "** FirmwareVersion  : %s\n", stGetDeviceInformationResponse.FirmwareVersion);
    DebugPrint(ONVIF_DEBUG_LEVEL, "** SerialNumber     : %s\n", stGetDeviceInformationResponse.SerialNumber);
    DebugPrint(ONVIF_DEBUG_LEVEL, "** HardwareId       : %s\n", stGetDeviceInformationResponse.HardwareId);
    DebugPrint(ONVIF_DEBUG_LEVEL, "*********************************************************************\n");
    return E_OAR_OK;
}

static int GetProfiles(struct soap * pstSoap, const char * acHttpAddr, struct _trt__GetProfilesResponse *  ptrt__GetProfilesResponse)
{
    //struct SOAP_ENV__Header header; 
    struct _trt__GetProfiles trt__GetProfiles;
    
    pstSoap->recv_timeout = 3;      //����5����û�����ݾ��˳�  
    //soap_default_SOAP_ENV__Header(pstSoap, &header); 

    memset((void *)&trt__GetProfiles, 0x0, sizeof(struct _trt__GetProfiles));

    soap_call___trt__GetProfiles(pstSoap, acHttpAddr, NULL, 
                                       &trt__GetProfiles, ptrt__GetProfilesResponse);
    if ( pstSoap->error  )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    DebugPrint(ONVIF_DEBUG_LEVEL, "%s: Success! [Addr: %s]\n", __FUNCTION__, acHttpAddr);

    return 0;
}

static int GetRtspUril(struct soap * pstSoap, char * acTokenFileName, const char * acHttpAddr, char * acRtspUril)
{
    struct _trt__GetStreamUri trt__GetStreamUri;
    struct tt__StreamSetup    stStreamSetup;
    struct tt__Transport stTransport;
    struct _trt__GetStreamUriResponse trt__GetStreamUriResponse;

    if ( NULL == pstSoap || NULL == acTokenFileName || NULL == acHttpAddr || NULL == acRtspUril)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: Input Error\n", __FUNCTION__);
        return -1;
    }
    
    memset(&trt__GetStreamUri, 0x0, sizeof(struct _trt__GetStreamUri));
    memset(&stStreamSetup, 0x0, sizeof(struct tt__StreamSetup));
    memset(&stTransport, 0x0, sizeof(struct tt__Transport));
    memset(&trt__GetStreamUriResponse, 0x0, sizeof(struct _trt__GetStreamUriResponse));
    trt__GetStreamUri.ProfileToken = acTokenFileName;

    soap_default_tt__StreamSetup(pstSoap, &stStreamSetup);
    soap_default_tt__Transport(pstSoap, &stTransport);
    stTransport.Protocol = tt__TransportProtocol__UDP;
    stStreamSetup.Transport = &stTransport;
    trt__GetStreamUri.StreamSetup = &stStreamSetup;
    
    
    //soap_default_tt__StreamSetup(pstSoap, trt__GetStreamUri.StreamSetup);

    soap_call___trt__GetStreamUri(pstSoap, acHttpAddr, NULL,  &trt__GetStreamUri, &trt__GetStreamUriResponse);
    if ( pstSoap->error  )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }
    strcpy(acRtspUril, trt__GetStreamUriResponse.MediaUri->Uri);

    return 0;
}

static int GetSnapShortUril(struct soap * pstSoap, char * acTokenFileName, const char * acHttpAddr, char * acSnapShortUril)
{
    struct _trt__GetSnapshotUri trt__GetSnapShortUri;
    struct _trt__GetSnapshotUriResponse trt__GetSnapShortUriResponse;

    if ( NULL == pstSoap || NULL == acTokenFileName || NULL == acHttpAddr || NULL == acSnapShortUril )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: Input Error\n", __FUNCTION__);
        return -1;
    }
    
    /* ��һ��profileΪ������ */
    memset(&trt__GetSnapShortUri, 0x0, sizeof(struct _trt__GetSnapshotUri));
    memset(&trt__GetSnapShortUriResponse, 0x0, sizeof(struct _trt__GetSnapshotUriResponse ));
    trt__GetSnapShortUri.ProfileToken = acTokenFileName;

    soap_call___trt__GetSnapshotUri(pstSoap, acHttpAddr, NULL, 
                                                       &trt__GetSnapShortUri, &trt__GetSnapShortUriResponse);

    if ( pstSoap->error  )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;                   
    }
    strcpy(acSnapShortUril, trt__GetSnapShortUriResponse.MediaUri->Uri);

    return 0;
}

static int SetCameraTime(struct soap * pstSoap, const int channel,const char * acHttpAddr, struct tm * pt)
{
    struct _tds__GetSystemDateAndTime stSystemDateAndTime;
    struct _tds__GetSystemDateAndTimeResponse stGetSystemDateAndTimeResponse;
    struct _tds__SetSystemDateAndTime stSetSystemDateAndTime;
    struct _tds__SetSystemDateAndTimeResponse stSetSystemDateAndTimeResponse;
    //struct tt__TimeZone stTimeZone;	    /* optional element of type tt:TimeZone */
	//struct tt__DateTime stUTCDateTime;	/* optional element of type tt:DateTime */

    memset((void *)&stSystemDateAndTime,            0x0, sizeof(struct _tds__GetSystemDateAndTime));
    memset((void *)&stGetSystemDateAndTimeResponse, 0x0, sizeof(struct _tds__GetSystemDateAndTimeResponse));
    memset((void *)&stSetSystemDateAndTime,         0x0, sizeof(struct _tds__SetSystemDateAndTime));
    memset((void *)&stSetSystemDateAndTimeResponse, 0x0, sizeof(struct _tds__SetSystemDateAndTimeResponse));
    
    CamAuthorized(pstSoap, channel);
    soap_call___tds__GetSystemDateAndTime(pstSoap, acHttpAddr, NULL, &stSystemDateAndTime, &stGetSystemDateAndTimeResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    /* �޸�����:�������ͷ��ǰ��ʱ��ģʽ��NTP,�ᵼ������ʱ��ʧ�ܣ�����̶���ΪManualģʽ */
    //stSetSystemDateAndTime.DateTimeType = stGetSystemDateAndTimeResponse.SystemDateAndTime->DateTimeType;
    stSetSystemDateAndTime.DateTimeType = tt__SetDateTimeType__Manual;
    stSetSystemDateAndTime.DaylightSavings = stGetSystemDateAndTimeResponse.SystemDateAndTime->DaylightSavings;
    stSetSystemDateAndTime.TimeZone        = stGetSystemDateAndTimeResponse.SystemDateAndTime->TimeZone;
    stSetSystemDateAndTime.UTCDateTime     = stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime;
    stSetSystemDateAndTime.UTCDateTime->Date->Year  = pt->tm_year;
    stSetSystemDateAndTime.UTCDateTime->Date->Month = pt->tm_mon;
    stSetSystemDateAndTime.UTCDateTime->Date->Day   = pt->tm_mday;
    stSetSystemDateAndTime.UTCDateTime->Time->Hour  = pt->tm_hour;
    stSetSystemDateAndTime.UTCDateTime->Time->Minute= pt->tm_min;
    stSetSystemDateAndTime.UTCDateTime->Time->Second= pt->tm_sec;

    CamAuthorized(pstSoap, channel);
    soap_call___tds__SetSystemDateAndTime(pstSoap, acHttpAddr, NULL, &stSetSystemDateAndTime, &stSetSystemDateAndTimeResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    return 0;
}

static int GetCameraTime(struct soap * pstSoap, const int channel, const char * acHttpAddr, struct tm * pt)
{
    struct _tds__GetSystemDateAndTime stSystemDateAndTime;
    struct _tds__GetSystemDateAndTimeResponse stGetSystemDateAndTimeResponse;

    //struct tt__TimeZone stTimeZone;	/* optional element of type tt:TimeZone */
	//struct tt__DateTime stUTCDateTime;	/* optional element of type tt:DateTime */

    memset((void *)&stSystemDateAndTime, 0x0, sizeof(struct _tds__GetSystemDateAndTime));
    memset((void *)&stGetSystemDateAndTimeResponse, 0x0, sizeof(struct _tds__GetSystemDateAndTimeResponse));

    CamAuthorized(pstSoap, channel);
    soap_call___tds__GetSystemDateAndTime(pstSoap, acHttpAddr, NULL, &stSystemDateAndTime, &stGetSystemDateAndTimeResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }
/*
    printf("TimeType = %d\n", stGetSystemDateAndTimeResponse.SystemDateAndTime->DateTimeType);
    printf("Daylight = %d\n", stGetSystemDateAndTimeResponse.SystemDateAndTime->DaylightSavings);
    printf("TimeZone = %s\n", stGetSystemDateAndTimeResponse.SystemDateAndTime->TimeZone->TZ);
    printf("UTCTime  = year[%d] month[%d] day[%d] hour[%d] min[%d] sec[%d]\n",
                         stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Date->Year,
                         stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Date->Month,
                         stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Date->Day,
                         stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Time->Hour,
                         stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Time->Minute,
                         stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Time->Second);
*/

    pt->tm_year   = stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Date->Year;
    pt->tm_mon    = stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Date->Month;
    pt->tm_mday   = stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Date->Day;

    pt->tm_hour   = stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Time->Hour;
    pt->tm_min    = stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Time->Minute;
    pt->tm_sec    = stGetSystemDateAndTimeResponse.SystemDateAndTime->UTCDateTime->Time->Second;
	
    return 0;

}

/* ��ȡ����ͷ�ķ�����Ϣ */
static int GetDeviceSevices(struct soap * pstSoap, const char * acHttpAddr, int channel, ST_ONVIF_CAM_SERVICE_ADDRS* pStServiceAddrs )
{
    int i = 0;
    struct _tds__GetServices stGetServices;
    struct _tds__GetServicesResponse stGetServicesResponse;

    DebugPrint(ONVIF_DEBUG_LEVEL, "%s: Enter Function\n", __FUNCTION__);
    
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel < 0) )
    {
        return E_OAR_INPUT_ERR;
    }

    if ( 0 == strlen(acHttpAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s:Null HttpAddr\n", __FUNCTION__);
        return E_OAR_OTHER_ERR;
    }
    
    memset(&stGetServices, 0x0, sizeof(struct _tds__GetServices));
    memset(&stGetServicesResponse, 0x0, sizeof(struct _tds__GetServicesResponse));

    
    CamAuthorized(pstSoap, channel);
    soap_call___tds__GetServices(pstSoap, acHttpAddr, NULL,
                                        &stGetServices, &stGetServicesResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    for ( i = 0; i < stGetServicesResponse.__sizeService; i++)
    {
        //printf("%s: No%d, Addr: %s\n", __FUNCTION__, i, stGetServicesResponse.Service[i].XAddr);
        if ( 0 == strcmp(stGetServicesResponse.Service[i].Namespace, ONVIF_CAM_NAMESPACE_DEVICE) )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "Set DeviceService Addr! [channel=%d]\n",channel);
            strcpy(pStServiceAddrs->acDeviceServiceAddr, stGetServicesResponse.Service[i].XAddr);
        }
        else if ( 0 == strcmp(stGetServicesResponse.Service[i].Namespace, ONVIF_CAM_NAMESPACE_MEDIA) )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "Set MediaService Addr! [channel=%d]\n",channel);
            strcpy(pStServiceAddrs->acMediaServiceAddr, stGetServicesResponse.Service[i].XAddr);
        }
        else if ( 0 == strcmp(stGetServicesResponse.Service[i].Namespace, ONVIF_CAM_NAMESPACE_PTZ) )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "Set PTZService Addr! [channel=%d]\n",channel);
            strcpy(pStServiceAddrs->acPTZServiceAddr, stGetServicesResponse.Service[i].XAddr);
        }
        else if ( 0 == strcmp(stGetServicesResponse.Service[i].Namespace, ONVIF_CAM_NAMESPACE_IMG) )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "Set IMGService Addr! [channel=%d]\n",channel);
            strcpy(pStServiceAddrs->acIMGServiceAddr, stGetServicesResponse.Service[i].XAddr);
        }                
        else
        {
            //NONE;
        }
    }

    DebugPrint(ONVIF_DEBUG_LEVEL, "%s: Success! [Addr: %s]\n", __FUNCTION__, acHttpAddr);
    return 0;
}

static int GetAllPresets(struct soap * pstSoap, const char * acHttpAddr, const char * profile)
{
    int  i = 0, num = 0;
    char acProfile[ONVIF_STRING_LEN] = {0x0};
    struct _tptz__GetPresets stGetPresets;
    struct _tptz__GetPresetsResponse stGetPresetsResponse;

    memset(acProfile, 0x0, ONVIF_STRING_LEN);
    memset(&stGetPresets, 0x0, sizeof(struct _tptz__GetPresets));
    memset(&stGetPresetsResponse, 0x0, sizeof(struct _tptz__GetPresetsResponse));

    DebugPrint(ONVIF_DEBUG_LEVEL, "Enter Func: %s\n", __FUNCTION__);

    strncpy(acProfile, profile, ONVIF_STRING_LEN - 1);
    stGetPresets.ProfileToken = acProfile;
    
    soap_call___tptz__GetPresets(pstSoap, acHttpAddr, NULL, &stGetPresets , &stGetPresetsResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    num = stGetPresetsResponse.__sizePreset;

    DebugPrint(ONVIF_DEBUG_LEVEL, "-- Total Presets : %d\n", num);

    for ( i = 0; i < num; i++ )
    {
        //strncpy(pstPreset[i].acPresetName, stGetPresetsResponse.Preset[i].Name, ONVIF_STRING_LEN - 1);
        //strncpy(pstPreset[i].acPresetToken, stGetPresetsResponse.Preset[i].token, ONVIF_STRING_LEN - 1);
        //pstPreset[i].bSet = ONVIF_TRUE;
        DebugPrint(ONVIF_DEBUG_LEVEL, "Num[%d]: Name = %s, Token = %s\n",i,stGetPresetsResponse.Preset[i].Name, 
                                                       stGetPresetsResponse.Preset[i].token);
    }


    return 0;
    
}

static int AddPreset(struct soap * pstSoap, int channel, const char * acHttpAddr, const char * profile, int iPresetNo)
{
    int i = 0, j = 0;
    int num = 0;
    int iToken = 0;
    char   acProfile[ONVIF_STRING_LEN] = {0x0};
    char   acPresetToken[ONVIF_STRING_LEN] = {0x0};
    ONVIF_BOOL bExsit = ONVIF_FALSE;
    struct _tptz__SetPreset sttptz__SetPreset;
    struct _tptz__GetPresets stGetPresets;
    struct _tptz__GetPresetsResponse stGetPresetsResponse;
    struct _tptz__SetPresetResponse  sttptz__SetPresetResponse;

    memset(acProfile, 0x0, ONVIF_STRING_LEN);
    memset(&stGetPresets, 0x0, sizeof(struct _tptz__GetPresets));
    memset(&stGetPresetsResponse, 0x0, sizeof(struct _tptz__GetPresetsResponse));

    DebugPrint(ONVIF_DEBUG_LEVEL, "Enter Func: %s\n", __FUNCTION__);

    strncpy(acProfile, profile, ONVIF_STRING_LEN - 1);
    DebugPrint(ONVIF_DEBUG_LEVEL, "acProfile: %s\n", acProfile);
    stGetPresets.ProfileToken = acProfile;
    
    CamAuthorized(pstSoap, channel);
    soap_call___tptz__GetPresets(pstSoap, acHttpAddr, NULL, &stGetPresets , &stGetPresetsResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    memset(&sttptz__SetPreset, 0x0, sizeof(struct _tptz__SetPreset));
    memset(acProfile, 0x0, ONVIF_STRING_LEN);
    strncpy(acProfile, profile, ONVIF_STRING_LEN - 1);

    sttptz__SetPreset.ProfileToken = acProfile;

    /* ��������presettoken��Ϊ�գ���ʾ�޸�������ڵ�Ԥ��λ��Ϣ */
    num = stGetPresetsResponse.__sizePreset;

    DebugPrint(ONVIF_DEBUG_LEVEL, "Get Presets Number: %d\n", num);

    for ( i = 0; i < num; i++ )
    {
        //printf("No-%d: acPreset=%s, GetPresetName=%s\n", i, acPresetToken, stGetPresetsResponse.Preset[i].token);
        sscanf(stGetPresetsResponse.Preset[i].token, "%d", &iToken);
        if ( iToken == iPresetNo )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "PresetID = %d, Exsit\n", iPresetNo);
            bExsit = ONVIF_TRUE;
            break;
        }

    }
    
    if ( ONVIF_TRUE == bExsit )
    {
        memset(acPresetToken, 0x0, ONVIF_STRING_LEN);
        snprintf(acPresetToken, ONVIF_STRING_LEN, "%d", iPresetNo);
        
        sttptz__SetPreset.PresetToken = acPresetToken;
        CamAuthorized(pstSoap, channel);
        soap_call___tptz__SetPreset(pstSoap, acHttpAddr, NULL, &sttptz__SetPreset, &sttptz__SetPresetResponse);
        if ( pstSoap->error )
        {
            ONVIF_ERROR_PRINT(pstSoap);
            return pstSoap->error;
        }
    }
    else
    {
        int iBegin = 0, iEnd = 0;

        iBegin = 0;
        
        for ( i = 0; i < num; i++ )
        {
            sscanf(stGetPresetsResponse.Preset[i].token, "%d", &iEnd);
            DebugPrint(ONVIF_DEBUG_LEVEL, "Add: %d - %d\n", iBegin, iEnd);
            /* �û�б����õ�Ԥ��λ��Ϣ */
            for ( j = iBegin + 1; j < iEnd ; j++ )
            {
                CamAuthorized(pstSoap, channel);
                soap_call___tptz__SetPreset(pstSoap, acHttpAddr, NULL, &sttptz__SetPreset, &sttptz__SetPresetResponse);
                if ( pstSoap->error )
                {
                    ONVIF_ERROR_PRINT(pstSoap);
                    return pstSoap->error;
                }
                DebugPrint(ONVIF_DEBUG_LEVEL, "Added: Token = %s\n", sttptz__SetPresetResponse.PresetToken);
            }
            sscanf(stGetPresetsResponse.Preset[i].token, "%d", &iBegin);
            //sscanf(stGetPresetsResponse.Preset[i+1].token, "%d", &iEnd);
        }

        /* �����е�����Ԥ��λ���С�ڲ���Ԥ��λ���ʱ����Ҫ����������׶ο�ȱ��Ԥ��λ */
        if ( iEnd < iPresetNo )
        {
            for ( i = iEnd; i < iPresetNo; i++ )
            {
                CamAuthorized(pstSoap, channel);
                soap_call___tptz__SetPreset(pstSoap, acHttpAddr, NULL, &sttptz__SetPreset, &sttptz__SetPresetResponse);
                if ( pstSoap->error )
                {
                    ONVIF_ERROR_PRINT(pstSoap);
                    return pstSoap->error;
                }
            }
        }
    }

    return 0;

}

static int GotoPreset(struct soap * pstSoap, int channel, const char * acHttpAddr, const char * profile, int iPresetNo)
{
    int i = 0, num = 0;
    struct _tptz__GotoPreset stGotoPreset;
    struct _tptz__GotoPresetResponse stGotoPresetResponse;
    struct _tptz__GetPresets stGetPresets;
    struct _tptz__GetPresetsResponse stGetPresetsResponse;
    struct tt__PTZSpeed stSpeed;
    char   acPresetToken[ONVIF_STRING_LEN] = {0x0};
    char   acProfile[ONVIF_STRING_LEN]     = {0x0};
    
    memset(&stGetPresets, 0x0, sizeof(struct _tptz__GetPresets));
    memset(&stGetPresetsResponse, 0x0, sizeof(struct _tptz__GetPresetsResponse));
    memset(acProfile, 0x0, ONVIF_STRING_LEN);

    strncpy(acProfile, profile, ONVIF_AUTHOR_STR_LEN - 1);

    stGetPresets.ProfileToken = acProfile;

    CamAuthorized(pstSoap, channel);
    soap_call___tptz__GetPresets(pstSoap, acHttpAddr, NULL, &stGetPresets , &stGetPresetsResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    memset(acPresetToken, 0x0, ONVIF_STRING_LEN);
    snprintf(acPresetToken, ONVIF_STRING_LEN, "%d", iPresetNo);
    num = stGetPresetsResponse.__sizePreset;
    for ( i = 0; i < num; i++ )
    {
        if ( 0 == strcmp(acPresetToken, stGetPresetsResponse.Preset[i].token) )
        {
            stGotoPreset.PresetToken = acPresetToken;
            break;
        }
    }

    if ( i == num )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "Preset No [%d] not exsit\n", iPresetNo);
        return -1;
    }

    stGotoPreset.ProfileToken = acProfile;
    soap_default_tt__PTZSpeed(pstSoap, &stSpeed);
    stGotoPreset.Speed = &stSpeed;
    
    DebugPrint(ONVIF_DEBUG_LEVEL, "%s: profile=%s, preset=%s\n", __FUNCTION__, profile, stGotoPreset.PresetToken);
    CamAuthorized(pstSoap, channel);
    soap_call___tptz__GotoPreset(pstSoap, acHttpAddr, NULL, &stGotoPreset, &stGotoPresetResponse );
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    return 0;
}

static int DelPreset(struct soap * pstSoap, int channel, const char * acHttpAddr, char * profile, int iPresetNo)
{
    char acToken[ONVIF_STRING_LEN] = {0x0};
    struct _tptz__RemovePreset sttptz__RemovePreset;
    struct _tptz__RemovePresetResponse sttptz__RemovePresetResponse;

    memset(acToken, 0x0, ONVIF_STRING_LEN);
    sprintf(acToken, "%d", iPresetNo);

    sttptz__RemovePreset.ProfileToken = profile;
    sttptz__RemovePreset.PresetToken  = acToken;

    CamAuthorized(pstSoap, channel);
    soap_call___tptz__RemovePreset(pstSoap, acHttpAddr, NULL, &sttptz__RemovePreset, &sttptz__RemovePresetResponse);

    return 0;
}

static int GetImageParam(struct soap * pstSoap, const int channel, char * pacVideoSrc, const char * acHttpAddr, ST_ONVIF_IMG_PARAM * pstIMGParam)
{
    char   acVideoSrc[ONVIF_STRING_LEN] = {0x0};
    struct _timg__GetImagingSettings stGetIMGSetting;
    struct _timg__GetImagingSettingsResponse stResponse;

    memset(&stGetIMGSetting, 0x0, sizeof(struct _timg__GetImagingSettings));
    memset(acVideoSrc, 0x0, ONVIF_AUTHOR_STR_LEN);
    strncpy(acVideoSrc, pacVideoSrc, ONVIF_AUTHOR_STR_LEN);
    stGetIMGSetting.VideoSourceToken = acVideoSrc;

    CamAuthorized(pstSoap, channel);
    soap_call___timg__GetImagingSettings(pstSoap, acHttpAddr, NULL, &stGetIMGSetting, &stResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    if ( stResponse.ImagingSettings != NULL )
    {
        if ( NULL != stResponse.ImagingSettings->Brightness )
        {
            pstIMGParam->uiBrightness = (unsigned int)(*(stResponse.ImagingSettings->Brightness));
        }
        else
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "ONVIF-%s: Brigthness Error\n", __FUNCTION__);
        }
        
        if ( NULL != stResponse.ImagingSettings->ColorSaturation )
        {
            pstIMGParam->uiColorSaturation = (unsigned int)(*(stResponse.ImagingSettings->ColorSaturation));
        }
        else
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "ONVIF-%s: ColorSaturation Error\n", __FUNCTION__);
        }
        
        if ( NULL != stResponse.ImagingSettings->Contrast )
        {
            pstIMGParam->uiContrast = (unsigned int)(*(stResponse.ImagingSettings->Contrast));
        }
        else
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "ONVIF-%s: Contrast Error\n", __FUNCTION__);
        }
    }
    else
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "ONVIF-%s: ImagingSettings Error\n", __FUNCTION__);
        return -1;
    }
    
    return 0;
}

static int SetImageParam(struct soap * pstSoap, const int channel, char * pacVideoSrc, const char * acHttpAddr, ST_ONVIF_IMG_PARAM * pstIMGParam)
{
    struct _timg__SetImagingSettings stImg;
    struct tt__ImagingSettings20     stImagingSettings;
    struct _timg__SetImagingSettingsResponse stResponse;
    float fBrightness      = 0.0;
    float fColorSaturation = 0.0;
    float fContrast        = 0.0;
    char   acVideoSrc[ONVIF_STRING_LEN] = {0x0};

    memset(&stImagingSettings, 0x0, sizeof(struct tt__ImagingSettings20));
    fBrightness      = (float)(pstIMGParam->uiBrightness);
    fColorSaturation = (float)(pstIMGParam->uiColorSaturation);
    fContrast        = (float)(pstIMGParam->uiContrast);
    stImagingSettings.Brightness      = &fBrightness;
    stImagingSettings.ColorSaturation = &fColorSaturation;
    stImagingSettings.Contrast        = &fContrast;

    memset(&stImg, 0x0, sizeof(struct _timg__SetImagingSettings));
    memset(acVideoSrc, 0x0, ONVIF_AUTHOR_STR_LEN);
    strncpy(acVideoSrc, pacVideoSrc, ONVIF_AUTHOR_STR_LEN);
    stImg.VideoSourceToken = acVideoSrc;
    stImg.ImagingSettings  = &stImagingSettings;
    
    memset(&stResponse, 0x0, sizeof(struct _timg__SetImagingSettingsResponse));

    CamAuthorized(pstSoap, channel);
    soap_call___timg__SetImagingSettings(pstSoap, acHttpAddr, NULL, &stImg, &stResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    return 0;
}



#if 0
static int AbsoluteMove(struct soap * pstSoap, const char * acHttpAddr, const char * actoken)
{
    struct tt__Vector2D  stPanTilt;	/* optional element of type tt:Vector2D */
	struct tt__Vector1D  stZoom;	/* optional element of type tt:Vector1D */
    struct tt__PTZVector stPosition;
	struct tt__PTZSpeed  stSpeed;	
    struct _tptz__AbsoluteMove sttptz__AbsoluteMove;
    struct _tptz__AbsoluteMoveResponse sttptz__AbsoluteMoveResponse;

    stPanTilt.x = 0;
    stPanTilt.y = 0;
    stPanTilt.space = NULL;

    stZoom.x = 0;
    stZoom.space = NULL;

    stPosition.PanTilt = &stPanTilt;
    stPosition.Zoom    = &stZoom;

    stPanTilt.x = 0;
    stPanTilt.y = 0;
    stPanTilt.space = NULL;
    stZoom.x = 0;
    stZoom.space = NULL;

    stSpeed.PanTilt = &stPanTilt;
    stSpeed.Zoom    = &stZoom;
    
    sttptz__AbsoluteMove.ProfileToken = actoken;
    sttptz__AbsoluteMove.Position     = &stPosition;
    sttptz__AbsoluteMove.Speed        = &stSpeed;
    
    memset(&sttptz__AbsoluteMoveResponse, 0x0, sizeof(struct _tptz__AbsoluteMoveResponse));
    
    soap_call___tptz__AbsoluteMove(pstSoap, acHttpAddr, NULL, &sttptz__AbsoluteMove, &sttptz__AbsoluteMoveResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }
    return 0;
}

static int RelativeMove(struct soap * pstSoap, const char * acHttpAddr, const char * actoken)
{
    struct tt__Vector2D  stPanTilt;	/* optional element of type tt:Vector2D */
	struct tt__Vector1D  stZoom;	/* optional element of type tt:Vector1D */
    struct tt__PTZVector stTranslation;
    struct tt__PTZSpeed  stSpeed;
    struct _tptz__RelativeMove stRelativeMove;
    struct _tptz__RelativeMoveResponse stRelativeMoveResponse;

    stPanTilt.x = 0;
    stPanTilt.y = 0;
    stPanTilt.space = NULL;

    stZoom.x = 0;
    stZoom.space = NULL;

    stTranslation.PanTilt = &stPanTilt;
    stTranslation.Zoom    = &stZoom;

    stPanTilt.x = 0;
    stPanTilt.y = 0;
    stPanTilt.space = NULL;
    stZoom.x = 0;
    stZoom.space = NULL;

    stSpeed.PanTilt = &stPanTilt;
    stSpeed.Zoom    = &stZoom;

    stRelativeMove.ProfileToken = actoken;
    stRelativeMove.Translation  = &stTranslation;
    stRelativeMove.Speed        = &stSpeed;
    soap_call___tptz__RelativeMove(pstSoap, acHttpAddr, NULL,  &stRelativeMove, &stRelativeMoveResponse);
    
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }
    return 0;
}
#endif

static int StartContinuousMove(struct soap * pstSoap, const int channel, const char * acHttpAddr, const char * profile, E_ONVIF_MOVE_TYPE eMove)
{
    struct tt__PTZSpeed  stPTZSpeed;
    struct tt__Vector2D  stPanTilt;
    struct _tptz__ContinuousMove stContinuousMove;
    struct _tptz__ContinuousMoveResponse stContinuousMoveResponse;
    char   acProfile[ONVIF_STRING_LEN] = {0x0};

    memset(&stPTZSpeed, 0x0, sizeof(struct tt__PTZSpeed));
    memset(&stPanTilt,  0x0, sizeof(struct tt__Vector2D));
    memset(&stContinuousMove, 0x0, sizeof(struct _tptz__ContinuousMove));
    memset(&stContinuousMoveResponse, 0x0, sizeof(struct _tptz__ContinuousMoveResponse));

    switch (eMove)
    {
        case E_OMT_UP:
            stPanTilt.y = 0.5;
            break;
        case E_OMT_DN:
            stPanTilt.y = -0.5;
            break;
        case E_OMT_RT:
            stPanTilt.x = 0.5;
            break;
        case E_OMT_LT:
            stPanTilt.x = -0.5;
            break;
        default:
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
            return E_OAR_INPUT_ERR;
    }

    strncpy(acProfile, profile, ONVIF_STRING_LEN - 1);
    stContinuousMove.ProfileToken = acProfile;
    stPTZSpeed.PanTilt = &stPanTilt;
    stContinuousMove.Velocity = &stPTZSpeed;

    CamAuthorized(pstSoap, channel);
    soap_call___tptz__ContinuousMove(pstSoap, acHttpAddr, NULL, &stContinuousMove , &stContinuousMoveResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }
    
    return 0;
}

static int StopContinuousMove(struct soap * pstSoap, const int channel, const char * acHttpAddr, const char * profile)
{
    char   acProfile[ONVIF_STRING_LEN] = {0x0};
    struct _tptz__Stop         stStop;
    struct _tptz__StopResponse stStopResponse;
    enum   xsd__boolean bPanTilt, bZoom;

    bPanTilt = xsd__boolean__true_;
    bZoom    = xsd__boolean__false_;

    strncpy(acProfile, profile, ONVIF_STRING_LEN - 1);
    stStop.ProfileToken = acProfile;
    stStop.PanTilt = &bPanTilt;
    stStop.Zoom    = &bZoom;

    CamAuthorized(pstSoap, channel);
    soap_call___tptz__Stop(pstSoap, acHttpAddr, NULL, &stStop, &stStopResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    return 0;
    
}

static int StartContinuousZoom(struct soap * pstSoap, const int channel, const char * acHttpAddr, const char * profile, E_ONVIF_ZOOM_TYPE eZoom)
{
    struct tt__PTZSpeed  stPTZSpeed;
    struct tt__Vector1D  stZoom;
    struct _tptz__ContinuousMove stContinuousMove;
    struct _tptz__ContinuousMoveResponse stContinuousMoveResponse;
    char   acProfile[ONVIF_STRING_LEN] = {0x0};

    memset(&stPTZSpeed, 0x0, sizeof(struct tt__PTZSpeed));
    memset(&stZoom,  0x0, sizeof(struct tt__Vector1D));
    memset(&stContinuousMove, 0x0, sizeof(struct _tptz__ContinuousMove));
    memset(&stContinuousMoveResponse, 0x0, sizeof(struct _tptz__ContinuousMoveResponse));

    switch (eZoom)
    {
        case E_OZT_IN:
            stZoom.x = -0.5;
            break;
        case E_OZT_OUT:
            stZoom.x = 0.5;
            break;
        default:
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
            return E_OAR_INPUT_ERR;
    }

    strncpy(acProfile, profile, ONVIF_STRING_LEN - 1);
    stContinuousMove.ProfileToken = acProfile;
    stPTZSpeed.Zoom = &stZoom;
    stContinuousMove.Velocity = &stPTZSpeed;

    CamAuthorized(pstSoap, channel);
    soap_call___tptz__ContinuousMove(pstSoap, acHttpAddr, NULL, &stContinuousMove , &stContinuousMoveResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }
    
    return 0;
}

static int StopContinuousZoom(struct soap * pstSoap, const int channel, const char * acHttpAddr, const char * profile)
{
    char   acProfile[ONVIF_STRING_LEN] = {0x0};
    struct _tptz__Stop         stStop;
    struct _tptz__StopResponse stStopResponse;
    enum   xsd__boolean bPanTilt, bZoom;

    bZoom    = xsd__boolean__true_;
    bPanTilt = xsd__boolean__false_;

    strncpy(acProfile, profile, ONVIF_STRING_LEN - 1);
    stStop.ProfileToken = acProfile;
    stStop.PanTilt = &bPanTilt;
    stStop.Zoom    = &bZoom;

    CamAuthorized(pstSoap, channel);
    soap_call___tptz__Stop(pstSoap, acHttpAddr, NULL, &stStop, &stStopResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    return 0;
    
}

static int StartContinuousFocus(struct soap * pstSoap, const int channel, const char * acHttpAddr, const char * acSrcToken, E_ONVIF_FOCUS_TYPE eFocus)
{
    struct tt__ContinuousFocus stFocus;
    struct tt__FocusMove stFocusMove;
    struct _timg__Move   stMove;
    struct _timg__MoveResponse stMoveResponse;
    char   acToken[ONVIF_STRING_LEN] = {0x0};

    switch (eFocus)
    {
        case E_OFT_FAR:
            stFocus.Speed = 1;
            break;
        case E_OFT_NEAR:
            stFocus.Speed = -1;
            break;
        default:
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
            return E_OAR_INPUT_ERR;
    }

    soap_default_tt__FocusMove(pstSoap, &stFocusMove);
    soap_default__timg__Move(pstSoap, &stMove);
    
    stFocusMove.Continuous = &stFocus;
    stMove.Focus = &stFocusMove;

    memset(acToken, 0x0, ONVIF_STRING_LEN);
    strncpy(acToken, acSrcToken, ONVIF_STRING_LEN - 1);
    stMove.VideoSourceToken = acToken;


    DebugPrint(ONVIF_DEBUG_LEVEL, "HttpAddr:%s, SrcToken:%s\n", acHttpAddr, acSrcToken);
    
    CamAuthorized(pstSoap, channel);
    soap_call___timg__Move(pstSoap, acHttpAddr, NULL,  &stMove, &stMoveResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }
    
    return 0;
}

static int StopContinuousFocus(struct soap * pstSoap, const int channel, const char * acHttpAddr,  const char * acSrcToken)
{
    struct _timg__Stop stStop;
    struct _timg__StopResponse stStopResponse;
    char   acStr[ONVIF_STRING_LEN];

    memset((void *)&stStop, 0x0, sizeof(struct _timg__Stop));
    memset((void *)&stStopResponse, 0x0, sizeof(struct _timg__StopResponse));

    memset(acStr, 0x0, ONVIF_STRING_LEN);
    strncpy(acStr, acSrcToken, ONVIF_STRING_LEN);
    stStop.VideoSourceToken = acStr;

    DebugPrint(ONVIF_DEBUG_LEVEL, "%s: HttpAddr:%s, SrcToken:%s\n", __FUNCTION__,acHttpAddr, acSrcToken);
    
    CamAuthorized(pstSoap, channel);
    soap_call___timg__Stop(pstSoap, acHttpAddr, NULL, &stStop,  &stStopResponse);
    if ( pstSoap->error )
    {
        ONVIF_ERROR_PRINT(pstSoap);
        return pstSoap->error;
    }

    return 0;
    
}

static int FinishCamInfo(struct soap * pstSoap, int channel, ST_ONVIF_CAM_SERVICE_ADDRS * pstServerAddrs, ST_ONVIF_CAMINFO_SET * pstCamInfo)
{
    int iRet = 0;
    struct _trt__GetProfilesResponse trt__GetProfilesResponse;
    //char   acNetAddr[SER_NET_ADDR_LEN] = {0x0};
    char   acUril[SER_NET_ADDR_LEN] = {0x0}; 

    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel < 0) || ( NULL == pstCamInfo) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    if ( 0 == strlen(pstServerAddrs->acMediaServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    memset(&trt__GetProfilesResponse, 0x0, sizeof(struct _trt__GetProfilesResponse));

    CamAuthorized(pstSoap, channel);
    iRet = GetProfiles(pstSoap, pstServerAddrs->acMediaServiceAddr, &trt__GetProfilesResponse);
    if ( 0 != iRet )
    {
        //printf("GetProfiles Error: [Media addr]%s\n",pstServerAddrs->acMediaServiceAddr);
        
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ��ȡprofiles */
        CamAuthorized(pstSoap, channel);
        iRet = GetProfiles(pstSoap, pstServerAddrs->acCamHttpAddr, &trt__GetProfilesResponse);
        if ( 0 != iRet )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "GetProfiles Error: [device addr]%s\n",pstServerAddrs->acCamHttpAddr); 
            pstCamInfo->eValid = ONVIF_FALSE;
            return E_OAR_SOAP_ERR;
        }
    }

    /* ������ */
    pstCamInfo->stCamMainVideo.eFixed      = (ONVIF_BOOL)trt__GetProfilesResponse.Profiles[0].fixed;
    pstCamInfo->stCamMainVideo.iEncBitRate = trt__GetProfilesResponse.Profiles[0].VideoEncoderConfiguration->RateControl->BitrateLimit;
    pstCamInfo->stCamMainVideo.iFps        = trt__GetProfilesResponse.Profiles[0].VideoEncoderConfiguration->RateControl->FrameRateLimit;
    pstCamInfo->stCamMainVideo.iWidth      = trt__GetProfilesResponse.Profiles[0].VideoEncoderConfiguration->Resolution->Width;
    pstCamInfo->stCamMainVideo.iHeight     = trt__GetProfilesResponse.Profiles[0].VideoEncoderConfiguration->Resolution->Height;        
    /* ��strncpy���Ӱ�ȫ������󿽱��ִ�����Ԥ��1�������� */
    strncpy(pstCamInfo->stCamMainVideo.acProfile, trt__GetProfilesResponse.Profiles[0].token, ONVIF_STRING_LEN - 1);

    strncpy(pstCamInfo->stCamMainVideo.acSrcToken, trt__GetProfilesResponse.Profiles[0].VideoSourceConfiguration->SourceToken, ONVIF_STRING_LEN - 1);

    memset(acUril, 0x0, SER_NET_ADDR_LEN);
    CamAuthorized(pstSoap, channel);
    iRet = GetRtspUril(pstSoap, trt__GetProfilesResponse.Profiles[0].token, pstServerAddrs->acMediaServiceAddr, acUril);
    if ( 0 != iRet )
    {
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ��ȡRTSP��ַ */
        CamAuthorized(pstSoap, channel);
        GetRtspUril(pstSoap, trt__GetProfilesResponse.Profiles[0].token, pstServerAddrs->acCamHttpAddr, acUril);
    }
    strcpy(pstCamInfo->stCamMainVideo.acRtspUri, acUril);


    memset(acUril, 0x0, SER_NET_ADDR_LEN);
    CamAuthorized(pstSoap, channel);
    iRet = GetSnapShortUril(pstSoap, trt__GetProfilesResponse.Profiles[0].token, pstServerAddrs->acMediaServiceAddr, acUril);
    if ( 0 != iRet )
    {
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ��ȡץ�ĵ�ַ */
        CamAuthorized(pstSoap, channel);
        GetSnapShortUril(pstSoap, trt__GetProfilesResponse.Profiles[0].token, pstServerAddrs->acCamHttpAddr, acUril);
    }
    strcpy(pstCamInfo->stCamMainVideo.acSnapShortUri, acUril);
    
    /* ������ */
    if ( trt__GetProfilesResponse.__sizeProfiles < 2 )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "No SubStream Exsit\n");
        return E_OAR_OTHER_ERR;
    }
    pstCamInfo->stCamSubVideo.eFixed      = (ONVIF_BOOL)trt__GetProfilesResponse.Profiles[1].fixed;
    pstCamInfo->stCamSubVideo.iEncBitRate = trt__GetProfilesResponse.Profiles[1].VideoEncoderConfiguration->RateControl->BitrateLimit;
    pstCamInfo->stCamSubVideo.iFps        = trt__GetProfilesResponse.Profiles[1].VideoEncoderConfiguration->RateControl->FrameRateLimit;
    pstCamInfo->stCamSubVideo.iWidth      = trt__GetProfilesResponse.Profiles[1].VideoEncoderConfiguration->Resolution->Width;
    pstCamInfo->stCamSubVideo.iHeight     = trt__GetProfilesResponse.Profiles[1].VideoEncoderConfiguration->Resolution->Height;
    /* ��strncpy���Ӱ�ȫ������󿽱��ִ�����Ԥ��1�������� */
    strncpy(pstCamInfo->stCamSubVideo.acProfile, trt__GetProfilesResponse.Profiles[1].token, ONVIF_STRING_LEN - 1);

    strncpy(pstCamInfo->stCamSubVideo.acSrcToken, trt__GetProfilesResponse.Profiles[1].VideoSourceConfiguration->SourceToken, ONVIF_STRING_LEN - 1);

    memset(acUril, 0x0, SER_NET_ADDR_LEN);
    CamAuthorized(pstSoap, channel);
    iRet = GetRtspUril(pstSoap, trt__GetProfilesResponse.Profiles[1].token, pstServerAddrs->acMediaServiceAddr, acUril);
    if ( 0 != iRet ) 
    {
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ��ȡRTSP��ַ */
        CamAuthorized(pstSoap, channel);
        GetRtspUril(pstSoap, trt__GetProfilesResponse.Profiles[1].token, pstServerAddrs->acCamHttpAddr, acUril);
    }
    strcpy(pstCamInfo->stCamSubVideo.acRtspUri, acUril);

    memset(acUril, 0x0, SER_NET_ADDR_LEN);
    CamAuthorized(pstSoap, channel);
    iRet = GetSnapShortUril(pstSoap, trt__GetProfilesResponse.Profiles[1].token, pstServerAddrs->acMediaServiceAddr, acUril);
    if ( 0 != iRet )
    {
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ��ȡץ�ĵ�ַ */
        CamAuthorized(pstSoap, channel);
        GetSnapShortUril(pstSoap, trt__GetProfilesResponse.Profiles[1].token, pstServerAddrs->acCamHttpAddr, acUril);
    }
    strcpy(pstCamInfo->stCamSubVideo.acSnapShortUri, acUril);

    /* ���������ַ */
    memcpy(&(pstCamInfo->stServiceAddrs), pstServerAddrs, sizeof(ST_ONVIF_CAM_SERVICE_ADDRS));
    
    pstCamInfo->eValid = ONVIF_TRUE;

    /* ��ȡԤ��λ��Ϣ */
    /*
    if ( 0 != strlen(pstServerAddrs->acPTZServiceAddr) )
    {
        CamAuthorized(m_pstSoap, channel);
        GetAllPresets(m_pstSoap, pstServerAddrs->acPTZServiceAddr, 
                      trt__GetProfilesResponse.Profiles[0].token, &g_CamPresetInfo[channel]);
        
    }
    */

    if ( 0 != strlen(pstServerAddrs->acPTZServiceAddr) )
    {
        CamAuthorized(pstSoap, channel);
        iRet = GetAllPresets(pstSoap, pstServerAddrs->acPTZServiceAddr,trt__GetProfilesResponse.Profiles[0].token);
        if ( 0 != iRet )
        {
            CamAuthorized(pstSoap, channel);
            GetAllPresets(pstSoap, pstServerAddrs->acCamHttpAddr,trt__GetProfilesResponse.Profiles[0].token);
        }
    }
    
    return E_OAR_OK;
}

static int GetSnapShort(struct soap * pstSoap, const char * acHttpAddr)
{
    //soap_get_http_body();
    return 0;
}

static void * ProbeThreadProc(void * arg)
{
    int  num        = 0;
    int  i          = 0;
    int  j          = 0;
    int  iMapNum    = 0;
    unsigned int    uiRandVal = 0;
    struct wsdd__ProbeType        stProbeType;  
    struct __wsdd__ProbeMatches   resp; 
    //struct wsdd__ProbeMatchesType stwsdd__ProbeMatches;
    struct wsdd__ScopesType       stScope;  
    struct SOAP_ENV__Header       header; 
    ST_ONVIF_CAMINFO_SET          stCamInfo;
    ST_ONVIF_CAM_SERVICE_ADDRS    stServiceAddrs;
    char   acHttpAddr[SER_NET_ADDR_LEN] = {0x0};
    char   acStrIP[32] = {0x0};
    char * pacTempCamHttpAddr[MAX_ONVIF_CAM_NUM] = {NULL};
    char * pTemp = NULL;
    char * strScope_item  = "";
    char * strScope_Match = "";
    char * strProbeType_Types = "";
    char * dn = "dn:NetworkVideoTransmitter";
    char str_wsa__to[]     = "urn:schemas-xmlsoap-org:ws:2005:04:discovery";
    char str_wsa__Action[] = "http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe";
    char uuidstr[128];
    ONVIF_BOOL bTmpValid   = ONVIF_FALSE;
    struct soap *pSoap = NULL;
    
    pthread_detach(pthread_self());

#if 0
    /* �������е�����ͷ����Ϊ��Ч */
    pthread_mutex_lock(&s_CamParamMutex);
    for ( i = 0; i < MAX_ONVIF_CAM_NUM; i++ )
    {
        g_CamInfoSet[i].eValid = ONVIF_FALSE;
    }
    pthread_mutex_unlock(&s_CamParamMutex);
#endif    

    //m_pstSoap->recv_timeout = 3;     
    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        return NULL;
    }

    soap_set_namespaces(pSoap, namespaces_discovery);
    
    soap_default_SOAP_ENV__Header(pSoap, &header);  
#if 1

    srand((int)time(0));
	uiRandVal = rand();
    DebugPrint(ONVIF_DEBUG_LEVEL, "rand: %08x\n",uiRandVal);

    memset(uuidstr, 0x0, 128);
    sprintf(uuidstr, "uuid:%08x-4ebb-49ef-8aec-a78814bb43ff", uiRandVal);
    DebugPrint(ONVIF_DEBUG_LEVEL, "uuid: %s\n", uuidstr);

    header.wsa__MessageID = uuidstr;

#else
    /* ����������Ϣ�е�MessageID, ����������ε�MessageID����Ļ�, �󻪵�����ͷ���ܻ��Ѳ��� */
    if ( s_DiscovryUuidCnt % 2 == 0 )
    {
        header.wsa__MessageID = roStrUuidString1;
    }
    else
    {
        header.wsa__MessageID = roStrUuidString2;
    }
    s_DiscovryUuidCnt++;
#endif    
    
    header.wsa__To     = str_wsa__to;  
    header.wsa__Action = str_wsa__Action; 
    pSoap->header = &header;  

    memset((void *)&stScope, 0x0, sizeof(struct wsdd__ScopesType) );
    soap_default_wsdd__ScopesType(pSoap, &stScope);  
    stScope.__item  = strScope_item;
    stScope.MatchBy = strScope_Match;
    memset( (void *)&stProbeType, 0x0, sizeof(struct wsdd__ProbeType) );
    soap_default_wsdd__ProbeType(pSoap, &stProbeType); 
    #if 0
    stProbeType.Scopes = &stScope;  
    stProbeType.Types  = strProbeType_Types;  
    #else
    stProbeType.Scopes = NULL;  
    stProbeType.Types  = dn;  
    #endif

  
    soap_send___wsdd__Probe(pSoap, "soap.udp://239.255.255.250:3702", NULL, &stProbeType); 
    //soap_send___wsdd__ProbeMatches(m_pstSoap, "soap.udp://239.255.255.250:3702", NULL, &stwsdd__ProbeMatches);

    /* �����豸 */
    do{ 
                
        soap_recv___wsdd__ProbeMatches(pSoap, &resp);   
        if (pSoap->error)   
        {   
            //ONVIF_ERROR_PRINT(m_pstSoap);
            DebugPrint(ONVIF_DEBUG_LEVEL, "Device Probe Break! SoapRtn[%d], faultcode[%s], faultstring[%s]\n", 
                    pSoap->error, *soap_faultcode(pSoap), *soap_faultstring(pSoap));
            break;  
        }   
        else  
        {  
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s:%d\n", __FUNCTION__, __LINE__);
            if (NULL == resp.wsdd__ProbeMatches)
            {
                 continue;
			}
DebugPrint(ONVIF_DEBUG_LEVEL, "%s:%d\n", __FUNCTION__, __LINE__);
            pacTempCamHttpAddr[num] = (char *)malloc(strlen(resp.wsdd__ProbeMatches->ProbeMatch->XAddrs) + 1 );
            if ( NULL == pacTempCamHttpAddr[num] )
            {
                DebugPrint(ONVIF_DEBUG_LEVEL, "Malloc Memory Error\n");
                for ( i = 0; i < num; i++ )
                {
                    free(pacTempCamHttpAddr[i]);
                    pacTempCamHttpAddr[i] = NULL;
                }
                DestroySoap(pSoap);
				
                return (void *)0;
            }
            memset(pacTempCamHttpAddr[num], 0x0, strlen(resp.wsdd__ProbeMatches->ProbeMatch->XAddrs) + 1);
            
            //strcpy(pacTempCamHttpAddr[num], resp.wsdd__ProbeMatches->ProbeMatch->XAddrs);
            //sscanf(resp.wsdd__ProbeMatches->ProbeMatch->XAddrs,"%s %*s", pacTempCamHttpAddr[num]);
            pTemp = strtok(resp.wsdd__ProbeMatches->ProbeMatch->XAddrs," ");
            /* ��Ҫ�Ƚ��豸��ַ����������strtok�������޸��ַ����ĵ�ַ */
            strcpy(pacTempCamHttpAddr[num], pTemp);
            DebugPrint(ONVIF_DEBUG_LEVEL, "probe addr = %s\n", pTemp);
            memset(acStrIP, 0x0, 32);

            /* ��ȡIP */
            if ( 1 == GetIPAddrFromHttpAddr(pTemp, acStrIP) )
            {
                /* ����: �����IP��Ӧ���������ϲ�Ӧ�õ��б����������������в��Ҳ��� */
                
                if (s_fCBIpFilter)
                {
                    if ( 0 == s_fCBIpFilter(acStrIP) )
                    {
                        DebugPrint(ONVIF_DEBUG_LEVEL, "Filter device: %s\n", pacTempCamHttpAddr[num]);
                        free(pacTempCamHttpAddr[num]);
                        pacTempCamHttpAddr[num] = NULL; 
                        continue;
                    }
                }
            }
            else
            {
                DebugPrint(ONVIF_DEBUG_LEVEL, "Get IP Error! Ignore device: %s\n", pacTempCamHttpAddr[num]);
                free(pacTempCamHttpAddr[num]);
                pacTempCamHttpAddr[num] = NULL;    
                continue;
            }
            
            num++;

            if ( num >= MAX_ONVIF_CAM_NUM )
            {
                DebugPrint(ONVIF_DEBUG_LEVEL, "%s: Too Many Cameras\n", __FUNCTION__);
                break;
            }
            
        }  
    }while(1);  

    /* ӳ������ͷͨ�� */
    iMapNum = 0;

    for ( i = 0; i < MAX_ONVIF_CAM_NUM; i++ )
    {
        /* ���������ͨ���ĸ��±�־����ƥ����http��ַ��û����λ������ͷ��ʾ����probeû�б����� */
        m_stCamServices[i].eDiscoverFlg = ONVIF_FALSE;
        
        for ( j = 0; j < num; j++ )
        {
            if ( NULL == pacTempCamHttpAddr[j] )
            {
                continue;
            }
            /* ��ʾ֮ǰ��probe�Ѿ����֣����Ҵ˴�probe�ַ����ˣ����ʱ����Ҫ�ٸı���ͨ����� */
            if ( 0 == strcmp(m_stCamServices[i].acCamHttpAddr, pacTempCamHttpAddr[j]) )
            {
                iMapNum++;
                m_stCamServices[i].eDiscoverFlg = ONVIF_TRUE;

                /** 
                ���ﲻ��Ҫ��������ͷ��Ϣ�Ŀ��ñ�־������ᵼ���������������ͷ��
                ��������ͷ��ȡ��Ϣʧ�ܣ��ٴη���ʱ����ȥ������Ϣ������
                **/
                #if 0   
                /* �ϴη��ֵ�����ͷ������з����ˣ��Ͳ���ȥ���»�ȡ����ͷ���ݣ�����ֱ�ӿ��� */
                pthread_mutex_lock(&s_CamParamMutex);
                g_CamInfoSet[i].eValid = ONVIF_TRUE;
                pthread_mutex_unlock(&s_CamParamMutex);
                #endif

                free(pacTempCamHttpAddr[j]);
                pacTempCamHttpAddr[j] = NULL;
                break;
            }
        }
    }

    for ( i = 0; i < num; i++ )
    {
        /* ��ʾ������ͷ��֮ǰ���ֵ�һ�£��������ӳ��������Ѿ��ͷ��˿ռ� */
        if ( NULL == pacTempCamHttpAddr[i] )
        {
            continue;
        }
        
        for ( j = 0; j < MAX_ONVIF_CAM_NUM; j++ )
        {
            if ( ONVIF_TRUE == m_stCamServices[j].eDiscoverFlg )
            {
                continue;
            }

            //printf("Inset New Camera: j = %d\n", j);

            memset(m_stCamServices[j].acCamHttpAddr, 0x0, SER_NET_ADDR_LEN);
            strcpy(m_stCamServices[j].acCamHttpAddr, pacTempCamHttpAddr[i]);
            m_stCamServices[j].eDiscoverFlg = ONVIF_TRUE;

            /* ���ñ���ϴδ��ڿ��õ�����ͷ��Ϣʱ���Ҹ���ǰ���������ͷ��ַ��һ��
               ʱ����Ҫ���»�ȡ��ǰ�²�������ͷ����Ϣ */
            pthread_mutex_lock(&s_CamParamMutex);
            if ( ONVIF_TRUE == g_CamInfoSet[j].eValid ) 
            {
                g_CamInfoSet[j].eValid = ONVIF_FALSE;
            }
            pthread_mutex_unlock(&s_CamParamMutex);
            
            iMapNum++;
            
            free(pacTempCamHttpAddr[i]);
            pacTempCamHttpAddr[i] = NULL;

            break;
        }
    }


    if ( iMapNum < num )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "Remain some Cameras not map\n");

        for ( i = 0; i < num; i++ )
        {
            if ( NULL == pacTempCamHttpAddr[i] )
            {
                continue;
            }
            free(pacTempCamHttpAddr[i]);
            pacTempCamHttpAddr[i] = NULL;
        }
    }

    soap_set_namespaces(pSoap, namespaces);

    /* ��ȡ����ͷ�����ַ */
    for ( i = 0; i < MAX_ONVIF_CAM_NUM; i++ )
    {
        /* ���û�б�������Ҫ��շ����ַ��Ϣ */
        if ( ONVIF_FALSE == m_stCamServices[i].eDiscoverFlg )
        {
            memset(&(m_stCamServices[i]), 0x0, sizeof(ST_ONVIF_CAM_SERVICES));
            
            /* 
               ͬʱ��Ҫ�������ͷ����Ϣ������ĳ������ͷ���������ӱ�Ϊ�Ͽ���
               ���ᱣ������ͷ����Ϣ 
            */
            pthread_mutex_lock(&s_CamParamMutex);
            memset(&(g_CamInfoSet[i]), 0x0, sizeof(ST_ONVIF_CAMINFO_SET));
            pthread_mutex_unlock(&s_CamParamMutex);
            continue;
        }

        /* ���������������ϴ�ͬһ���豸����Ҫ�����»�ȡ����ͷ����Ϣ */
        pthread_mutex_lock(&s_CamParamMutex);
        bTmpValid = g_CamInfoSet[i].eValid;
        pthread_mutex_unlock(&s_CamParamMutex);
        if ( ONVIF_TRUE == bTmpValid )
        {
            //printf("Camera[%d] Infomation is OK last time\n", i);
            continue;
        }
        
        memset(acHttpAddr, 0x0, SER_NET_ADDR_LEN);
        strcpy(acHttpAddr, m_stCamServices[i].acCamHttpAddr);

        /* ����Ȩʧ����Ҫ���������ͷ����Ϣ */
        if ( E_OAR_OK != CheckAuthorized(pSoap, acHttpAddr, i) )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "Check Authorized Error: ch = %d, httpaddr = %s\n", i , acHttpAddr);
            memset(&(m_stCamServices[i]), 0x0, sizeof(ST_ONVIF_CAM_SERVICES));
            continue;
        }

        memset(&stServiceAddrs, 0x0, sizeof(ST_ONVIF_CAM_SERVICE_ADDRS));
        GetDeviceSevices(pSoap, acHttpAddr, i, &stServiceAddrs);
        memcpy(stServiceAddrs.acCamHttpAddr, acHttpAddr, SER_NET_ADDR_LEN);

        memcpy(m_stCamServices[i].acDeviceServiceAddr, stServiceAddrs.acDeviceServiceAddr, SER_NET_ADDR_LEN);
        memcpy(m_stCamServices[i].acMediaServiceAddr, stServiceAddrs.acMediaServiceAddr, SER_NET_ADDR_LEN);
        memcpy(m_stCamServices[i].acPTZServiceAddr, stServiceAddrs.acPTZServiceAddr, SER_NET_ADDR_LEN);
        memcpy(m_stCamServices[i].acIMGServiceAddr, stServiceAddrs.acIMGServiceAddr, SER_NET_ADDR_LEN);

        /* ��������ͷ��Ϣ */
        memset(&stCamInfo, 0x0, sizeof(ST_ONVIF_CAMINFO_SET));
        FinishCamInfo(pSoap, i, &stServiceAddrs, &stCamInfo);
        
        pthread_mutex_lock(&s_CamParamMutex);
        memcpy(&(g_CamInfoSet[i]), &stCamInfo, sizeof(ST_ONVIF_CAMINFO_SET));
        pthread_mutex_unlock(&s_CamParamMutex);

        /* ��ȡ����ͷԤ��λ��Ϣ */
        
        
    }

	g_iProbeNum = num;

    DestroySoap(pSoap);
    DebugPrint(ONVIF_DEBUG_LEVEL, "ProbeThread End......\n");
    
    s_iBeThreadRunningState = 0;
    return (void *)0;
}

int PrintServices()
{
    int i = 0;

    DebugPrint(ONVIF_DEBUG_LEVEL, "Start ======================================================\n");
    for ( i = 0; i < MAX_ONVIF_CAM_NUM; i++ )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "Number: %d\n", i);
        DebugPrint(ONVIF_DEBUG_LEVEL, "DiscoverFlag : %s\n", (m_stCamServices[i].eDiscoverFlg)?("TRUE"):("FALSE"));
        DebugPrint(ONVIF_DEBUG_LEVEL, "HttpAddr     : %s\n", m_stCamServices[i].acCamHttpAddr);
        DebugPrint(ONVIF_DEBUG_LEVEL, "DeviceAddr   : %s\n", m_stCamServices[i].acDeviceServiceAddr);
        DebugPrint(ONVIF_DEBUG_LEVEL, "MediaAddr    : %s\n", m_stCamServices[i].acMediaServiceAddr);
        DebugPrint(ONVIF_DEBUG_LEVEL, "PTZAddr      : %s\n", m_stCamServices[i].acPTZServiceAddr);
        DebugPrint(ONVIF_DEBUG_LEVEL, "IMGAddr      : %s\n", m_stCamServices[i].acIMGServiceAddr);
        
    }
    DebugPrint(ONVIF_DEBUG_LEVEL, "End   ======================================================\n");

    return 0;
}

/*******************************************************************************
** �� �� �� :  ONVIF_Init  
** ��  �� :  ��ʼ��
** ��  �� :  
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-02  
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_Init(const char * acUserName, const char * acPassWord, const char * acIPAddr, CB_IP_FILTER fCBIpFilter)
{
    int i = 0;
   // struct in_addr if_req; 


    pthread_mutexattr_t mutexattr;

#if 0
    m_pstSoap = soap_new(); 
    if(m_pstSoap == NULL)  
    {  
        return -1;  
    }  

    /* ��IP��ַ */
    if ( NULL != acIPAddr )
    {
        if_req.s_addr = inet_addr(acIPAddr);
        m_pstSoap->ipv4_multicast_if = (char*)soap_malloc(m_pstSoap, sizeof(struct in_addr)); 
        if ( NULL == m_pstSoap->ipv4_multicast_if )
        {
            printf("%s-%d: Malloc Error\n", __FUNCTION__, __LINE__);
            return -1;
        }
        memset(m_pstSoap->ipv4_multicast_if, 0, sizeof(struct in_addr)); 
        memcpy(m_pstSoap->ipv4_multicast_if, (char*)&if_req, sizeof(if_req));
    }
#endif

if (acIPAddr)
    g_if_req.s_addr = inet_addr(acIPAddr);

    memset((void *)g_CamInfoSet, 0x0, sizeof(ST_ONVIF_CAMINFO_SET) * MAX_ONVIF_CAM_NUM);

    memset((void *)m_stCamServices, 0x0, sizeof(ST_ONVIF_CAM_SERVICES)*MAX_ONVIF_CAM_NUM);

    /* ����ֻ�����û�������������ã������м�Ȩ��ʹ�����ã�ʹ��������CheckAuthoried������ʵ�� */
    memset((void *)m_stCamAuthorize, 0x0, sizeof(ST_ONVIF_CAM_AUTHORIZE)*MAX_ONVIF_CAM_NUM);
    if ( (NULL != acUserName) && (NULL != acPassWord) )
    {
        for ( i = 0; i < MAX_ONVIF_CAM_NUM; i++ )
        {
            SetAuthorUnPw(i, acUserName, acPassWord);
        }
    }

    /* namespaces������wsdd.nsmap�ļ��� */
    //soap_set_namespaces(m_pstSoap, namespaces);

    
   // if ( m_pstSoap->error  )
    //{
    //    ONVIF_ERROR_PRINT(m_pstSoap);
    //    return m_pstSoap->error;
    //}

    s_iBeThreadRunningState = 0;

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_settype(&mutexattr,PTHREAD_MUTEX_TIMED_NP);
    pthread_mutex_init(&s_CamParamMutex,&mutexattr);
    pthread_mutexattr_destroy(&mutexattr);

    s_fCBIpFilter = fCBIpFilter;



    return 0;
}

/*******************************************************************************
** �� �� �� :  ONVIF_Uninit  
** ��  �� :  ȥ��ʼ��
** ��  �� :  
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-02 
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_Uninit(void)
{
    //soap_destroy(m_pstSoap);  
    //soap_end(m_pstSoap);     
    //soap_free(m_pstSoap);  

    pthread_mutex_destroy(&s_CamParamMutex);
    
    return 0;
}

/*******************************************************************************
** �� �� �� :  ONVIF_StartProbe   
** ��  �� :  ����̽��
** ��  �� :  
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-04  
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_StartProbe(void)
{
    int iResult = 0;
    
    pthread_t tid;

    
    /* ����߳��Ѿ�������������״̬���ٴε��øú��������´������߳� */
    if ( 0 == s_iBeThreadRunningState )
    {
        iResult = pthread_create(&tid, NULL, ProbeThreadProc, NULL);

        if ( iResult < 0 )
        {
            DebugPrint(DEBUG_ERROR_PRINT, "Creat Probe Thread Error\n");
        }

        s_iBeThreadRunningState = 1;
    }
    else
    {
        DebugPrint(DEBUG_ERROR_PRINT, "Thread is Running. Ignore create thread\n");
    }

    return iResult;

}

#if 0
/*******************************************************************************
** �� �� �� :  ONVIF_SetCamAuthorized  
** ��    �� :  ���ü�Ȩ��Ϣ
               ���������û�����������Ϊ�գ���Ĭ�ϲ����м�Ȩ
** ��    �� :  
** ��    �� :  
** ��    �� :  
** ��    �� :  xsh 
** ��    �� :  2014-09-04
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_SetCamAuthorized(int channel, const char * acun, const char * acpw)
{
    if ( (MAX_ONVIF_CAM_NUM <= channel) || (channel < 0) )
    {
        return E_OAR_INPUT_ERR;
    }

    if ( ( NULL == acun ) || ( NULL == acpw ) )
    {
        printf("Cam [No %d], Set None Authorized\n", channel);
        m_stCamAuthorize[channel].bAuthor = ONVIF_FALSE;
        return E_OAR_OK;
    }

    m_stCamAuthorize[channel].bAuthor = ONVIF_TRUE;
    strcpy(m_stCamAuthorize[channel].acUserName, acun);
    strcpy(m_stCamAuthorize[channel].acPassWord, acpw);
    
    return E_OAR_OK;
}
#endif

/*******************************************************************************
** �� �� �� :  ONVIF_GetChNum   
** ��  �� :  ��ȡ�ն���������ONVIF����ͷ�ĸ���
** ��  �� :  
** ��  �� :  
** ��  �� :  ����ͷ����
** ��  �� :  xsh 
** ��  �� :  2014-09-02  
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_GetChNum(void)
{
    int i   = 0;
    int num = 0;

    pthread_mutex_lock(&s_CamParamMutex);
    /*for ( i = 0; i < MAX_ONVIF_CAM_NUM; i++ )
    {
        if ( ONVIF_TRUE == g_CamInfoSet[i].eValid )
        {
            num++;
        }
    }*/
    num = g_iProbeNum;
    pthread_mutex_unlock(&s_CamParamMutex);

    return num;
}

/*******************************************************************************
** �� �� �� :  ONVIF_GetChInfo   
** ��  �� :  ����̽�⵽������ͷ����Ż�ȡ����ͷ�Ĳ�����Ϣ
** ��  �� :  channel   ����ͷ���
               pstInfo   ����ͷ������Ϣ��ַ
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-02  
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_GetChInfo(int channel, ST_ONVIF_CAMINFO_SET * pstParam)
{
    if ( (MAX_ONVIF_CAM_NUM <= channel) || (channel < 0) || (NULL == pstParam) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    pthread_mutex_lock(&s_CamParamMutex);
    if ( ONVIF_FALSE == g_CamInfoSet[channel].eValid )
    {
        pthread_mutex_unlock(&s_CamParamMutex);
        DebugPrint(ONVIF_DEBUG_LEVEL, "Camera[NO-%d] Invalid\n", channel);
        return E_OAR_OTHER_ERR;
    }

    memcpy(pstParam, &(g_CamInfoSet[channel]), sizeof(ST_ONVIF_CAMINFO_SET));
    pthread_mutex_unlock(&s_CamParamMutex);

    return E_OAR_OK;
}

/*******************************************************************************
** �� �� �� :  ONVIF_SetCamTime   
** ��  �� :  ����ָ������ͷ��ʱ�䣬��Ҫ������Сʱ�����ӡ����ӡ��ꡢ�¡���
               ʱ�����Ч���е����߱�֤
** ��  �� :  
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-02  
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_SetCamTime(int channel, struct tm * pt)
{
    int  iRet = 0;
    char acDeviceServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    struct soap *pSoap = NULL;

    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel < 0) || ( NULL == pt ) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    memset(acDeviceServiceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acDeviceServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acDeviceServiceAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);

    if ( 0 == strlen(acDeviceServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }

    iRet = SetCameraTime(pSoap, channel, acDeviceServiceAddr, pt);
    if ( 0 != iRet )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
        iRet = E_OAR_SOAP_ERR;
    }
    DestroySoap(pSoap);
    
    return iRet;
}

int ONVIF_SetCamTime_Test(struct tm * pt, char * httpaddr)
{
    int  result = 0;
    struct soap *pSoap = NULL;

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }
    result = SetCameraTime(pSoap, 0, httpaddr, pt);
    if ( 0 != result )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
        return E_OAR_SOAP_ERR;
    }
    DestroySoap(pSoap);

    return E_OAR_OK;
}

/*******************************************************************************
** �� �� �� :  ONVIF_GetCamTime   
** ��  �� :  ����ָ������ͷ��ʱ�䣬��Ҫ������Сʱ�����ӡ����ӡ��ꡢ�¡���
               ʱ�����Ч���е����߱�֤
** ��  �� :  
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-02  
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_GetCamTime(int channel, struct tm * pt)
{
    int  iRet = 0;
    char acDeviceServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    struct soap *pSoap = NULL;

    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) || ( NULL == pt ) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    memset(acDeviceServiceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acDeviceServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acDeviceServiceAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);

    if ( 0 == strlen(acDeviceServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }
    
    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }

    iRet = GetCameraTime(pSoap, channel, acDeviceServiceAddr, pt);
    if ( 0 != iRet )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
        iRet =  E_OAR_SOAP_ERR;
    }
    DestroySoap(pSoap);
    
    return iRet;
}

/*
int ONVIF_AddPreset(int channel, const ST_ONVIF_CAM_PRESET_INFO * pstCamPresetInfo)
{
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) || (NULL == pstCamPresetInfo) )
    {
        printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    return E_OAR_OK;
}
*/

/*******************************************************************************
** �� �� �� :  ONVIF_StartContinuousMove   
** ��  �� :    ���������ƶ�
** ��  �� :    channel: ͨ���ţ� eMove: �ƶ�����
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-16  
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_StartContinuousMove(int channel, E_ONVIF_MOVE_TYPE eMove)
{
    int  iRet = 0;
    char acProfile[ONVIF_STRING_LEN] = {0x0};
    char acPTZServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acDeviceAddr[SER_NET_ADDR_LEN] = {0x0};
    struct soap *pSoap = NULL;
    
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    memset(acProfile, 0x0, ONVIF_STRING_LEN);
    memset(acPTZServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acDeviceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acProfile, g_CamInfoSet[channel].stCamMainVideo.acProfile, ONVIF_STRING_LEN - 1);
    strncpy(acPTZServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acPTZServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acDeviceAddr, g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);

    if ( 0 == strlen(acPTZServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }
    //CamAuthorized(pSoap, channel);
    iRet = StartContinuousMove(pSoap, channel, acPTZServiceAddr, acProfile, eMove);
    if ( 0 != iRet)
    {
       // printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
       // printf("%s: Change Addr: %s\n", __FUNCTION__, acDeviceAddr);
        
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����PTZ���� */
        iRet = StartContinuousMove(pSoap, channel, acDeviceAddr, acProfile, eMove);
        if ( 0 != iRet )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
            iRet = E_OAR_SOAP_ERR;
        }
    }
    DestroySoap(pSoap);
    
    return iRet;
}

/*******************************************************************************
** �� �� �� :  ONVIF_StopContinuousMove   
** ��  �� :    ֹͣ�����ƶ�
** ��  �� :    channel: ͨ���ţ� eMove: �ƶ�����
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-16  
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_StopContinuousMove(int channel)
{
    int  iRet = 0;
    char acProfile[ONVIF_STRING_LEN] = {0x0};
    char acPTZServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acDeviceAddr[SER_NET_ADDR_LEN] = {0x0};
    struct soap *pSoap = NULL;
    
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    
    memset(acProfile, 0x0, ONVIF_STRING_LEN);
    memset(acPTZServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acDeviceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acProfile, g_CamInfoSet[channel].stCamMainVideo.acProfile, ONVIF_STRING_LEN - 1);
    strncpy(acPTZServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acPTZServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acDeviceAddr, g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);

    if ( 0 == strlen(acPTZServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }
    //CamAuthorized(m_pstSoap, channel);
    iRet = StopContinuousMove(pSoap, channel, acPTZServiceAddr, acProfile);
    if ( 0 != iRet)
    {
        //printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
        //printf("%s: Change Addr: %s\n", __FUNCTION__, acDeviceAddr);
        
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����PTZ���� */
        iRet = StopContinuousMove(pSoap, channel, acDeviceAddr, acProfile);
        if ( 0 != iRet )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
            iRet =  E_OAR_SOAP_ERR; 
        }
        
    }
    DestroySoap(pSoap);
    
    return iRet;
}

/*******************************************************************************
** �� �� �� :  ONVIF_StartContinuousZoom   
** ��  �� :    ���������ƶ�
** ��  �� :    channel: ͨ���ţ� eZoom: �Ŵ���С
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-16  
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_StartContinuousZoom(int channel, E_ONVIF_ZOOM_TYPE eZoom)
{
    int  iRet = 0;
    char acProfile[ONVIF_STRING_LEN] = {0x0};
    char acPTZServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acDeviceAddr[SER_NET_ADDR_LEN] = {0x0};
    struct soap *pSoap = NULL;
    
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }


    memset(acProfile, 0x0, ONVIF_STRING_LEN);
    memset(acPTZServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acDeviceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acProfile, g_CamInfoSet[channel].stCamMainVideo.acProfile, ONVIF_STRING_LEN - 1);
    strncpy(acPTZServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acPTZServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acDeviceAddr, g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);
    
    if ( 0 == strlen(acPTZServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }
    //CamAuthorized(m_pstSoap, channel);
    iRet = StartContinuousZoom(pSoap, channel, acPTZServiceAddr, acProfile, eZoom);
    if ( 0 != iRet)
    {
        //printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
       // printf("%s: Change Addr: %s\n", __FUNCTION__, acDeviceAddr);
        
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����PTZ���� */
        iRet = StartContinuousZoom(pSoap, channel, acDeviceAddr, acProfile, eZoom);
        if ( 0 != iRet)
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
            iRet =  E_OAR_SOAP_ERR;
        }
    }
    DestroySoap(pSoap);
    
    return iRet;
}

/*******************************************************************************
** �� �� �� :  ONVIF_StopContinuousZoom   
** ��  �� :    ֹͣ�����ƶ�
** ��  �� :    channel: ͨ���ţ� eMove: �ƶ�����
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-16  
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_StopContinuousZoom(int channel)
{
    int  iRet = 0;
    char acProfile[ONVIF_STRING_LEN] = {0x0};
    char acPTZServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acDeviceAddr[SER_NET_ADDR_LEN] = {0x0};
    struct soap *pSoap = NULL;
    
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }


    memset(acProfile, 0x0, ONVIF_STRING_LEN);
    memset(acPTZServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acDeviceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acProfile, g_CamInfoSet[channel].stCamMainVideo.acProfile, ONVIF_STRING_LEN - 1);
    strncpy(acPTZServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acPTZServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acDeviceAddr, g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);
    
    if ( 0 == strlen(acPTZServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }
    //CamAuthorized(m_pstSoap, channel);
    iRet = StopContinuousZoom(pSoap, channel, acPTZServiceAddr, acProfile);
    if ( 0 != iRet)
    {
       // printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
       // printf("%s: Change Addr: %s\n", __FUNCTION__, acDeviceAddr);
        
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����PTZ���� */
        iRet = StopContinuousZoom(pSoap, channel, acDeviceAddr, acProfile);
        if ( 0 != iRet)
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
            iRet =  E_OAR_SOAP_ERR;
        }
    }
    DestroySoap(pSoap);
    
    return iRet;
}

/*******************************************************************************
** �� �� �� :  ONVIF_SetCamPreset   
** ��  �� :    ����Ԥ��λ
** ��  �� :    channel: ͨ���ţ� iPresetNo: Ԥ��λ���
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-18
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_SetCamPreset(int channel, int iPresetNo)
{
    int iRet = 0;
    char acProfile[ONVIF_STRING_LEN] = {0x0};
    char acPTZServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acDeviceAddr[SER_NET_ADDR_LEN] = {0x0};
    struct soap *pSoap = NULL;
    
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }


    memset(acProfile, 0x0, ONVIF_STRING_LEN);
    memset(acPTZServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acDeviceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acProfile, g_CamInfoSet[channel].stCamMainVideo.acProfile, ONVIF_STRING_LEN - 1);
    strncpy(acPTZServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acPTZServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acDeviceAddr, g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);
    
    if ( 0 == strlen(acPTZServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }
    iRet = AddPreset(pSoap,channel, acPTZServiceAddr, acProfile, iPresetNo);
    if ( E_OAR_OK != iRet )
    {
        //printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
        //printf("%s: Change Addr: %s\n", __FUNCTION__, acDeviceAddr);
        
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����PTZ���� */
        iRet = AddPreset(pSoap,channel, acDeviceAddr, acProfile, iPresetNo);
        if ( E_OAR_OK != iRet )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
            iRet = E_OAR_SOAP_ERR;
        }
    }
    DestroySoap(pSoap);

    return iRet;
    
}

/*******************************************************************************
** �� �� �� :  ONVIF_GotoPreset   
** ��  �� :    ��ת��ָ��Ԥ��λ
** ��  �� :    channel: ͨ���ţ� iPresetNo: Ԥ��λ���
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-18
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_GotoPreset(int channel, int iPresetNo)
{
    int iRet = 0;
    char acProfile[ONVIF_STRING_LEN] = {0x0};
    char acPTZServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acDeviceAddr[SER_NET_ADDR_LEN] = {0x0};
    struct soap *pSoap = NULL;

    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }


    memset(acProfile, 0x0, ONVIF_STRING_LEN);
    memset(acPTZServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acDeviceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acProfile, g_CamInfoSet[channel].stCamMainVideo.acProfile, ONVIF_STRING_LEN - 1);
    strncpy(acPTZServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acPTZServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acDeviceAddr, g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);
    
    if ( 0 == strlen(acPTZServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }
    iRet = GotoPreset(pSoap, channel, acPTZServiceAddr, acProfile, iPresetNo);
    if ( E_OAR_OK != iRet )
    {
        //printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
        //printf("%s: Change Addr: %s\n", __FUNCTION__, acDeviceAddr);
        
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����PTZ���� */
        iRet = GotoPreset(pSoap, channel, acDeviceAddr, acProfile, iPresetNo);
        if ( E_OAR_OK != iRet )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
            iRet =  E_OAR_SOAP_ERR;
        }
    }
    DestroySoap(pSoap);

    return iRet;
}

/*******************************************************************************
** �� �� �� :  ONVIF_GetAllPreset   
** ��  �� :    ��ȡָ������ͷ������Ԥ��λ��Ϣ
** ��  �� :    channel: ͨ����
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-18
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_GetAllPreset(int channel)
{
    char acPTZServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acDeviceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acProfile[ONVIF_STRING_LEN] = {0x0};
    struct soap *pSoap = NULL;
    
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    memset(acProfile, 0x0, ONVIF_STRING_LEN);
    memset(acPTZServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acDeviceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acProfile, g_CamInfoSet[channel].stCamMainVideo.acProfile, ONVIF_STRING_LEN - 1);
    strncpy(acPTZServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acPTZServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acDeviceAddr, g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }
    CamAuthorized(pSoap, channel);
    if ( 0 != GetAllPresets(pSoap, acPTZServiceAddr,acProfile) )
    {
        //printf("%s: Change Addr: %s\n", __FUNCTION__, acDeviceAddr);
        
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����PTZ���� */
        if ( 0 != GetAllPresets(pSoap, acDeviceAddr,acProfile) )
        {
            return E_OAR_SOAP_ERR;
        }
    }
    DestroySoap(pSoap);

    return E_OAR_OK;
}

/*******************************************************************************
** �� �� �� :  ONVIF_StartContinuousFocus   
** ��  �� :    ��������ͷ�ľ۽�
** ��  �� :    channel: ͨ����  eFocus: �۽�����(Զ/��)
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-18
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_StartContinuousFocus(int channel, E_ONVIF_FOCUS_TYPE eFocus)
{
    int iRet = E_OAR_OK;
    char acIMGServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acDeviceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acSrcToken[ONVIF_STRING_LEN] = {0x0};
    struct soap *pSoap = NULL;

    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    memset(acIMGServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acSrcToken, 0x0, ONVIF_STRING_LEN);
    memset(acDeviceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acSrcToken, g_CamInfoSet[channel].stCamMainVideo.acSrcToken, ONVIF_STRING_LEN - 1);
    strncpy(acIMGServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acIMGServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acDeviceAddr, g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);

    if ( 0 == strlen(acIMGServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }

    iRet = StartContinuousFocus(pSoap, channel, acIMGServiceAddr, acSrcToken, eFocus);
    if ( 0 != iRet)
    {
       //printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
        //printf("%s: Change Addr: %s\n", __FUNCTION__, acDeviceAddr);
        
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����PTZ���� */
        iRet = StartContinuousFocus(pSoap, channel, acDeviceAddr, acSrcToken, eFocus);
        if ( 0 != iRet)
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
            iRet = E_OAR_SOAP_ERR;
        }
    }
    DestroySoap(pSoap);

    return iRet;
}

/*******************************************************************************
** �� �� �� :  ONVIF_StopContinuousFocus   
** ��  �� :    ֹͣ����ͷ�ľ۽�
** ��  �� :    channel: ͨ���� 
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-09-18
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_StopContinuousFocus(int channel)
{
    int iRet = 0;
    char acIMGServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acDeviceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acSrcToken[ONVIF_STRING_LEN] = {0x0};
    struct soap *pSoap = NULL;

    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    memset(acIMGServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acSrcToken, 0x0, ONVIF_STRING_LEN);
    memset(acDeviceAddr, 0x0, SER_NET_ADDR_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acSrcToken, g_CamInfoSet[channel].stCamMainVideo.acSrcToken, ONVIF_STRING_LEN - 1);
    strncpy(acIMGServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acIMGServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acDeviceAddr, g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);

    if ( 0 == strlen(acIMGServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }
	
    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }

    iRet = StopContinuousFocus(pSoap, channel, acIMGServiceAddr, acSrcToken);
    if ( 0 != iRet)
    {
        //printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
        //printf("%s: Change Addr: %s\n", __FUNCTION__, acDeviceAddr);
        
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����PTZ���� */
        iRet = StopContinuousFocus(pSoap, channel, acDeviceAddr, acSrcToken);
        if ( 0 != iRet)
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
            iRet = E_OAR_SOAP_ERR;
        }
    }
    DestroySoap(pSoap);

    return iRet;
}

/*******************************************************************************
** �� �� �� :  ONVIF_GetImagingSettings   
** ��  �� :    ��ȡ����ͷ��ͼ�����
** ��  �� :    channel: ͨ���� 
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-12-04
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_GetImagingSettings(int channel, ST_ONVIF_IMG_PARAM * pstIMGParam)
{
    int iRet = 0;
    char acIMGServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acHttpAddr[SER_NET_ADDR_LEN] = {0x0};
    char acSrcToken[ONVIF_STRING_LEN] = {0x0};
    struct soap *pSoap = NULL;
    
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) || (NULL == pstIMGParam) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    memset(acIMGServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acHttpAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acSrcToken, 0x0, ONVIF_STRING_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acSrcToken, g_CamInfoSet[channel].stCamMainVideo.acSrcToken, ONVIF_STRING_LEN - 1);
    strncpy(acIMGServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acIMGServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acHttpAddr,  g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);

    if ( 0 == strlen(acIMGServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }

    iRet = GetImageParam(pSoap, channel, acSrcToken, acIMGServiceAddr, pstIMGParam);
    if ( 0 != iRet)
    {
        //printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
        //printf("Change addr: %s", acHttpAddr);
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����ͼ����� */
        iRet = GetImageParam(pSoap, channel, acSrcToken, acHttpAddr, pstIMGParam);
        if ( 0 != iRet )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
            iRet = E_OAR_SOAP_ERR;
        }
    }
    DestroySoap(pSoap);
    
    return iRet;
}

/*******************************************************************************
** �� �� �� :  ONVIF_SetImagingSettings   
** ��  �� :    ��������ͷ��ͼ�����
** ��  �� :    channel: ͨ���� 
** ��  �� :  
** ��  �� :  
** ��  �� :  xsh 
** ��  �� :  2014-12-04
 
** �޸ļ�¼ : 
*******************************************************************************/
int ONVIF_SetImagingSettings(int channel, const ST_ONVIF_IMG_PARAM * pstIMGParam)
{
    int iRet = 0;
    char acIMGServiceAddr[SER_NET_ADDR_LEN] = {0x0};
    char acHttpAddr[SER_NET_ADDR_LEN] = {0x0};
    char acSrcToken[ONVIF_STRING_LEN] = {0x0};
    struct soap *pSoap = NULL;
    
    if ( (channel >= MAX_ONVIF_CAM_NUM) || (channel  < 0) || (NULL == pstIMGParam) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_INPUT_ERR));
        return E_OAR_INPUT_ERR;
    }

    memset(acIMGServiceAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acHttpAddr, 0x0, SER_NET_ADDR_LEN);
    memset(acSrcToken, 0x0, ONVIF_STRING_LEN);
    
    pthread_mutex_lock(&s_CamParamMutex);
    /* ʹ����������profile */
    strncpy(acSrcToken, g_CamInfoSet[channel].stCamMainVideo.acSrcToken, ONVIF_STRING_LEN - 1);
    strncpy(acIMGServiceAddr, g_CamInfoSet[channel].stServiceAddrs.acIMGServiceAddr, SER_NET_ADDR_LEN - 1);
    strncpy(acHttpAddr, g_CamInfoSet[channel].stServiceAddrs.acCamHttpAddr, SER_NET_ADDR_LEN - 1);
    pthread_mutex_unlock(&s_CamParamMutex);

    if ( 0 == strlen(acIMGServiceAddr) )
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_ADDR_ERR));
        return E_OAR_ADDR_ERR;
    }

    pSoap = CreateSoap(3);
    if (NULL == pSoap)
    {
        DebugPrint(ONVIF_DEBUG_LEVEL, "[%s] Create soap failed\n", __FUNCTION__);
        return E_OAR_ADDR_ERR;
    }
    iRet = SetImageParam(pSoap, channel, acSrcToken, acIMGServiceAddr, (ST_ONVIF_IMG_PARAM *)pstIMGParam);
    if ( 0 != iRet)
    {
        //printf("%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
        //printf("Change addr: %s", acHttpAddr);
        /* Modifyed by xsh: �����е�����ͷֻ��ͨ���豸��ַ����ͼ����� */
        iRet = SetImageParam(pSoap, channel, acSrcToken, acHttpAddr, (ST_ONVIF_IMG_PARAM *)pstIMGParam);
        if ( 0 != iRet )
        {
            DebugPrint(ONVIF_DEBUG_LEVEL, "%s: %s\n", __FUNCTION__, ONVIF_ERR2STR(E_OAR_SOAP_ERR));
            iRet = E_OAR_SOAP_ERR;
        }
        
    }
    DestroySoap(pSoap);
    
    return iRet;
}

