#ifndef __ONVIFINC_H__
#define __ONVIFINC_H__

/*                      摄像头服务命名空间标志                   */
/* 设备服务 */
#define ONVIF_CAM_NAMESPACE_DEVICE    "http://www.onvif.org/ver10/device/wsdl"

/* 媒体服务 */
#define ONVIF_CAM_NAMESPACE_MEDIA     "http://www.onvif.org/ver10/media/wsdl"

/* 事件服务 */
#define ONVIF_CAM_NAMESPACE_EVENT     "http://www.onvif.org/ver10/events/wsdl"

/* PTZ服务 */
#define ONVIF_CAM_NAMESPACE_PTZ       "http://www.onvif.org/ver20/ptz/wsdl"

/* IMG服务 */
#define ONVIF_CAM_NAMESPACE_IMG       "http://www.onvif.org/ver20/imaging/wsdl"

/* 打印错误信息 */
#define ONVIF_ERROR_PRINT(a) { \
    printf("%s: error[%d], faultcode[%s], faultstring[%s]\n", \
                  __FUNCTION__, (a)->error, *soap_faultcode((a)), *soap_faultstring((a))) ;\
}

/* ONVIF 服务地址 */
typedef struct tagST_ONVIF_CAM_SERVICES
{
    char    acDeviceServiceAddr[SER_NET_ADDR_LEN];
    char    acMediaServiceAddr[SER_NET_ADDR_LEN];
    char    acPTZServiceAddr[SER_NET_ADDR_LEN];
    char    acIMGServiceAddr[SER_NET_ADDR_LEN];

    char    acCamHttpAddr[SER_NET_ADDR_LEN]; /* 摄像头http地址 */

    ONVIF_BOOL   eDiscoverFlg;               /* 摄像头发现/更新标志 表示摄像头被发现，数据不一定可用*/

}ST_ONVIF_CAM_SERVICES;

/* ONVIF 摄像头鉴权参数 */
typedef struct tagST_ONVIF_CAM_AUTHORIZE
{
    char       acUserName[ONVIF_AUTHOR_STR_LEN];
    char       acPassWord[ONVIF_AUTHOR_STR_LEN];
    ONVIF_BOOL bAuthor;        /* 是否需要鉴权 */
}ST_ONVIF_CAM_AUTHORIZE;



#endif

