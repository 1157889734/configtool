#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "configtool.h"
#include "fileconf.h"
#include "./onvif/OnvifApi.h"
#include <net/if.h>
#include "debug.h"

typedef struct _T_IPC_INFO
{
    int iNvrNO;
	int iUiGroup;
	int iUiPos;
	char acRtspAddr[256];
	int iImgIndex;
    char acMainRtspAddr[256];
}__attribute__((packed)) T_IPC_INFO;

typedef struct _T_CONN_INFO
{
    int iSockfd;
    int  iOnvifNeedResp ;  //是否需要回复onvif
	time_t time;            //收到onvif请求时候的时间
} __attribute__((packed)) T_CONN_INFO, *PT_CONN_INFO;

typedef struct _T_IP_RTSP_INFO
{
	char acIp[16];
	char acRtsp[128];
    char acMainRtsp[128];
}__attribute__((packed)) T_IP_RTSP_INFO, *PT_IP_RTSP_INFO;

//在配置文档里面相机序号和图片序号统一是从1开始的

static int g_acImg2VideoIdx[32] = {0};  //第几图片对应的第几相机
static int g_iVideoNum = 0;
static char g_acConfigIniName[] = "/mnt/sconf/CCTVConfig.ini";
static char	g_acPecuVideoIdx[24] = {0};
static char	g_acFireVideoIdx[6]  = {0};
static char	g_acDoorVideoIdx[48] = {0};
static int  g_iOnvifInit = 0;
static T_IPC_INFO g_acIpcInfo[32] = {0};
static char g_acUser[16] = {0};
static char g_acPassword[16] = {0};
static char g_iDispType = 0;
static char g_acFileFullName[512] = {0};
static int  g_iFileTotalSize = 0;

#define CONFIG_FILE_DIR 	"/mnt/sconf"

static BYTE GetMsgDataEcc(BYTE  *pcData, INT32 iLen)
{
    int i = 0;
    BYTE ucEcc = 0;
    
    if ((NULL == pcData) || (0 == iLen))	
    {
        return 0;	
    }
    
    for (i = 0; i < iLen; i++)
    {
        ucEcc ^= pcData[i];
    }
    
    return ucEcc;
}


static int GetIPAddrFromRtspAddr(char * strRtsp, char * strIP)
{
    char * pstr1 = NULL;
    char * pstr2 = NULL;
	int iLen =0;

    pstr1 = strstr(strRtsp, "://");
    if ( NULL == pstr1 ) 
    {
        return 0;
    }

    pstr2 = strstr(pstr1+3, ":");
    if ( NULL == pstr2 ) 
    {
        return 0;
    }

	iLen = pstr2-pstr1 -3;
	if(iLen >15)
	{
		return 0;
	}

    strncpy(strIP, pstr1+3,iLen);
	
    return 1;
}

static int AddUserPasswordToRtsp(char *pRtsp,char *pDst,int DstLen, char *pUser,char *pPassword)
{
	char * pstr1 = NULL;

    pstr1 = strstr(pRtsp, "://");
    if ( NULL == pstr1 ) 
    {
        return -1;
    }
	if( (0 ==strlen(pUser)) || (0 == strlen(pPassword)))
	{
		snprintf(pDst,DstLen-1,"rtsp://%s",(char *)(pstr1+3));
	}
	else
	{
		snprintf(pDst,DstLen-1,"rtsp://%s:%s@%s",pUser,pPassword,(char *)(pstr1+3));
	}
   return 0;
}

static int ParseRtspUrl(char *pcRawUrl, char *pcUrl, char *pcUser, char *pcPasswd)
{
    char acStr[256];
    char *pcTmp = NULL;
    char *pcPos = NULL;
    char *pcContent = NULL;
    char *pcIpaddr = NULL;
    
    if ((NULL == pcRawUrl) || (NULL == pcUrl) || (NULL == pcUser) || (NULL == pcUser))
    {
        return -1;	
    }
    memset(acStr, 0, sizeof(acStr));
    strncpy(acStr, pcRawUrl, sizeof(acStr));
   
    /* rtsp:// */
    pcContent = acStr + 7;
    pcTmp = strsep(&pcContent, "/");
    
    if (pcTmp)
    {
        pcIpaddr = pcTmp;
        if(strstr(pcIpaddr, "@"))
        {
            pcTmp = strsep(&pcIpaddr, "@");
        }
        else
        {
            pcTmp = NULL;	
        }
    }

    if (pcTmp)
    {
        pcPos = pcTmp;
        pcTmp = strsep(&pcPos, ":");
        
        if (pcTmp)
        {
            strcpy(pcUser, pcTmp);
        }
        
        if (pcPos)
        {
            strcpy(pcPasswd, pcPos);
        }
        
    }
    
    if (NULL == pcIpaddr)
    {
        return -1;	
    }

    if (pcContent)
    {
        snprintf(pcUrl, 256, "rtsp://%s/%s", pcIpaddr, pcContent);
    }
    else
    {
        snprintf(pcUrl, 256, "rtsp://%s", pcIpaddr);
    }
    
    
    return 0;
}


static int SendPmsgInfo(int iSocket, unsigned char ucMsgCmd,T_MSG_INFO* ptMsgInfo,INT16 i16DataLen)
{
	T_MSG_HEAD *ptMsgHead = &(ptMsgInfo->tMsgHead);
	unsigned char ucEcc = 0;
	int iRet = 0;
	
	ptMsgHead->magic = MSG_MAGIC_FLAG;
    ptMsgHead->cmd =  ucMsgCmd;
    ptMsgHead->sLen = htons(i16DataLen);

	// 计算ECC校验
    ucEcc = GetMsgDataEcc((BYTE *)ptMsgHead, sizeof(T_MSG_HEAD));
    if (i16DataLen > 0)
    {
        ucEcc ^= GetMsgDataEcc((BYTE *)ptMsgInfo->acMsgData, i16DataLen);
    }       
    ptMsgInfo->acMsgData[i16DataLen] = ucEcc;  
    iRet = send(iSocket, ptMsgInfo, sizeof(T_MSG_HEAD) + i16DataLen + 1, 0);
    return iRet;
}

static int ParseSetIpcNumInfo(void *arg,char *pcData, int iDataLen)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	int iRet = 0;

	tMsgInfo.acMsgData[0] = 2;
	if(1 == iDataLen)
	{
		if((28 == pcData[0] || 32 == pcData[0]) )
		{
			tMsgInfo.acMsgData[0] = 1;
			if(g_iVideoNum != pcData[0])
			{
				memset(acData,0,sizeof(acData));
				snprintf(acData,sizeof(acData)-1,"%d",pcData[0]);
	   			iRet = ModifyParam(g_acConfigIniName,"[IPCINFO]","IPCNUM",acData);
				if(0 == iRet)
				{
					g_iVideoNum = pcData[0];	
				}
				else
				{
					tMsgInfo.acMsgData[0] = 2;
				}
			}
		}
	}
   return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_IPC_NUM_RESP,&tMsgInfo,1);
}

static int ParseSetPecuConnInfo(void *arg,char *pcData, int iDataLen)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	char acKey[32] = {0};
	int iRet = 0;
	int i = 0;
	int iVideoIdx = 0;
	T_PECU_CONN_INFO *ptPecuConnInfo = (T_PECU_CONN_INFO *)pcData;
			
	tMsgInfo.acMsgData[0] = 1;
	if(iDataLen != sizeof(T_PECU_CONN_INFO))
	{
		tMsgInfo.acMsgData[0] = 2;
		return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_PECU_CONN_RESP,&tMsgInfo,1);
	}
	
	for(i=0;i<sizeof(ptPecuConnInfo->acImgIdx);i++)
	{
	   	if(pcData[i] <1 || pcData[i]>32) //图片序号必须在1-32之间
	   	{
	   		tMsgInfo.acMsgData[0] = 2;
			break;
	   	}
		memset(acData,0,sizeof(acData));
		memset(acKey,0,sizeof(acKey));
	   	iVideoIdx = g_acImg2VideoIdx[pcData[i]-1];
		if(iVideoIdx != g_acPecuVideoIdx[i])
		{
			snprintf(acData,sizeof(acData)-1,"%d",iVideoIdx+1);
			snprintf(acKey,sizeof(acKey)-1,"PECU%d",i+1);
	   		iRet = ModifyParam(g_acConfigIniName,"[PECUCONFIG]",acKey,acData);
			if(iRet <0)
			{
				tMsgInfo.acMsgData[0] = 2;
				break;
			}
			else
			{
				g_acPecuVideoIdx[i] = iVideoIdx;
			}
		}
	}
	
	return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_PECU_CONN_RESP,&tMsgInfo,1);
}

static int ParseSetFireConnInfo(void *arg,char *pcData, int iDataLen)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	char acKey[32] = {0};
	int iRet = 0;
	int i = 0;
	int iVideoIdx = 0;
	T_FIRE_CONN_INFO *ptFireConnInfo = (T_FIRE_CONN_INFO *)pcData;

	if(iDataLen != sizeof(T_FIRE_CONN_INFO))
	{
		tMsgInfo.acMsgData[0] = 2;
		return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_FIRE_CONN_RESP,&tMsgInfo,1);
	}
	
	tMsgInfo.acMsgData[0] = 1;
	for(i = 0;i<sizeof(ptFireConnInfo->acImgIdx);i++)
	{
		if(pcData[i] <1 || pcData[i]>32)
	   	{
	   		tMsgInfo.acMsgData[0] = 2;
			break;
	   	}
		memset(acData,0,sizeof(acData));
		memset(acKey,0,sizeof(acKey));
	   	iVideoIdx = g_acImg2VideoIdx[pcData[i]-1];
		if(iVideoIdx != g_acFireVideoIdx[i])
		{
			snprintf(acData,sizeof(acData)-1,"%d",iVideoIdx+1);
			snprintf(acKey,sizeof(acKey)-1,"FIRE%d",i+1);
	   		iRet = ModifyParam(g_acConfigIniName,"[FIRECONFIG]",acKey,acData);
			if(iRet <0)
			{
				tMsgInfo.acMsgData[0] = 2;
				break;
			}
			else
			{
				g_acFireVideoIdx[i] = iVideoIdx;
			}
		}
	}
	return	SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_FIRE_CONN_RESP,&tMsgInfo,1);
}

static int ParseSetDoorConnInfo(void *arg,char *pcData, int iDataLen)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	char acKey[32] = {0};
	int iRet = 0;
	int i = 0;
	int iVideoIdx = 0;
	T_DOOR_CONN_INFO *ptDoorConnInfo = (T_DOOR_CONN_INFO *)pcData;

	if(iDataLen != sizeof(T_DOOR_CONN_INFO))
	{
		tMsgInfo.acMsgData[0] = 2;
		return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_DOOR_CONN_RESP,&tMsgInfo,1);
	}
	tMsgInfo.acMsgData[0] = 1;
	for(i=0;i<48;i++)
	{
	   	if(pcData[i] <1 || pcData[i]>32)
	   	{
	   		tMsgInfo.acMsgData[0] = 2;
			break;
	   	}
		memset(acData,0,sizeof(acData));
		memset(acKey,0,sizeof(acKey));
	   	iVideoIdx = g_acImg2VideoIdx[pcData[i]-1];
		
		if(iVideoIdx != g_acDoorVideoIdx[i])
		{
			snprintf(acData,sizeof(acData)-1,"%d",iVideoIdx+1);
			snprintf(acKey,sizeof(acKey)-1,"DOOR%d%d",i/8+1,i%8+1);
	   		iRet = ModifyParam(g_acConfigIniName,"[DOORCONFIG]",acKey,acData);
			if(iRet <0)
			{
				tMsgInfo.acMsgData[0] = 2;
				break;
			}
			else
			{
				g_acDoorVideoIdx[i] = iVideoIdx;
			}
		}
	}
	return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_DOOR_CONN_RESP,&tMsgInfo,1);
}

static int RespIpcNum(void *arg)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	T_IPC_NUM_RESP_INFO *ptIpcNumRespInfo = (T_IPC_NUM_RESP_INFO *)(tMsgInfo.acMsgData);
	int iRet = 0;
	
	ptIpcNumRespInfo->cFlag = 2;
			
	memset(acData,0,sizeof(acData));
	iRet = ReadParam(g_acConfigIniName, "[IPCINFO]", "IPCNUM", acData);
		
	if(iRet >0)
	{
		g_iVideoNum = atoi(acData);
		if( 28 == g_iVideoNum || 32 == g_iVideoNum)
		{
			ptIpcNumRespInfo->cFlag = 1;
			ptIpcNumRespInfo->cIpcNum = g_iVideoNum;
		}
	}
	return SendPmsgInfo(iSocket,MSG_SERV2CLI_GET_IPC_NUM_RESP,&tMsgInfo,sizeof(T_IPC_NUM_RESP_INFO));
}

static int RespPECUConnInfo(void *arg)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	char acKey[32] = {0};
	int iRet = 0;
	int i = 0;
	int iVideoIdx = 0;
	T_PECU_RESP_INFO *ptPecuRespInfo = (T_PECU_RESP_INFO *)tMsgInfo.acMsgData;

	ptPecuRespInfo->cFlag = 1;
    for(i =0;i<sizeof(ptPecuRespInfo->tPecuInfo);i++)
    {
    	memset(acData,0,sizeof(acData));
		memset(acKey,0,sizeof(acKey));
		snprintf(acKey,sizeof(acKey)-1,"PECU%d",i+1);
		iRet = ReadParam(g_acConfigIniName,"[PECUCONFIG]",acKey, acData);
		if(iRet<=0)
		{
		   	ptPecuRespInfo->cFlag = 2;
			break;
		}
				
		iVideoIdx = atoi(acData)-1;
		g_acPecuVideoIdx[i] = iVideoIdx;
		if(iVideoIdx <0 || iVideoIdx >31)
		{
			ptPecuRespInfo->cFlag = 2;
			break;
		}
		ptPecuRespInfo->tPecuInfo.acImgIdx[i] = g_acIpcInfo[iVideoIdx].iImgIndex;
    }
	return SendPmsgInfo(iSocket,MSG_SERV2CLI_GET_PECU_CONN_RESP,&tMsgInfo,sizeof(T_PECU_RESP_INFO));
}

static int RespFireConnInfo(void *arg)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	char acKey[32] = {0};
	int iRet = 0;
	int i = 0;
	int iVideoIdx = 0;
	T_FIRE_RESP_INFO *ptFireRespInfo = (T_FIRE_RESP_INFO *)(tMsgInfo.acMsgData);

	ptFireRespInfo->cFlag = 1;
    for(i =0;i<6;i++)
    {
    	memset(acData,0,sizeof(acData));
		memset(acKey,0,sizeof(acKey));
		snprintf(acKey,sizeof(acKey)-1,"FIRE%d",i+1);
		iRet = ReadParam(g_acConfigIniName,"[FIRECONFIG]",acKey, acData);
		if(iRet<=0)
		{
		   	ptFireRespInfo->cFlag = 2;
			break;
		}
		iVideoIdx = atoi(acData)-1;
		g_acFireVideoIdx[i] = iVideoIdx;
		if(iVideoIdx <0 || iVideoIdx >31)
		{
			ptFireRespInfo->cFlag = 2;
			break;
		}
		ptFireRespInfo->tFireInfo.acImgIdx[i] = g_acIpcInfo[iVideoIdx].iImgIndex;
    }
	return SendPmsgInfo(iSocket,MSG_SERV2CLI_GET_FIRE_CONN_RESP,&tMsgInfo,sizeof(T_FIRE_RESP_INFO));
}

static int ParseSetCameraIpInfo(void *arg,char *pcData, int iDataLen)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[512] = {0};
	char acKey[32] = {0};
	int iRet = 0;
	int i = 0;
	int iVideoIdx = 0;
	int iNum = ONVIF_GetChNum();
	ST_ONVIF_CAMINFO_SET  stParam;
	T_IP_RTSP_INFO* pOnvifCamerInfo = NULL;
	int  iValidCameraNum = 0; 
	T_CAMERA_IP_INFO *ptRecvCamerIpData = (T_CAMERA_IP_INFO *)pcData;
	
	if(0 == iNum)
	{
		tMsgInfo.acMsgData[0] = 1;
		return SendPmsgInfo(iSocket,MSG_SERV2CLI_GET_DOOR_CONN_RESP,&tMsgInfo,1);
	}

	if(iDataLen != sizeof(T_CAMERA_IP_INFO))
	{
		tMsgInfo.acMsgData[0] = 2;
		return SendPmsgInfo(iSocket,MSG_SERV2CLI_GET_DOOR_CONN_RESP,&tMsgInfo,1);
	}
	
	pOnvifCamerInfo = (T_IP_RTSP_INFO*)malloc(iNum*sizeof(T_IP_RTSP_INFO));
	
		
	for(i=0;i<iNum;i++)
	{
		memset(&stParam, 0x0, sizeof(ST_ONVIF_CAMINFO_SET));
        if( 0 == ONVIF_GetChInfo(i, &stParam) )
        {
            char acIp[16]={0};

            if(1 == GetIPAddrFromRtspAddr(stParam.stCamSubVideo.acRtspUri,acIp))
            {
            	T_IP_RTSP_INFO *ptOnvifIpRtsp = &pOnvifCamerInfo[iValidCameraNum];

				memcpy(ptOnvifIpRtsp->acIp,acIp,16);
				memcpy(ptOnvifIpRtsp->acRtsp,stParam.stCamSubVideo.acRtspUri,128);
                memcpy(ptOnvifIpRtsp->acMainRtsp,stParam.stCamMainVideo.acRtspUri,128);
                printf("acMainRtsp:%s\n",ptOnvifIpRtsp->acMainRtsp);
				iValidCameraNum ++;
           }
        }
	}

	for(i=0;i<32;i++)
	{
		char *pcRecvCamerIp = ptRecvCamerIpData->acCameraIpInfo[i];
		int j = 0;

		if(0 == pcRecvCamerIp[0])
		{
			continue ;
		}
		for(j=0;j<iValidCameraNum;j++)
		{
			T_IP_RTSP_INFO *ptOnvifIpRtsp = &pOnvifCamerInfo[j];
			
			if(0 == strcmp(pcRecvCamerIp,ptOnvifIpRtsp->acIp))
			{
				int iVideoIdx = g_acImg2VideoIdx[i];

				if(0 != strcmp(ptOnvifIpRtsp->acRtsp,g_acIpcInfo[iVideoIdx].acRtspAddr))
				{
					T_IPC_INFO *ptLocalIpcInfo = &g_acIpcInfo[iVideoIdx];
		
					memset(acData,0,sizeof(acData));
					memset(acKey,0,sizeof(acKey));
				
					snprintf(acData,sizeof(acData)-1
					,"%d+%d+%d+%d+%s+%s",ptLocalIpcInfo->iNvrNO,ptLocalIpcInfo->iUiGroup
					,ptLocalIpcInfo->iUiPos,ptLocalIpcInfo->iImgIndex,ptOnvifIpRtsp->acRtsp,
					ptOnvifIpRtsp->acMainRtsp);

					snprintf(acKey,sizeof(acKey)-1,"IPC%d",iVideoIdx+1);
	   			    iRet = ModifyParam(g_acConfigIniName,"[IPCINFO]",acKey,acData);
				   	if(iRet >= 0)
				   	{
					 	memcpy(g_acIpcInfo[iVideoIdx].acRtspAddr,ptOnvifIpRtsp->acRtsp,128);
                        memcpy(g_acIpcInfo[iVideoIdx].acMainRtspAddr,ptOnvifIpRtsp->acMainRtsp,128);
				   	}
				}
				break;
			}
		}
	}

	free(pOnvifCamerInfo);
	pOnvifCamerInfo = NULL;
	
	tMsgInfo.acMsgData[0] =1;
	return SendPmsgInfo(iSocket,MSG_SERV2CLI_GET_DOOR_CONN_RESP,&tMsgInfo,1);
}

static int RespCameraRtsp(void *arg)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	int i = 0;
	T_CAMERA_RTSP_INFO *ptCamerRtspData = (T_CAMERA_RTSP_INFO *)tMsgInfo.acMsgData;

	for(i=0;i<32;i++)
	{
		int iVideoIdx = g_acImg2VideoIdx[i];
		char *pcRtsp = ptCamerRtspData->acCameraRtspInfo[i];

		memcpy(pcRtsp,g_acIpcInfo[iVideoIdx].acRtspAddr,128);
	}
	
	return SendPmsgInfo(iSocket,MSG_SERV2CLI_GET_CAMERA_RTSP_RESP,&tMsgInfo,sizeof(T_CAMERA_RTSP_INFO));
}

static int ParseSetIpcUserPass(void *arg,char *pcData, int iDataLen)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	char acKey[32] = {0};
	int iRet = 0;
	int i = 0;
	int iVideoIdx = 0;
	T_MODIFY_USER_PARAM *ptUserPassParam = (T_MODIFY_USER_PARAM *)pcData;

	tMsgInfo.acMsgData[0] = 1;
	if(iDataLen != sizeof(T_MODIFY_USER_PARAM))
	{
		tMsgInfo.acMsgData[0] = 2;
		return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_CAMEAR_PASS_RESP,&tMsgInfo,1);
	}

	if(strcmp(g_acUser,ptUserPassParam->acNewUser))
	{
		iRet = ModifyParam(g_acConfigIniName,"[IPCACCOUNT]","USER",ptUserPassParam->acNewUser);
	}
	
	if(iRet <0)
	{
		tMsgInfo.acMsgData[0] = 2;
	}
	else
	{
		memset(g_acUser,0,sizeof(g_acUser));
		strncpy(g_acUser,ptUserPassParam->acNewUser,sizeof(g_acUser)-1);

		if(strcmp(g_acPassword,ptUserPassParam->acNewPswd))
		{
			iRet = ModifyParam(g_acConfigIniName,"[IPCACCOUNT]","PASSWORD",ptUserPassParam->acNewPswd);
		}
		if(iRet <0)
		{
			tMsgInfo.acMsgData[0] =2;
		}
		else
		{
			memset(g_acPassword,0,sizeof(g_acPassword));
			strncpy(g_acPassword,ptUserPassParam->acNewPswd,sizeof(g_acUser)-1);
		}
	}
	return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_CAMEAR_PASS_RESP,&tMsgInfo,1);
}

static int RespDoorConnInfo(void *arg)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	char acKey[32] = {0};
	int iRet = 0;
	int i = 0;
	int iVideoIdx = 0;
	T_DOOR_RESP_INFO *ptDoorRespInfo = (T_DOOR_RESP_INFO *)(tMsgInfo.acMsgData);

	ptDoorRespInfo->cFlag = 1;
    for(i =0;i<sizeof(T_DOOR_CONN_INFO);i++)
    {
    	memset(acData,0,sizeof(acData));
		memset(acKey,0,sizeof(acKey));
		snprintf(acKey,sizeof(acKey)-1,"DOOR%d%d",i/8+1,i%8+1);
		iRet = ReadParam(g_acConfigIniName,"[DOORCONFIG]",acKey, acData);
		if(iRet<=0)
		{
		    ptDoorRespInfo->cFlag = 2;
			break;
		}
		iVideoIdx = atoi(acData)-1;
		g_acDoorVideoIdx[i] = iVideoIdx;
		if(iVideoIdx <0 || iVideoIdx >31)
		{
			ptDoorRespInfo->cFlag = 2;
			break;
		}
		ptDoorRespInfo->tDoorInfo.acImgIdx[i] = g_acIpcInfo[iVideoIdx].iImgIndex;
    }
	return SendPmsgInfo(iSocket,MSG_SERV2CLI_GET_DOOR_CONN_RESP,&tMsgInfo,sizeof(T_DOOR_RESP_INFO));
}

static int SetOnvifRunningIpAddr()
{
	int inet_sock;  
	char acIp[16] = {0};
	extern struct in_addr g_if_req; 
    struct ifreq ifr;  
	const char *addrs[] = {"eth0", "eth1"};
	for(int i = 0; i < 2; i++)
	{
		inet_sock = socket(AF_INET, SOCK_DGRAM, 0);
		strcpy(ifr.ifr_name, addrs[i]);  
		ioctl(inet_sock, SIOCGIFFLAGS, &ifr);
		if(0 == (ifr.ifr_flags & IFF_RUNNING))
		{
			close(inet_sock);
			continue;
		}
		memset(acIp, 0, sizeof(acIp));
		ioctl(inet_sock, SIOCGIFADDR, &ifr);  
		strncpy(acIp, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr),sizeof(acIp)-1);
		close(inet_sock);
		printf("ifr.ifr_flags:%d ,%s, %s \n", ifr.ifr_flags, addrs[i], acIp);
		if(strlen(acIp))
		   	g_if_req.s_addr = inet_addr(acIp);
	}
	return 0;
}

static int InitOnvif()
{
	int inet_sock;  
	char acIp[16] = {0};
	
    struct ifreq ifr;  
	const char *addrs[] = {"eth0", "eth1"};
	for(int i = 0; i < 2; i++)
	{
		inet_sock = socket(AF_INET, SOCK_DGRAM, 0);
		strcpy(ifr.ifr_name, addrs[i]);  
		ioctl(inet_sock, SIOCGIFFLAGS, &ifr);
		if(0 == (ifr.ifr_flags & IFF_RUNNING))
		{
			close(inet_sock);
			continue;
		}
		ioctl(inet_sock, SIOCGIFADDR, &ifr);  
		strncpy(acIp, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr),sizeof(acIp)-1);
		close(inet_sock);
	}
	
	ONVIF_Init(g_acUser,g_acPassword,acIp,NULL);
	return 0;
}

static int RespDispType(void *arg)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	T_DISP_TYPE_RESP_INFO *ptDispTypeRespInfo = (T_DISP_TYPE_RESP_INFO *)(tMsgInfo.acMsgData);
	int iRet = 0;
	
	ptDispTypeRespInfo->cFlag = 2;
			
	memset(acData,0,sizeof(acData));
	iRet = ReadParam("/mnt/mmc/dhmi/displayconfig.ini", "[DisplayConfig]", "DisplayMode", acData);
			
	if(iRet >0)
	{
		g_iDispType = atoi(acData);
		if( g_iDispType >0 && g_iDispType <4)
		{
			ptDispTypeRespInfo->cFlag = 1;
			ptDispTypeRespInfo->cDispType = g_iDispType;
		}
	}
	return SendPmsgInfo(iSocket,MSG_SERV2CLI_GET_DISP_TYPE_RESP,&tMsgInfo,sizeof(T_DISP_TYPE_RESP_INFO));
}

static int ParseSetDispType(void *arg,char *pcData, int iDataLen)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
	T_MSG_INFO tMsgInfo={0};
	char acData[64] = {0};
	int iRet = 0;

	tMsgInfo.acMsgData[0] = 2;
	if(1 == iDataLen)
	{
		if(pcData[0]<4 && pcData[0]>0 )
		{
			tMsgInfo.acMsgData[0] = 1;
			if(g_iDispType != pcData[0])
			{
				FILE *fp = NULL;
			
				memset(acData,0,sizeof(acData));
				snprintf(acData,sizeof(acData)-1,"%d",pcData[0]);

				fp = fopen("/mnt/mmc/dhmi/displayconfig.ini", "rb");
				if(NULL == fp)
				{
					if(access("/mnt/mmc/dhmi", F_OK )!=0 )  
  					{  
      					if(mkdir("/mnt/mmc/dhmi", 0755)== -1)  
      					{  
                			return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_DISP_TYPE_RESP,&tMsgInfo,1);
      					}
      				} 
					fp = fopen("/mnt/mmc/dhmi/displayconfig.ini", "ab");
				}

				if(NULL == fp )
				{
					return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_DISP_TYPE_RESP,&tMsgInfo,1);
				}

				fclose(fp);
	   			iRet = ModifyParam("/mnt/mmc/dhmi/displayconfig.ini","[DisplayConfig]","DisplayMode",acData);
				if(0 == iRet)
				{
					g_iDispType = pcData[0];	
					tMsgInfo.acMsgData[0] = 1;
				}
				else
				{
					tMsgInfo.acMsgData[0] = 2;
				}
			}
		}
	}
   return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_DISP_TYPE_RESP,&tMsgInfo,1);
}

static int ParseBeginLoadCfgFile(void *arg,char *pcData, int iDataLen)
{
    T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
    T_BEGIN_LOAD_CFG *ptLoadInfo = (T_BEGIN_LOAD_CFG *)pcData;
    T_MSG_INFO tMsgInfo={0};
    FILE *pFile = NULL;

    memset(g_acFileFullName,0,sizeof(g_acFileFullName));
    snprintf(g_acFileFullName,sizeof(g_acFileFullName)-1,"%s/%s",CONFIG_FILE_DIR, ptLoadInfo->acFileName);

    pFile = fopen(g_acFileFullName, "wt+");
    g_iFileTotalSize = ntohl(ptLoadInfo->iFileSize);
    
    if(NULL == pFile )
    {
        tMsgInfo.acMsgData[0] = 2;
        return SendPmsgInfo(iSocket,MSG_SERV2CLI_BEGIN_LOAD_CFG_RESP,&tMsgInfo,1);
    }
    else
    {
        tMsgInfo.acMsgData[0] = 1;
        fclose(pFile);
        return SendPmsgInfo(iSocket,MSG_SERV2CLI_BEGIN_LOAD_CFG_RESP,&tMsgInfo,1);
    }
}

static int ParseCfgFile(void *arg,char *pcData, int iDataLen)
{
    T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
    T_LOAD_CFG *ptCfgFile = (T_LOAD_CFG *)pcData;
    T_MSG_INFO tMsgInfo={0};
    FILE *pFile = NULL;

    pFile = fopen(g_acFileFullName, "at");
    if(pFile)
    {
        int iLen = (ntohs)(ptCfgFile->sPktLen);
        int iSize = 0;
        
        iSize = fwrite(ptCfgFile->acData,1,iLen,pFile);
        fclose(pFile);
        
        if(iSize == iLen)
        {
            tMsgInfo.acMsgData[0] = 1;
            return SendPmsgInfo(iSocket,MSG_SERV2CLI_LOAD_CFG_RESP,&tMsgInfo,1);
        }
    }
    
    tMsgInfo.acMsgData[0] = 2;
    return SendPmsgInfo(iSocket,MSG_SERV2CLI_LOAD_CFG_RESP,&tMsgInfo,1);
}

static int ParseEndCfgFile(void *arg,char *pcData, int iDataLen)
{
    T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	int iSocket = ptConnInfo->iSockfd;
    T_LOAD_CFG *ptCfgFile = (T_LOAD_CFG *)pcData;
    T_MSG_INFO tMsgInfo={0};
    FILE *pFile = NULL;

    pFile = fopen(g_acFileFullName, "at");
    if(pFile)
    {
        int iSize = 0;

        fflush(pFile);
	    fsync(fileno(pFile));

        fseek(pFile,0,SEEK_END);
        iSize = ftell(pFile);
        fclose(pFile);
        
        if(iSize == g_iFileTotalSize)
        {
            tMsgInfo.acMsgData[0] = 1;
            return SendPmsgInfo(iSocket,MSG_SERV2CLI_END_LOAD_CFG_RESP,&tMsgInfo,1);
        }
    }
    
    tMsgInfo.acMsgData[0] = 2;
    return SendPmsgInfo(iSocket,MSG_SERV2CLI_END_LOAD_CFG_RESP,&tMsgInfo,1);
}


static int ParseCycTime(void *arg,char *pcData, int iDataLen)
{
    T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
    char acData[12];
    unsigned short usTime = (((unsigned short)pcData[0]<<8) | pcData[1]);
    int iRet = 0;
    int iSocket = ptConnInfo->iSockfd;
    T_MSG_INFO tMsgInfo={0};
    
    tMsgInfo.acMsgData[0] = 2;
    memset(acData,0,sizeof(acData));
	snprintf(acData,sizeof(acData)-1,"%d",usTime);

    iRet = ModifyParam(g_acConfigIniName,"[CYCTIME]","time",acData);
    if(0 == iRet)
	{
		tMsgInfo.acMsgData[0] = 1;
    }
    return SendPmsgInfo(iSocket,MSG_SERV2CLI_SET_CYCTIME_RESP,&tMsgInfo,1);
}


int PmsgProcFun(void *arg, unsigned char ucMsgCmd, char *pcData, int iDataLen)
{
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
	
	switch (ucMsgCmd)
    {
    	case  MSG_CLI2SERV_HEART:
			  break;
    	case MSG_CLI2SERV_SET_IPC_NUM:
    	{
			ParseSetIpcNumInfo(arg,pcData,iDataLen);
			break;		
		}
    	case MSG_CLI2SERV_SET_PECU_CONN:
		{	
			ParseSetPecuConnInfo(arg,pcData,iDataLen);
			break;
		}
    	case MSG_CLI2SERV_SET_FIRE_CONN:
		{
			ParseSetFireConnInfo(arg,pcData,iDataLen);
			break;
		}
    	case MSG_CLI2SERV_SET_DOOR_CONN :
		{
			ParseSetDoorConnInfo(arg,pcData,iDataLen);
       		break;
    	}
		case MSG_CLI2SERV_KILL_CCTV:
		{
			system("reboot");
			break;
		}
		case MSG_CLI2SERV_GET_IPC_NUM :
		{
			RespIpcNum(arg);
		    break;	
		}	
    	case MSG_CLI2SERV_GET_PECU_CONN :
    	{
			RespPECUConnInfo(arg);
			break;
    	}
    	case MSG_CLI2SERV_GET_FIRE_CONN:
    	{
			RespFireConnInfo(arg);
			break;
    	}
    	case MSG_CLI2SERV_GET_DOOR_CONN :
		{
    		RespDoorConnInfo(arg);
			break;
    	}
		case MSG_CLI2SERV_GET_ONLINE_CAMERA:
		{
			if(0 == g_iOnvifInit)
			{
				g_iOnvifInit = 1;
				InitOnvif();
			}
			//SetOnvifRunningIpAddr();
			ONVIF_StartProbe();
			ptConnInfo->iOnvifNeedResp = 1;
			ptConnInfo->time = time(NULL);
			break;
		}
		case MSG_CLI2SERV_SET_CAMERA_IP:
		{
			ParseSetCameraIpInfo(arg,pcData,iDataLen);
			break;
		}
		case MSG_CLI2SERV_GET_CAMERA_RTSP:
		{
			RespCameraRtsp(arg);
			break;	
		}
		case MSG_CLI2SERV_SET_CAMEAR_PASS:
		{
			ParseSetIpcUserPass(arg,pcData,iDataLen);
			break;
		}
		case MSG_CLI2SERV_GET_DISP_TYPE:
		{
			RespDispType(arg);
			break;
		}
		case MSG_CLI2SERV_SET_DISP_TYPE:
		{
			ParseSetDispType(arg,pcData,iDataLen);
			break;
		}
        case MSG_CLI2SERV_BEGIN_LOAD_CFG:
        {
            ParseBeginLoadCfgFile(arg,pcData,iDataLen);
            break;
        }
        case MSG_CLI2SERV_LOAD_CFG:
        {
            ParseCfgFile(arg,pcData,iDataLen);
            break;
        }
        case MSG_CLI2SERV_END_LOAD_CFG:
        {
            ParseEndCfgFile(arg,pcData,iDataLen);
            break;
        }
        case MSG_CLI2SERV_SET_CYCTIME:
        {
            ParseCycTime(arg,pcData,iDataLen);
            break;
        }
	  	default:
       	{
            break;	
       	}
    }
	return 0;
}

void *MsgThread(void *arg)
{
    PT_MSG_INFO ptMsg = NULL;
	T_CONN_INFO *ptConnInfo = (T_CONN_INFO *)arg;
    int iConnSocket = ptConnInfo->iSockfd;
    struct timeval tv0,tv1;
	fd_set	tTmpSet;
	char *pcRecvBuf = NULL;     //用来接收数据
    char *pcLeaveBuf = NULL;    //用来保存上次未解析完的数据
    char *pcMsgBuf = NULL;      //信息的数据位置
    unsigned int iPreLeaveLen = 0;   //一条完整信息前面已收到的数据长度
    unsigned int iLeaveLen = 0;      //收到的数据中还未解析的数据长度
    int iRecvLen = 0;       //本次recv到的数据长度
    int iOffset = 0;        //接收到的数据中还未解析的数据的开始位置
    int iBufLen = 10240;
	unsigned int iDataLen = 0;    //完整的一条信息的数据长度
	int iCount = 0;
	time_t t = 0;

    pthread_detach(pthread_self());
    printf("[%s] sock %d\n", __FUNCTION__, iConnSocket);
    
    tv0.tv_sec = 10;
    tv0.tv_usec = 0;

    if (setsockopt(iConnSocket, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv0, sizeof(tv0)))
    {
         perror("setsockopt");
    }
    
    ptMsg = (PT_MSG_INFO)malloc(sizeof(T_MSG_INFO));
    if (NULL == ptMsg)
    {
        return NULL;	
    }
	pcRecvBuf = (char *)malloc(iBufLen);
    if (NULL == pcRecvBuf)
    {
        return NULL;        	
    }
    memset(pcRecvBuf, 0, iBufLen);
    
    pcLeaveBuf = (char *)malloc(iBufLen);
    if (NULL == pcLeaveBuf)
    {
        free(pcRecvBuf);
        return NULL;        	
    }
    memset(pcLeaveBuf, 0, iBufLen);
	
    while (1)
    {
        tv1.tv_sec = 0;
        tv1.tv_usec = 20000;

		FD_ZERO(&tTmpSet);
        FD_SET(iConnSocket, &tTmpSet);
       if (select(iConnSocket + 1, &tTmpSet, NULL, NULL,&tv1) > 0)
       {
            if (FD_ISSET(iConnSocket, &tTmpSet))
            {
                iRecvLen = recv(iConnSocket, &pcRecvBuf[iPreLeaveLen], iBufLen - iPreLeaveLen - 1, 0);
                if (iRecvLen <= 0)
                {
                    perror("recv:");  
                    break;
                }
                iCount = 0;
                if (iPreLeaveLen > 0)
                {
                    memcpy(pcRecvBuf, pcLeaveBuf, iPreLeaveLen);
                }
                
                iLeaveLen = iRecvLen + iPreLeaveLen;
                iOffset = 0;
                while (iLeaveLen > 0)
                {
                    if (iLeaveLen < 5)
                    {
                        memcpy(pcLeaveBuf, &pcRecvBuf[iOffset], iLeaveLen);
                        iPreLeaveLen = iLeaveLen;
                        break;    	
                    }
                        
                    pcMsgBuf = &pcRecvBuf[iOffset];
                   
                    // 验证消息头的正确性
                    if (MSG_MAGIC_FLAG != (unsigned char)pcMsgBuf[0])
                    {
                        iPreLeaveLen = 0;
                        break;
                    }
                        
                    // 验证消息长度的正确性
                    
                    iDataLen = (((short)pcMsgBuf[2])<<8)|pcMsgBuf[3];
                    if (iDataLen > 10240)
                    {
                        iPreLeaveLen = 0;
                        break;
                    }
                        
                    if (iDataLen <= iLeaveLen - 5)
                    {
                       
                        PmsgProcFun(arg, pcMsgBuf[1], &pcMsgBuf[4], iDataLen);
                        
                        iLeaveLen -= iDataLen + 5;
                        iOffset += iDataLen + 5;
                        iPreLeaveLen = 0;
                        continue;
                    }
                    else
                    {
                        memcpy(pcLeaveBuf, &pcRecvBuf[iOffset], iLeaveLen);
                        iPreLeaveLen = iLeaveLen;
                        break;	
                    }
                }
            }
        }
	   	else
	   	{
	   	 	iCount++;
		 	if(iCount >500)
		 	{
		 		break;
		 	}
	   	}
		if(ptConnInfo->iOnvifNeedResp)
		{
			t = time(NULL);
			if((t - ptConnInfo->time)>9)
			{
				int iNum = ONVIF_GetChNum();
				ST_ONVIF_CAMINFO_SET  stParam;
				T_MSG_INFO tMsgInfo={0};
				int i=0;
				int iSendLen = 1;
				char cPackageNum = 0;
	
				ptConnInfo->iOnvifNeedResp = 0;
				for(i=0;i<iNum && i<MAX_ONVIF_CAM_NUM;i++)
				{
					memset(&stParam, 0x0, sizeof(ST_ONVIF_CAMINFO_SET));
            		if( 0 == ONVIF_GetChInfo(i, &stParam) )
            		{
            			char acIp[16]={0};
						
            			if(1 == GetIPAddrFromRtspAddr(stParam.stCamSubVideo.acRtspUri,acIp))
            			{
            				memcpy(&tMsgInfo.acMsgData[iSendLen],acIp,16);
            				iSendLen += 16;
						    cPackageNum ++;
            			}
            		}
				}
                DebugPrint(ONVIF_DEBUG_LEVEL, "Device Probe Num = %d\n", 
                    iNum);
				tMsgInfo.acMsgData[0] = cPackageNum;
				SendPmsgInfo(ptConnInfo->iSockfd,MSG_SERV2CLI_GET_ONLINE_CAMERA_RESP,&tMsgInfo,iSendLen);
			}
		}
    }
    
NET_ERR:    
    if (iConnSocket > 0)
    {
        close(iConnSocket);
		iConnSocket = 0;
    }
    if (pcRecvBuf)
    {
        free(pcRecvBuf);
        pcRecvBuf = NULL;	
    }
    
    if (pcLeaveBuf)
    {
        free(pcLeaveBuf);
        pcLeaveBuf = NULL;	
    }
    return NULL;
}


void *StartMsgListenThread(void *arg)
{
    int iMsgSock = 0, iConnSock = 0;
    struct sockaddr_in servaddr;
    struct sockaddr_in cliaddr;
    int clilen = sizeof(cliaddr);
    int iRet = 0;
    int flag = 1;
    int iMaxFd = 0;
    fd_set	tAllSet, tTmpSet;
    struct timeval tv;

    iMsgSock = socket(AF_INET, SOCK_STREAM, 0);
    if (iMsgSock < 0)
    {
        perror("socket error:");
        return NULL;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(MSG_TCP_PORT);

    iRet = setsockopt(iMsgSock, SOL_SOCKET, SO_REUSEADDR, (char*)&flag, sizeof(int));
    if(iRet != 0)
    {
        printf("ERR: setsockopt  socket error. err = %d\n",iRet);
        close(iMsgSock);
        return NULL;
    }
    iRet = bind(iMsgSock, (struct sockaddr *)&servaddr, sizeof(servaddr));
    if (iRet < 0)
    {
        perror(":");
        return NULL;
    }

    listen(iMsgSock, 5);
    
    FD_ZERO(&tAllSet);
    FD_SET(iMsgSock, &tAllSet);
    iMaxFd = iMsgSock;
    

    while (1)
    {
        tv.tv_sec = 5;
        tv.tv_usec = 0;
    		tTmpSet = tAllSet;	
        if (select(iMaxFd + 1, &tTmpSet, NULL, NULL, &tv) > 0)
        {
            if (FD_ISSET(iMsgSock, &tTmpSet))
            {
            
                iConnSock = accept(iMsgSock, (struct sockaddr *)&cliaddr, &clilen);
                if (iConnSock > 0)
                {
                    // creat thread or fork process
                    T_CONN_INFO *PConnInfo = (T_CONN_INFO*)malloc(sizeof(T_CONN_INFO));
                    pthread_t tid = 0;

					memset(PConnInfo,0,sizeof(PConnInfo));
					PConnInfo->iSockfd = iConnSock;
					printf("event process have a new connect\n");
                    pthread_create(&tid, NULL, MsgThread, PConnInfo);
                }
            }

        }
        usleep(5000);
    }
    
    close(iMsgSock);
    
    return NULL;
}

static int ParseIpcInfo(char *pcParseStr, T_IPC_INFO *ptIpcInfo)
{
	char *pcPos = strtok(pcParseStr,"+");
	int iIndex = 0;
	int iRet = 0;
	while(pcPos)
	{
		if(0 == iIndex)
		{
			ptIpcInfo->iNvrNO = atoi(pcPos);
			if(ptIpcInfo->iNvrNO <1 || ptIpcInfo->iNvrNO >6)
			{
				iRet = -1;
			}
		}
		else if(1 == iIndex)
		{
			ptIpcInfo->iUiGroup = atoi(pcPos);
			if(ptIpcInfo->iUiGroup <1 || ptIpcInfo->iUiGroup >8)
			{
				iRet = -2;
			}
		}
		else if(2 == iIndex)
		{
			ptIpcInfo->iUiPos = atoi(pcPos);
			if(ptIpcInfo->iUiPos <1 || ptIpcInfo->iUiPos >4)
			{
				iRet = -3;
			}
		}
		else if(3 == iIndex)
		{
			ptIpcInfo->iImgIndex = atoi(pcPos);
			if(ptIpcInfo->iImgIndex <1 || ptIpcInfo->iImgIndex > 32)
			{
				iRet = -4;
			}
		}
		else if(4 == iIndex)
		{	
			strcpy(ptIpcInfo->acRtspAddr,pcPos);
			//ParseRtspUrl(pcPos,ptIpcInfo->acRtspAddr,ptIpcInfo->acUser,ptIpcInfo->acPassword);
		
		}
        else if(5 == iIndex)
        {
            strcpy(ptIpcInfo->acMainRtspAddr,pcPos);
            //AddUserPasswordToRtsp(pcPos,ptIpcInfo->acMainRtspAddr,sizeof(ptIpcInfo->acMainRtspAddr),pcUser,pcPswd);
            break;
        }
		pcPos = strtok(NULL,"+");
		iIndex ++;
	}
	if((iIndex != 4 && iIndex!= 5)|| iRet <0)
	{
		printf("parseIpcInfo err iIndex =%d iRet =%d \n",iIndex,iRet);
		return -1;
	}
	
	return 0;
}

static int ReadIpcImgIdx()
{
	char acParseStr[256] = {0};
	char acKeyValue[24] = {0};
	int iRet = 0;
	int i;


	ReadParam(g_acConfigIniName, "[IPCACCOUNT]", "USER", g_acUser);
	
	ReadParam(g_acConfigIniName, "[IPCACCOUNT]", "PASSWORD", g_acPassword);
	
	iRet = ReadParam(g_acConfigIniName, "[IPCINFO]", "IPCNUM", acParseStr);
	if(iRet > 0 )
	{
		g_iVideoNum = atoi(acParseStr);
	}
	
	for(i=0;i<32;i++)
	{		
		memset(acParseStr,0,sizeof(acParseStr));
		memset(acKeyValue,0,sizeof(acKeyValue));
		sprintf(acKeyValue,"IPC%d",i+1);
		iRet = ReadParam(g_acConfigIniName, "[IPCINFO]", acKeyValue, acParseStr);
		if(iRet < 0 )
		{
			printf("IPCINFO Error,[%s] \n", g_acConfigIniName);
			return -1;
		}
		
		if(0 == ParseIpcInfo(acParseStr, &g_acIpcInfo[i]))
		{
			int iImgIdx = g_acIpcInfo[i].iImgIndex;    
			if(iImgIdx >0 && iImgIdx <33)
			{
				g_acImg2VideoIdx[iImgIdx-1] = i;	
			}			
		}
		else
		{
			return -1;
		}
	}
	
	return 0;
}

void InitConfigTool()
{
	pthread_t tid;
    int iRet = 0;

	iRet = ReadIpcImgIdx();
	if(iRet <0)
	{
		printf("InitConfigTool Parse ini file fail!\n");
	}
	iRet = pthread_create(&tid, NULL, StartMsgListenThread, NULL);
}

void UnInitConfigTool()
{
	
}
