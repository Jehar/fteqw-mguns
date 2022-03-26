#pragma once


#define MAX_BUFFSIZE	8192
#define MAX_STRING		256

extern char steam_UserName[MAX_STRING];


enum cl_packets
{
	CL_HANDSHAKE,
	CL_STARTSERVER,
	CL_CONNECTSERVER,
	CL_DISCONNECTSERVER,
	CL_AUTH_FETCH,
	CL_AUTH_VALIDATE,
	CL_MAX,
};


enum sv_packets
{
	SV_SETNAME,
	SV_STEAMID,
	SV_AUTH_RETRIEVED,
	SV_AUTH_VALIDATED,
	SV_MAX,
};



float PIPE_ReadFloat(void);
signed long PIPE_ReadLong(void);
signed short PIPE_ReadShort(void);
unsigned char PIPE_ReadByte(void);
int PIPE_ReadString(char *buff);
int PIPE_WriteFloat(float dat_float);
int PIPE_WriteLong(signed long dat);
int PIPE_WriteShort(signed short dat);
int PIPE_WriteByte(unsigned char dat);
int PIPE_WriteString(char* str);














