#ifndef __ONVIFAPI_H__
#define __ONVIFAPI_H__
#ifdef  __cplusplus
extern "C"
{
#endif

#include "../debug.h"
#define ONVIF_DEBUG_LEVEL DEBUG_LEVEL_5

#define MAX_ONVIF_CAM_NUM    40
#define SER_NET_ADDR_LEN     128
#define ONVIF_STRING_LEN     32

#define MAX_PRESETS_PER_CAM  10

#define ONVIF_AUTHOR_STR_LEN 16

#define ARRAY_NUM(a)  (sizeof(a)/sizeof(a[0]))

/* ����ͷIP���˻ص����� */
typedef int (*CB_IP_FILTER)(const char *);
/*******************************************************************************
 **  ONVIF API�����ӿڷ���ֵ���Ͷ���
*******************************************************************************/
typedef enum tagE_ONVIF_API_RTN
{
    E_OAR_OK = 0,        /* ���سɹ� */
    E_OAR_INPUT_ERR = -1,     /* ����������� */
    E_OAR_SOAP_ERR  = -2,      /* SOAP���ô��� */
    E_OAR_NET_ERR   = -3,       /* ������� */
    E_OAR_AUTHR_ERR = -4,     /* ��Ȩ���� */
    E_OAR_ADDR_ERR  = -5,      /* �����ַ���� */
    E_OAR_OTHER_ERR = -6,     /* �������� */
}E_ONVIF_API_RTN;

#define ONVIF_ERR2STR(a)  ( ( (a>0) || (a<ARRAY_NUM(m_OnvifApiRtnString)))?("Out Of Error Define!"):(m_OnvifApiRtnString[0-a]) )                          

/*******************************************************************************
 **  ONVIF�ӿ����ݽṹ����
*******************************************************************************/
typedef enum tagONVIF_BOOL{ ONVIF_FALSE = 0,  ONVIF_TRUE = 1 }ONVIF_BOOL;

/* onvif����ͷ��Ƶ���� */
typedef struct tagST_ONVIF_CAM_VIDEO_PARAM
{
    int  iWidth;
    int  iHeight;
    int  iFps;
    int  iEncBitRate;
    ONVIF_BOOL eFixed;  /* ���ñ�־ */
    char acRtspUri[SER_NET_ADDR_LEN];      /* rtsp����ַ */
    char acSnapShortUri[SER_NET_ADDR_LEN]; /* ץ�ĵ�ַ */
    char acProfile[ONVIF_STRING_LEN];      /* profile */
    char acSrcToken[ONVIF_STRING_LEN];      /* SourceToken */

}ST_ONVIF_CAM_VIDEO_PARAM;

/* ONVIF ����ͷԤ��λ��Ϣ */
typedef struct tagST_ONVIF_CAM_PRESET_INFO
{
    char acPresetName[ONVIF_STRING_LEN];  // Ԥ��λ����
    char acPresetToken[ONVIF_STRING_LEN]; // Ԥ��λ��Ŷ�Ӧ���ַ���
    ONVIF_BOOL bSet;                      // ��Ԥ��λ�Ƿ�����
}ST_ONVIF_CAM_PRESET_INFO;

/* ȡֵ��Χ */
typedef struct tagST_ONVIF_FLOAT_RANGE
{
    float min;
    float max;
}ST_ONVIF_FLOAT_RANGE;
/* ONVIF ����ͷλ�úͱ佹��Ϣ */

/* ONVIF ����ͷPTZ���� */
typedef struct tagST_ONVIF_CAM_PTZ_PARAM
{
    ST_ONVIF_FLOAT_RANGE XRange;
    ST_ONVIF_FLOAT_RANGE YRange;
    ST_ONVIF_CAM_PRESET_INFO stPreset[10];
}ST_ONVIF_CAM_PTZ_PARAM;

/* ONVIF ����ͷ��ַ */
typedef struct tagST_ONVIF_CAM_NET_INTERFACE
{
    char  strIpV4Addr[16];   /* IPV4��ַ: �ַ�����ʾ */
}ST_ONVIF_CAM_NET_INTERFASE;

typedef struct tagST_ONVIF_CAM_SERVICE_ADDRS
{
    char    acDeviceServiceAddr[SER_NET_ADDR_LEN];
    char    acMediaServiceAddr[SER_NET_ADDR_LEN];
    char    acPTZServiceAddr[SER_NET_ADDR_LEN];
    char    acIMGServiceAddr[SER_NET_ADDR_LEN];
    char    acCamHttpAddr[SER_NET_ADDR_LEN];       /* ����ͷhttp��ַ */
}ST_ONVIF_CAM_SERVICE_ADDRS;


/* onvif����ͷ��Ϣ���ϣ������������ͷweb��������ַ */
typedef struct tagST_ONVIF_CAMINFO_SET
{
    ST_ONVIF_CAM_VIDEO_PARAM    stCamMainVideo;     /* ��������Ϣ */
    ST_ONVIF_CAM_VIDEO_PARAM    stCamSubVideo;      /* ��������Ϣ */
    ST_ONVIF_CAM_PTZ_PARAM      stCamPTZParam;      /* Ԥ��λ��Ϣ */
    ST_ONVIF_CAM_NET_INTERFASE  stCamNetInterface;  /* ����ӿ���Ϣ */

    ST_ONVIF_CAM_SERVICE_ADDRS  stServiceAddrs;
    ONVIF_BOOL                  eValid;             /* ����ͷ���ݿ��Ա�־����ʾ����ͷ�����ݿ��� */
    
}ST_ONVIF_CAMINFO_SET;

/* ONVIF ����ͷͼ��������� */
typedef struct tagST_ONVIF_IMG_PARAM
{
    unsigned int uiBrightness;
    unsigned int uiColorSaturation;
    unsigned int uiContrast;
}ST_ONVIF_IMG_PARAM;

/* ONVIF����ͷ�ƶ������� */
typedef enum tagE_ONVIF_MOVE_TYPE
{
    E_OMT_UP = 1,   // ����
    E_OMT_DN = 2,       // ����
    E_OMT_LT = 3,       // ����
    E_OMT_RT = 4,       // ����
}E_ONVIF_MOVE_TYPE;

/* ONVIF ����ͷ����(����)���� */
typedef enum tagE_ONVIF_ZOOM_TYPE
{
    E_OZT_IN  = 5,    // �Ŵ�
    E_OZT_OUT = 6,    // ��С
}E_ONVIF_ZOOM_TYPE;

/* ONVIF ����ͷ�۽�(Զ��)���� */
typedef enum tagE_ONVIF_FOCUS_TYPE
{
    E_OFT_FAR   = 7,  // ��Զ
    E_OFT_NEAR  = 8,  // ����
}E_ONVIF_FOCUS_TYPE;

/*******************************************************************************
**  ONVIF�ӿں�������
*******************************************************************************/
/* ��ʼ�� */
int ONVIF_Init(const char * acUserName, const char * acPassWord, const char * acIPAddr, CB_IP_FILTER fCBIpFilter);

/* ����ͷ̽�� */
int ONVIF_StartProbe(void);

/* ��ȡ̽�⵽������ͷ���� */
int ONVIF_GetChNum(void);

/* ����̽�⵽������ͷ��Ż�ȡ������ͷ�Ĳ�����Ϣ */
int ONVIF_GetChInfo(int channel, ST_ONVIF_CAMINFO_SET * pstParam);

/* ��������ͷ��ʱ�� */
int ONVIF_SetCamTime(int channel, struct tm * sttime);

/* ��ȡ����ͷ��ʱ�� */
int ONVIF_GetCamTime(int channel, struct tm * sttime);

/* �ͷ���Դ */
int ONVIF_Release(void);

/* ȥ��ʼ�� */
int ONVIF_Uninit(void);

int ONVIF_SnapInit(void);

int ONVIF_SnapDeInit(void);

int ONVIF_SnapSynctime(void);

int ONVIF_SnapShort(void);

/* ��ʼ�����ƶ�����ͷ */
int ONVIF_StartContinuousMove(int channel, E_ONVIF_MOVE_TYPE eMove);

/* ֹͣ�����ƶ�����ͷ */
int ONVIF_StopContinuousMove(int channel);

/* ��ʼ���� */
int ONVIF_StartContinuousZoom(int channel, E_ONVIF_ZOOM_TYPE eZoom);

/* ֹͣ���� */
int ONVIF_StopContinuousZoom(int channel);

/* ��ʼ�۽� */
int ONVIF_StartContinuousFocus(int channel, E_ONVIF_FOCUS_TYPE eFocus);

/* ֹͣ�۽� */
int ONVIF_StopContinuousFocus(int channel);

/* ����Ԥ��λ */
int ONVIF_SetCamPreset(int channel, int iPresetNo);

/* ��ת��Ԥ��λ */
int ONVIF_GotoPreset(int channel, int iPresetNo);

/* ��ȡ����Ԥ��λ */
int ONVIF_GetAllPreset(int channel);

/* ��ȡͼ����� */
int ONVIF_GetImagingSettings(int channel, ST_ONVIF_IMG_PARAM * pstIMGParam);

/* ����ͼ����� */
int ONVIF_SetImagingSettings(int channel, const ST_ONVIF_IMG_PARAM * pstIMGParam);


#ifdef  __cplusplus
}
#endif

#endif

