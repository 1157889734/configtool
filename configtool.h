#ifndef _CONFIG_TOOL_H_
#define _CONFIG_TOOL_H_

#include "types.h"

#define MSG_MAGIC_FLAG      0xFF
#define MSG_TCP_PORT        11021

enum E_CLI2SERV_MSG
{
    MSG_CLI2SERV_HEART = 0x00,
    MSG_CLI2SERV_SET_IPC_NUM = 0x01,
    MSG_CLI2SERV_SET_PECU_CONN = 0x02,
    MSG_CLI2SERV_SET_FIRE_CONN = 0x03,
    MSG_CLI2SERV_SET_DOOR_CONN = 0x04,
    MSG_CLI2SERV_KILL_CCTV  = 0x05,
    MSG_CLI2SERV_GET_IPC_NUM = 0x06,
    MSG_CLI2SERV_GET_PECU_CONN = 0x07,
    MSG_CLI2SERV_GET_FIRE_CONN = 0x08,
    MSG_CLI2SERV_GET_DOOR_CONN = 0x09,
    MSG_CLI2SERV_GET_ONLINE_CAMERA = 0x0A,
    MSG_CLI2SERV_SET_CAMERA_IP   = 0x0B,
    MSG_CLI2SERV_GET_CAMERA_RTSP = 0x0C,
    MSG_CLI2SERV_SET_CAMEAR_PASS = 0x0D,
    MSG_CLI2SERV_GET_DISP_TYPE = 0x0E,
    MSG_CLI2SERV_SET_DISP_TYPE = 0x0F,
    MSG_CLI2SERV_BEGIN_LOAD_CFG= 0x10,
    MSG_CLI2SERV_LOAD_CFG      = 0x11,
    MSG_CLI2SERV_END_LOAD_CFG  = 0x12,
    MSG_CLI2SERV_SET_CYCTIME   = 0x13,
};

enum E_SERV2CLI_MSG
{
    MSG_SERV2CLI_SET_IPC_NUM_RESP    = 0xA1,
    MSG_SERV2CLI_SET_PECU_CONN_RESP  = 0xA2,
    MSG_SERV2CLI_SET_FIRE_CONN_RESP  = 0xA3,
    MSG_SERV2CLI_SET_DOOR_CONN_RESP  = 0xA4,
    MSG_SERV2CLI_GET_IPC_NUM_RESP    = 0xA6,
    MSG_SERV2CLI_GET_PECU_CONN_RESP  = 0xA7,
    MSG_SERV2CLI_GET_FIRE_CONN_RESP  = 0xA8,
    MSG_SERV2CLI_GET_DOOR_CONN_RESP  = 0xA9,
    MSG_SERV2CLI_GET_ONLINE_CAMERA_RESP = 0xAA,
    MSG_SERV2CLI_SET_CAMERA_IP_RESP	 = 0xAB,
    MSG_SERV2CLI_GET_CAMERA_RTSP_RESP = 0xAC,
    MSG_SERV2CLI_SET_CAMEAR_PASS_RESP = 0xAD,
    MSG_SERV2CLI_GET_DISP_TYPE_RESP = 0xAE,
    MSG_SERV2CLI_SET_DISP_TYPE_RESP = 0xAF,
    MSG_SERV2CLI_BEGIN_LOAD_CFG_RESP= 0xB0,
    MSG_SERV2CLI_LOAD_CFG_RESP      = 0xB1,
    MSG_SERV2CLI_END_LOAD_CFG_RESP  = 0xB2,
    MSG_SERV2CLI_SET_CYCTIME_RESP   = 0xB3
};

	
typedef struct _T_MSG_HEAD
{
    BYTE magic;
    BYTE cmd;
    INT16 sLen;
}__attribute__((packed)) T_MSG_HEAD, *PT_MSG_HEAD;

typedef struct _T_MSG_INFO
{
    T_MSG_HEAD tMsgHead;
    char acMsgData[10240];
}__attribute__((packed)) T_MSG_INFO, *PT_MSG_INFO;

typedef struct _T_PECU_CONN_INFO
{
	char acImgIdx[24];  //取值1-32
}__attribute__((packed)) T_PECU_CONN_INFO, *PT_PECU_CONN_INFO;

typedef struct _T_FIRE_CONN_INFO
{
	char acImgIdx[6];  //取值1-32
}__attribute__((packed)) T_FIRE_CONN_INFO, *PT_FIRE_CONN_INFO;

typedef struct _T_DOOR_CONN_INFO
{
	char acImgIdx[48];  //取值1-32
}__attribute__((packed)) T_DOOR_CONN_INFO, *PT_DOOR_CONN_INFO;


typedef struct _T_IPC_NUM_RESP_INFO
{
	char cFlag;  //1:成功   2:失败
	char cIpcNum;
}__attribute__((packed)) T_IPC_NUM_RESP_INFO, *PT_IPC_NUM_RESP_INFO;

typedef struct _T_DISP_TYPE_RESP_INFO
{
	char cFlag;  //1:成功   2:失败
	char cDispType;
}__attribute__((packed)) T_DISP_TYPE_RESP_INFO, *PT_DISP_TYPE_RESP_INFO;


typedef struct _T_PECU_RESP_INFO
{
	char cFlag;  //1:成功   2:失败
	T_PECU_CONN_INFO tPecuInfo;
}__attribute__((packed)) T_PECU_RESP_INFO, *PT_PECU_RESP_INFO;


typedef struct _T_FIRE_RESP_INFO
{
	char cFlag;  //1:成功   2:失败
	T_FIRE_CONN_INFO tFireInfo;
}__attribute__((packed)) T_FIRE_RESP_INFO, *PT_FIRE_RESP_INFO;

typedef struct _T_DOOR_RESP_INFO
{
	char cFlag;  //1:成功   2:失败
	T_DOOR_CONN_INFO tDoorInfo;
}__attribute__((packed)) T_DOOR_RESP_INFO, *PT_DOOR_RESP_INFO;


typedef struct _T_CAMERA_IP_INFO
{
    //根据图片序号来定义  这里面的所有序号都是按推案序号来的  因为图片的序号是固定对应相机名称的
    char acCameraIpInfo[32][16];
}__attribute__((packed)) T_CAMERA_IP_INFO, *PT_CAMERA_IP_INFO;

typedef struct _T_CAMERA_RTSP_INFO
{
    //根据图片序号来定义  这里面的所有序号都是按推案序号来的  因为图片的序号是固定对应相机名称的
    char acCameraRtspInfo[32][128];
}__attribute__((packed)) T_CAMERA_RTSP_INFO, *PT_CAMERA_RTSP_INFO;

typedef struct _T_CONFIG_PARAM
{
    char cCamNum;
    char acPecuInfo[24];
    char acDoorInfo[48];
    char acFireInfo[6];
    char cResv;
}__attribute__((packed)) T_CONFIG_PARAM;

typedef struct _T_MODIFY_USER_PARAM
{
    char acNewUser[16];
    char acNewPswd[16];
}__attribute__((packed)) T_MODIFY_USER_PARAM;

typedef struct _T_BEGIN_LOAD_CFG
{
    char acFileName[52];
    int iFileSize;
}__attribute__((packed))T_BEGIN_LOAD_CFG;

typedef struct _T_LOAD_CFG
{
    char cIndex;
    unsigned short sPktLen;
    char acData[1001];
}__attribute__((packed))T_LOAD_CFG;


void InitConfigTool();
void UnInitConfigTool();

#endif
