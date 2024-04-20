#pragma once


#define MAX_BUFFSIZE	8192
#define MAX_STRING		256

#define MAX_LOADOUT		16

extern char steam_UserName[MAX_STRING];


enum cl_packets
{
	CL_HANDSHAKE, // 0
	CL_STARTSERVER, // 1
	CL_CONNECTSERVER, // 2
	CL_DISCONNECTSERVER, // 3
	CL_AUTH_FETCH, // 4
	CL_AUTH_VALIDATE, // 5
	CL_PLAYINGWITH, // 6
	CL_INV_SENDITEMS, // 7
	CL_INV_GETPROPERTY, // 8
	CL_INV_GETINSTANCEPROPERTY, // 9
	CL_INV_UPDATECLIENT, // 10
	CL_INV_BUILDSERIAL, // 11
	CL_INV_CLIENTLOADOUT, // 12
	CL_INV_GRANTITEM, // 13
	CL_INV_TIMEDROP, // 14
	CL_RICHPRESCENSE, // 15
	CL_MAX,
};


enum sv_packets
{
	SV_PRINT, // 0
	SV_SETNAME, // 1
	SV_STEAMID, // 2
	SV_AUTH_RETRIEVED, // 3
	SV_AUTH_VALIDATED, // 4
	SV_AVATAR_FETCHED, // 5
	SV_INV_ITEMLIST, // 6
	SV_INV_PROPERTY, // 7
	SV_INV_INSTANCEPROPERTY, // 8
	SV_INV_LOCALSERIAL, // 9
	SV_INV_CLIENTLOADOUT, // 10
	SV_INV_SERIALPASSED, // 11
	SV_INV_NEWITEM, // 12
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
	svcsteam_playtimedrop,
};

enum richpresencetypes
{
	RP_STATE,
	RP_SCORE,
	RP_SERVER,
};

enum richpresencestates
{
	RPSTATE_MENU,
	RPSTATE_MULTIPLAYER,
	RPSTATE_MULTIPLAYER_WARMUP,
	RPSTATE_MULTIPLAYER_RANKED,
	RPSTATE_MULTIPLAYER_SPECTATE,
	RPSTATE_SINGLEPLAYER,
	RPSTATE_COOP,
};

float PIPE_ReadFloat(void);
uint32_t PIPE_ReadLong(void);
uint64_t PIPE_ReadLongLong(void);
int16_t PIPE_ReadShort(void);
uint8_t PIPE_ReadByte(void);
int PIPE_ReadString(char *buff);
void PIPE_ReadCharArray(char *into, uint32_t *size);
int PIPE_WriteFloat(float dat_float);
int PIPE_WriteLong(uint32_t dat);
int PIPE_WriteLongLong(uint64_t dat);
int PIPE_WriteShort(int16_t dat);
int PIPE_WriteByte(uint8_t dat);
int PIPE_WriteString(const char *str, size_t maxsz);
int PIPE_WriteCharArray(const char *dat, uint32_t size);














