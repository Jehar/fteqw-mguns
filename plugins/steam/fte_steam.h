#pragma once


#define MAX_BUFFSIZE	8192
#define MAX_STRING		256

#define MAX_LOADOUT		16

extern char steam_UserName[MAX_STRING];


enum cl_packets
{
	CL_HANDSHAKE,
	CL_STARTSERVER,
	CL_CONNECTSERVER,
	CL_DISCONNECTSERVER,
	CL_AUTH_FETCH,
	CL_AUTH_VALIDATE,
	CL_PLAYINGWITH,
	CL_INV_SENDITEMS,
	CL_INV_GETPROPERTY,
	CL_INV_GETINSTANCEPROPERTY,
	CL_INV_UPDATECLIENT,
	CL_INV_BUILDSERIAL,
	CL_INV_CLIENTLOADOUT,
	CL_INV_GRANTITEM,
	CL_MAX,
};


enum sv_packets
{
	SV_PRINT,
	SV_SETNAME,
	SV_STEAMID,
	SV_AUTH_RETRIEVED,
	SV_AUTH_VALIDATED,
	SV_AVATAR_FETCHED,
	SV_INV_ITEMLIST,
	SV_INV_PROPERTY,
	SV_INV_INSTANCEPROPERTY,
	SV_INV_LOCALSERIAL,
	SV_INV_CLIENTLOADOUT,
	SV_INV_SERIALPASSED,
	SV_MAX,
};


enum clcommands
{
	clcsteam_fragpacket,
	clcsteam_steamid,
	clcsteam_auth,
	clcsteam_loadoutserial,
};


enum svcommands
{
	svcsteam_fragpacket,
	svcsteam_requestinfo,
	svcsteam_requestloadout,
};



float PIPE_ReadFloat(void);
signed long PIPE_ReadLong(void);
long long PIPE_ReadLongLong(void);
signed short PIPE_ReadShort(void);
unsigned char PIPE_ReadByte(void);
int PIPE_ReadString(char *buff);
void PIPE_ReadCharArray(char *into, unsigned long *size);
int PIPE_WriteFloat(float dat_float);
int PIPE_WriteLong(signed long dat);
int PIPE_WriteLongLong(long long dat);
int PIPE_WriteShort(signed short dat);
int PIPE_WriteByte(unsigned char dat);
int PIPE_WriteString(char* str);
int PIPE_WriteCharArray(char *dat, unsigned long size);














