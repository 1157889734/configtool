#ifndef __ONVIFINC_H__
#define __ONVIFINC_H__

/*                      ����ͷ���������ռ��־                   */
/* �豸���� */
#define ONVIF_CAM_NAMESPACE_DEVICE    "http://www.onvif.org/ver10/device/wsdl"

/* ý����� */
#define ONVIF_CAM_NAMESPACE_MEDIA     "http://www.onvif.org/ver10/media/wsdl"

/* �¼����� */
#define ONVIF_CAM_NAMESPACE_EVENT     "http://www.onvif.org/ver10/events/wsdl"

/* PTZ���� */
#define ONVIF_CAM_NAMESPACE_PTZ       "http://www.onvif.org/ver20/ptz/wsdl"

/* IMG���� */
#define ONVIF_CAM_NAMESPACE_IMG       "http://www.onvif.org/ver20/imaging/wsdl"

/* ��ӡ������Ϣ */
#define ONVIF_ERROR_PRINT(a) { \
    printf("%s: error[%d], faultcode[%s], faultstring[%s]\n", \
                  __FUNCTION__, (a)->error, *soap_faultcode((a)), *soap_faultstring((a))) ;\
}

/* ONVIF �����ַ */
typedef struct tagST_ONVIF_CAM_SERVICES
{
    char    acDeviceServiceAddr[SER_NET_ADDR_LEN];
    char    acMediaServiceAddr[SER_NET_ADDR_LEN];
    char    acPTZServiceAddr[SER_NET_ADDR_LEN];
    char    acIMGServiceAddr[SER_NET_ADDR_LEN];

    char    acCamHttpAddr[SER_NET_ADDR_LEN]; /* ����ͷhttp��ַ */

    ONVIF_BOOL   eDiscoverFlg;               /* ����ͷ����/���±�־ ��ʾ����ͷ�����֣����ݲ�һ������*/

}ST_ONVIF_CAM_SERVICES;

/* ONVIF ����ͷ��Ȩ���� */
typedef struct tagST_ONVIF_CAM_AUTHORIZE
{
    char       acUserName[ONVIF_AUTHOR_STR_LEN];
    char       acPassWord[ONVIF_AUTHOR_STR_LEN];
    ONVIF_BOOL bAuthor;        /* �Ƿ���Ҫ��Ȩ */
}ST_ONVIF_CAM_AUTHORIZE;



#endif

