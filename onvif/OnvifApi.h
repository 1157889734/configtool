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

/* 摄像头IP过滤回调函数 */
typedef int (*CB_IP_FILTER)(const char *);
/*******************************************************************************
 **  ONVIF API函数接口返回值类型定义
*******************************************************************************/
typedef enum tagE_ONVIF_API_RTN
{
    E_OAR_OK = 0,        /* 返回成功 */
    E_OAR_INPUT_ERR = -1,     /* 输入参数错误 */
    E_OAR_SOAP_ERR  = -2,      /* SOAP调用错误 */
    E_OAR_NET_ERR   = -3,       /* 网络错误 */
    E_OAR_AUTHR_ERR = -4,     /* 鉴权错误 */
    E_OAR_ADDR_ERR  = -5,      /* 服务地址错误 */
    E_OAR_OTHER_ERR = -6,     /* 其他错误 */
}E_ONVIF_API_RTN;

#define ONVIF_ERR2STR(a)  ( ( (a>0) || (a<ARRAY_NUM(m_OnvifApiRtnString)))?("Out Of Error Define!"):(m_OnvifApiRtnString[0-a]) )                          

/*******************************************************************************
 **  ONVIF接口数据结构定义
*******************************************************************************/
typedef enum tagONVIF_BOOL{ ONVIF_FALSE = 0,  ONVIF_TRUE = 1 }ONVIF_BOOL;

/* onvif摄像头视频参数 */
typedef struct tagST_ONVIF_CAM_VIDEO_PARAM
{
    int  iWidth;
    int  iHeight;
    int  iFps;
    int  iEncBitRate;
    ONVIF_BOOL eFixed;  /* 启用标志 */
    char acRtspUri[SER_NET_ADDR_LEN];      /* rtsp流地址 */
    char acSnapShortUri[SER_NET_ADDR_LEN]; /* 抓拍地址 */
    char acProfile[ONVIF_STRING_LEN];      /* profile */
    char acSrcToken[ONVIF_STRING_LEN];      /* SourceToken */

}ST_ONVIF_CAM_VIDEO_PARAM;

/* ONVIF 摄像头预制位信息 */
typedef struct tagST_ONVIF_CAM_PRESET_INFO
{
    char acPresetName[ONVIF_STRING_LEN];  // 预置位名称
    char acPresetToken[ONVIF_STRING_LEN]; // 预置位编号对应的字符串
    ONVIF_BOOL bSet;                      // 该预置位是否被设置
}ST_ONVIF_CAM_PRESET_INFO;

/* 取值范围 */
typedef struct tagST_ONVIF_FLOAT_RANGE
{
    float min;
    float max;
}ST_ONVIF_FLOAT_RANGE;
/* ONVIF 摄像头位置和变焦信息 */

/* ONVIF 摄像头PTZ参数 */
typedef struct tagST_ONVIF_CAM_PTZ_PARAM
{
    ST_ONVIF_FLOAT_RANGE XRange;
    ST_ONVIF_FLOAT_RANGE YRange;
    ST_ONVIF_CAM_PRESET_INFO stPreset[10];
}ST_ONVIF_CAM_PTZ_PARAM;

/* ONVIF 摄像头地址 */
typedef struct tagST_ONVIF_CAM_NET_INTERFACE
{
    char  strIpV4Addr[16];   /* IPV4地址: 字符串表示 */
}ST_ONVIF_CAM_NET_INTERFASE;

typedef struct tagST_ONVIF_CAM_SERVICE_ADDRS
{
    char    acDeviceServiceAddr[SER_NET_ADDR_LEN];
    char    acMediaServiceAddr[SER_NET_ADDR_LEN];
    char    acPTZServiceAddr[SER_NET_ADDR_LEN];
    char    acIMGServiceAddr[SER_NET_ADDR_LEN];
    char    acCamHttpAddr[SER_NET_ADDR_LEN];       /* 摄像头http地址 */
}ST_ONVIF_CAM_SERVICE_ADDRS;


/* onvif摄像头信息集合，这里包括摄像头web服务器地址 */
typedef struct tagST_ONVIF_CAMINFO_SET
{
    ST_ONVIF_CAM_VIDEO_PARAM    stCamMainVideo;     /* 主码流信息 */
    ST_ONVIF_CAM_VIDEO_PARAM    stCamSubVideo;      /* 辅码流信息 */
    ST_ONVIF_CAM_PTZ_PARAM      stCamPTZParam;      /* 预制位信息 */
    ST_ONVIF_CAM_NET_INTERFASE  stCamNetInterface;  /* 网络接口信息 */

    ST_ONVIF_CAM_SERVICE_ADDRS  stServiceAddrs;
    ONVIF_BOOL                  eValid;             /* 摄像头数据可以标志，表示摄像头的数据可用 */
    
}ST_ONVIF_CAMINFO_SET;

/* ONVIF 摄像头图像参数定义 */
typedef struct tagST_ONVIF_IMG_PARAM
{
    unsigned int uiBrightness;
    unsigned int uiColorSaturation;
    unsigned int uiContrast;
}ST_ONVIF_IMG_PARAM;

/* ONVIF摄像头移动方向定义 */
typedef enum tagE_ONVIF_MOVE_TYPE
{
    E_OMT_UP = 1,   // 向上
    E_OMT_DN = 2,       // 向下
    E_OMT_LT = 3,       // 向左
    E_OMT_RT = 4,       // 向右
}E_ONVIF_MOVE_TYPE;

/* ONVIF 摄像头调焦(伸缩)定义 */
typedef enum tagE_ONVIF_ZOOM_TYPE
{
    E_OZT_IN  = 5,    // 放大
    E_OZT_OUT = 6,    // 缩小
}E_ONVIF_ZOOM_TYPE;

/* ONVIF 摄像头聚焦(远近)定义 */
typedef enum tagE_ONVIF_FOCUS_TYPE
{
    E_OFT_FAR   = 7,  // 调远
    E_OFT_NEAR  = 8,  // 调近
}E_ONVIF_FOCUS_TYPE;

/*******************************************************************************
**  ONVIF接口函数声明
*******************************************************************************/
/* 初始化 */
int ONVIF_Init(const char * acUserName, const char * acPassWord, const char * acIPAddr, CB_IP_FILTER fCBIpFilter);

/* 摄像头探测 */
int ONVIF_StartProbe(void);

/* 获取探测到的摄像头个数 */
int ONVIF_GetChNum(void);

/* 根据探测到的摄像头序号获取该摄像头的参数信息 */
int ONVIF_GetChInfo(int channel, ST_ONVIF_CAMINFO_SET * pstParam);

/* 设置摄像头的时间 */
int ONVIF_SetCamTime(int channel, struct tm * sttime);

/* 获取摄像头的时间 */
int ONVIF_GetCamTime(int channel, struct tm * sttime);

/* 释放资源 */
int ONVIF_Release(void);

/* 去初始化 */
int ONVIF_Uninit(void);

int ONVIF_SnapInit(void);

int ONVIF_SnapDeInit(void);

int ONVIF_SnapSynctime(void);

int ONVIF_SnapShort(void);

/* 开始连续移动摄像头 */
int ONVIF_StartContinuousMove(int channel, E_ONVIF_MOVE_TYPE eMove);

/* 停止连续移动摄像头 */
int ONVIF_StopContinuousMove(int channel);

/* 开始调焦 */
int ONVIF_StartContinuousZoom(int channel, E_ONVIF_ZOOM_TYPE eZoom);

/* 停止调焦 */
int ONVIF_StopContinuousZoom(int channel);

/* 开始聚焦 */
int ONVIF_StartContinuousFocus(int channel, E_ONVIF_FOCUS_TYPE eFocus);

/* 停止聚焦 */
int ONVIF_StopContinuousFocus(int channel);

/* 设置预置位 */
int ONVIF_SetCamPreset(int channel, int iPresetNo);

/* 跳转到预置位 */
int ONVIF_GotoPreset(int channel, int iPresetNo);

/* 获取所有预置位 */
int ONVIF_GetAllPreset(int channel);

/* 获取图像参数 */
int ONVIF_GetImagingSettings(int channel, ST_ONVIF_IMG_PARAM * pstIMGParam);

/* 设置图像参数 */
int ONVIF_SetImagingSettings(int channel, const ST_ONVIF_IMG_PARAM * pstIMGParam);


#ifdef  __cplusplus
}
#endif

#endif

