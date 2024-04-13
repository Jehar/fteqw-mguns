
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
typedef HANDLE PipeType;
#define NULLPIPE NULL
typedef unsigned __int8 uint8;
typedef __int32 int32;
typedef unsigned __int64 uint64;
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
typedef uint8_t uint8;
typedef int32_t int32;
typedef uint64_t uint64;
typedef int PipeType;
#define NULLPIPE -1
#endif

#include "quakedef.h"
#include "../plugin.h"

#include "pr_common.h"

#include "steam.h"
#include "fte_steam.h"


#ifdef _WIN32

static int pipeReady(PipeType fd)
{
	DWORD avail = 0;
	return (PeekNamedPipe(fd, NULL, 0, NULL, &avail, NULL) && (avail > 0));
} /* pipeReady */

static int writePipe(PipeType fd, const void *buf, const unsigned int _len)
{
	const DWORD len = (DWORD)_len;
	DWORD bw = 0;
	return ((WriteFile(fd, buf, len, &bw, NULL) != 0) && (bw == len));
} /* writePipe */

static int readPipe(PipeType fd, void *buf, const unsigned int _len)
{
	DWORD avail = 0;
	PeekNamedPipe(fd, NULL, 0, NULL, &avail, NULL);
	if (avail < _len)
		return 0;

	const DWORD len = (DWORD)_len;
	DWORD br = 0;
	return ReadFile(fd, buf, len, &br, NULL) ? (int)br : 0;
} /* readPipe */

static void closePipe(PipeType fd)
{
	CloseHandle(fd);
} /* closePipe */

static char *getEnvVar(const char *key, char *buf, const size_t _buflen)
{
	const DWORD buflen = (DWORD)_buflen;
	const DWORD rc = GetEnvironmentVariableA(key, buf, buflen);
	/* rc doesn't count null char, hence "<". */
	return ((rc > 0) && (rc > buflen)) ? NULL : buf;
} /* getEnvVar */

#else

static int pipeReady(PipeType fd)
{
	int rc;
	struct pollfd pfd = { fd, POLLIN | POLLERR | POLLHUP, 0 };
	while (((rc = poll(&pfd, 1, 0)) == -1) && (errno == EINTR)) { /*spin*/ }
	return (rc == 1);
} /* pipeReady */

static int writePipe(PipeType fd, const void *buf, const unsigned int _len)
{
	const ssize_t len = (ssize_t)_len;
	ssize_t bw;
	while (((bw = write(fd, buf, len)) == -1) && (errno == EINTR)) { /*spin*/ }
	return (bw == len);
} /* writePipe */

static int readPipe(PipeType fd, void *buf, const unsigned int _len)
{
	if (!pipeReady(fd))
		return 0;

	return read(fd, buf, (ssize_t)_len);
	//const ssize_t len = (ssize_t)_len;
	//ssize_t br;
	//while (((br = read(fd, buf, len)) == -1) && (errno == EINTR)) { /*spin*/ }
	//return (int)br == -1 ? (int)br : 0;
} /* readPipe */

static void closePipe(PipeType fd)
{
	close(fd);
} /* closePipe */

static char *getEnvVar(const char *key, char *buf, const size_t buflen)
{
	const char *envr = getenv(key);
	if (!envr || (strlen(envr) >= buflen))
		return NULL;
	strcpy(buf, envr);
	return buf;
} /* getEnvVar */

#endif

plugclientfuncs_t *clientfuncs;
static PipeType GPipeRead = NULLPIPE;
static PipeType GPipeWrite = NULLPIPE;


static int initPipes(void)
{
	char buf[64];
	uint64_t val;

	if (!getEnvVar("STEAMSHIM_READHANDLE", buf, sizeof(buf)))
	{
		Con_Printf("Could not read STEAMSHIM_READHANDLE\n");
		return 0;
	}
	else if (sscanf(buf, "%llu", &val) != 1)
		return 0;
	else
		GPipeRead = (PipeType)val;

	if (!getEnvVar("STEAMSHIM_WRITEHANDLE", buf, sizeof(buf)))
	{
		Con_Printf("Could not read STEAMSHIM_WRITEHANDLE\n");
		return 0;
	}
	else if (sscanf(buf, "%llu", &val) != 1)
		return 0;
	else
		GPipeWrite = (PipeType)val;

	Con_Printf("steam plug: pipes initialized\n");

	return ((GPipeRead != NULLPIPE) && (GPipeWrite != NULLPIPE));
} /* initPipes */



plugcorefuncs_t		*corefuncs;
//plugclientfuncs_t	*clientfuncs;
plugfsfuncs_t		*fsfuncs;
plugmsgfuncs_t		*msgfuncs;
plugworldfuncs_t	*worldfuncs;
typedef unsigned char byte;

#define PLUG_HEADER	"steam"

char SteamID[MAX_STRING];
void(*func_readarray[SV_MAX])();
void Network_ReadSVC();
void Network_ReadCLC(client_t *client);


typedef struct
{
	byte	data[MAX_BUFFSIZE];
	int		cursize;
} pipebuff_t;

pipebuff_t pipeSendBuffer;


int PIPE_SendData()
{
	uint32_t bytes_written;
	int succ = writePipe(GPipeWrite, pipeSendBuffer.data, pipeSendBuffer.cursize);

	if (succ)
		pipeSendBuffer.cursize = 0;

	return succ;
}


float PIPE_ReadFloat()
{
	float dat;
	int succ = readPipe(GPipeRead, &dat, 4);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


uint32_t PIPE_ReadLong()
{
	uint32_t dat;
	int succ = readPipe(GPipeRead, &dat, 4);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


uint64_t PIPE_ReadLongLong()
{
	uint64_t dat;
	int succ = readPipe(GPipeRead, &dat, 8);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


int16_t PIPE_ReadShort()
{
	int16_t dat;
	int succ = readPipe(GPipeRead, &dat, 2);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


byte PIPE_ReadByte()
{
	byte dat;
	int succ = readPipe(GPipeRead, &dat, 1);
	if (!succ)
	{
		return -1;
	}
	
	return dat;
}


int PIPE_ReadString(char *buff)
{
	uint32_t amount_written;

	int i;
	for (i = 0; i < MAX_STRING; i++)
	{
		readPipe(GPipeRead, buff + i, 1);
		if (buff[i] == 0)
			break;
	}
	amount_written = i;

	return amount_written;
}


void PIPE_ReadCharArray(char *into, uint32_t *size)
{
	*size = (unsigned long)PIPE_ReadShort();
	int succ = readPipe(GPipeRead, into, *size);
	if (!succ)
	{
		*size = 0;
	}
}





int PIPE_WriteFloat(float dat_float)
{
	int32_t dat;
	memcpy(&dat, &dat_float, sizeof(int32_t));

	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;
	pipeSendBuffer.data[seek + 2] = (dat >> 16) & 0xFF;
	pipeSendBuffer.data[seek + 3] = (dat >> 24) & 0xFF;

	pipeSendBuffer.cursize += 4;

	return true;
}


int PIPE_WriteLong(uint32_t dat)
{
	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;
	pipeSendBuffer.data[seek + 2] = (dat >> 16) & 0xFF;
	pipeSendBuffer.data[seek + 3] = (dat >> 24) & 0xFF;

	pipeSendBuffer.cursize += 4;

	return true;
}


int PIPE_WriteLongLong(uint64_t dat)
{
	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;
	pipeSendBuffer.data[seek + 2] = (dat >> 16) & 0xFF;
	pipeSendBuffer.data[seek + 3] = (dat >> 24) & 0xFF;
	pipeSendBuffer.data[seek + 4] = (dat >> 32) & 0xFF;
	pipeSendBuffer.data[seek + 5] = (dat >> 40) & 0xFF;
	pipeSendBuffer.data[seek + 6] = (dat >> 48) & 0xFF;
	pipeSendBuffer.data[seek + 7] = (dat >> 56) & 0xFF;

	pipeSendBuffer.cursize += 8;

	return true;
}


int PIPE_WriteShort(int16_t dat)
{
	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;

	pipeSendBuffer.cursize += 2;

	return true;
}


int PIPE_WriteByte(uint8_t dat)
{
	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat;
	pipeSendBuffer.cursize += 1;

	return true;
}


int PIPE_WriteString(char* str)
{
	int str_length = strlen(str);
	memcpy(&(pipeSendBuffer.data[pipeSendBuffer.cursize]), str, str_length);

	if (str[str_length - 1] != NULL)
		pipeSendBuffer.data[pipeSendBuffer.cursize + str_length] = NULL; str_length++;

	pipeSendBuffer.cursize += str_length;

	return true;
}


int PIPE_WriteCharArray(char *dat, uint32_t size)
{
	PIPE_WriteShort((int16_t)size);

	int seek = pipeSendBuffer.cursize;
	memcpy(&(pipeSendBuffer.data[seek]), dat, size);
	pipeSendBuffer.cursize += size;

	return true;
}

#define FP_MAXCHUNK		160
#define FP_ID_MASK		0x00FF
#define FP_ID_CLC		0x0100
typedef struct fragpacket_s
{
	uint32_t		id;				// which uid is this fragmented packet
	byte			totalpieces;	// how many pieces do we need to fully read it
	byte			transmitted;	// how many have we recieved?
	void*			chunks[0xFF];
	uint32_t		chunks_size[0xFF];
	struct fragpacket_s	*next;
} fragpacket_t;

struct netprim_s msg_nullnetprim;
fragpacket_t *fragpacketlist;
byte sv_fragid[MAX_CLIENTS];
byte cl_fragid;

fragpacket_t* FragPacket_Find(uint32_t id)
{
	fragpacket_t *list, *newfp;
	for (list = fragpacketlist; list; list = list->next)
	{
		if (list->id == id)
			return list;
	}

	newfp = corefuncs->Malloc(sizeof(fragpacket_t));
	newfp->id = id;
	newfp->next = fragpacketlist;
	fragpacketlist = newfp;
	return newfp;
}


void FragPacket_Finalize(fragpacket_t *packet)
{
	if (!packet->totalpieces || packet->transmitted < packet->totalpieces)
	{
		//Con_Printf("frag packet #%i: %i of %i\n", packet->id & FP_ID_MASK, packet->transmitted, packet->totalpieces);
		return;
	}

#if 1
	if (packet->id & FP_ID_CLC)
		Con_Printf("finalizing clc frag packet user%i %i\n", packet->id >> 16, packet->id & 255);
	else
		Con_Printf("finalizing svc frag packet %i\n", packet->id & 255);
#endif

	byte *data = corefuncs->Malloc(packet->totalpieces * FP_MAXCHUNK);
	sizebuf_t msgbuf;
	memset(&msgbuf, 0, sizeof(sizebuf_t));
	msgbuf.maxsize = (packet->totalpieces * FP_MAXCHUNK);
	msgbuf.allowoverflow = true;
	msgbuf.data = data;

	for (int i = 1; i < packet->totalpieces; i++) // transfer chunks into our new message
	{
		if (!packet->chunks)
			continue;

		Con_Printf("adding chunk %i to pack\n", i);

		msgfuncs->WriteData(&msgbuf, packet->chunks[i], packet->chunks_size[i]);
		corefuncs->Free(packet->chunks[i]);
	}

#if 0
	Con_Printf("final msg #%i: ", packet->id);
	for (int j = 0; j < msgbuf.cursize; j++)
		Con_Printf(" %i", msgbuf.data[j]);
	Con_Printf("\n");
#endif

	msgbuf.currentbit = 0;
	///*
	msgfuncs->BeginReading(&msgbuf, msg_nullnetprim);
	if (packet->id & FP_ID_CLC)
	{
		client_t *client;
		if (worldfuncs->GetClient)
		{	
			client = worldfuncs->GetClient(packet->id >> 16);
			Con_Printf("frag packet client (%s): %i vs %i\n", client->name, worldfuncs->GetSlot(client), packet->id >> 16);
			Network_ReadCLC(client);
		}
		else
		{
			Con_Printf("no worldfuncs\n");
		}
		//Network_ReadCLC(0);
	}
	else
	{
		Network_ReadSVC();
	}
	//*/
	
	if (fragpacketlist == packet)
	{
		fragpacketlist = packet->next;
	}
	else
	{
		fragpacket_t *lst;
		for (lst = fragpacketlist; lst->next != packet; lst = lst->next)
			;
		lst->next = packet->next;
		
	}

	corefuncs->Free(packet);
	corefuncs->Free(data);
}


void FragPacket_ReadChunk(fragpacket_t *packet, byte chunk, void* msg, int length)
{ 
	if (packet->chunks[chunk] != NULL) // we already have this one... weird
		return;
	packet->transmitted++; // we got another one, chief
	
	// copy the memory over to dynamically allocated slot
	packet->chunks[chunk] = corefuncs->Malloc(length);
	packet->chunks_size[chunk] = length;
	memcpy(packet->chunks[chunk], msg, length);
}


void MSG_SendToServer(qboolean reliable, sizebuf_t *msgbuf)
{
	if (msgbuf->cursize > FP_MAXCHUNK) // we need to split this up
	{
		reliable = true; // since it's split it has to be reliable, sorry
		int chunkstosplit = ceil(msgbuf->cursize / (float)FP_MAXCHUNK);

		// meta info about fragmented packet
		byte data[2048];
		sizebuf_t header;
		memset(&header, 0, sizeof(sizebuf_t));
		header.allowoverflow = false;
		header.maxsize = sizeof(data);
		header.data = data;

		msgfuncs->WriteByte(&header, clcfte_pluginpacket);
		msgfuncs->WriteShort(&header, 4);
		msgfuncs->WriteString(&header, PLUG_HEADER);
		msgfuncs->WriteByte(&header, clcsteam_fragpacket);
		msgfuncs->WriteByte(&header, cl_fragid);
		msgfuncs->WriteByte(&header, 0);
		msgfuncs->WriteByte(&header, chunkstosplit + 1);
		//sCon_Printf("sending header %i\n", header.cursize);
		msgfuncs->CL_SendMessage(reliable, header.data, header.cursize);
		//

#if 1
		int size;
		sizebuf_t message;
		int seek = 0;

		for (int i = 1; i <= chunkstosplit; i++, msgbuf->cursize -= FP_MAXCHUNK)
		{
			byte tdata[2048];
			memset(&message, 0, sizeof(sizebuf_t));
			message.cursize = 0;
			message.allowoverflow = false;
			message.maxsize = sizeof(data);
			message.data = data;

			size = min(msgbuf->cursize, FP_MAXCHUNK);
			//memcpy(tdata, msgbuf->data + seek, size);
			//seek += size;

			#if 0
			Con_Printf("sending chunk #%i ", i);
			for (int j = 0; j < size; j++)
				Con_Printf(" %i", data[j]);
			Con_Printf("\n");
			#endif


			msgfuncs->WriteByte(&message, clcfte_pluginpacket);
#if FP_MAXCHUNK > 0xFF // this is probably never gonna be true, but just in case...
			msgfuncs->WriteShort(&message, size + 5);
#else
			msgfuncs->WriteShort(&message, size + 4);
#endif
			msgfuncs->WriteString(&message, PLUG_HEADER);
			msgfuncs->WriteByte(&message, clcsteam_fragpacket);
			msgfuncs->WriteByte(&message, cl_fragid);
			msgfuncs->WriteByte(&message, i);
#if FP_MAXCHUNK > 0xFF
			msgfuncs->WriteShort(&message, size);
#else
			msgfuncs->WriteByte(&message, size);
#endif
			msgfuncs->WriteData(&message, msgbuf->data + seek, size);
			seek += size;

#if 0
			Con_Printf("sending chunk msg #%i ", i);
			for (int j = 0; j < message.cursize; j++)
				Con_Printf(" %i", message.data[j]);
			Con_Printf("\n");
#endif
			//for (int j = 0; j < 0xFFFFF; j++);

			msgfuncs->CL_SendMessage(reliable, message.data, message.cursize);
			//Con_Printf("sending chunk %i vs %i\n", message.cursize - (strlen(PLUG_HEADER) + 3), size + 4);
		}
#endif


		cl_fragid = (cl_fragid + 1) & 255;
		return;
	}

	byte data[512];
	sizebuf_t message;
	memset(&message, 0, sizeof(sizebuf_t));
	message.allowoverflow = true;
	message.maxsize = sizeof(data);
	message.data = data;

	msgfuncs->WriteByte(&message, clcfte_pluginpacket);
	msgfuncs->WriteShort(&message, msgbuf->cursize);
	msgfuncs->WriteString(&message, PLUG_HEADER);
	msgfuncs->WriteData(&message, msgbuf->data, msgbuf->cursize);

	msgfuncs->CL_SendMessage(reliable, message.data, message.cursize);
}


void MSG_SendToClient(client_t *client, qboolean reliable, sizebuf_t *msgbuf)
{
	if (msgbuf->cursize > FP_MAXCHUNK) // we need to split this up
	{
		reliable = true; // since it's split it has to be reliable, sorry
		int chunkstosplit = ceil(msgbuf->cursize / (float)FP_MAXCHUNK);

		// meta info about fragmented packet
		sizebuf_t *header;

		header = msgfuncs->SV_StartReliable(client, 10);
		msgfuncs->WriteByte(header, svcfte_pluginpacket);
		msgfuncs->WriteShort(header, 4);
		msgfuncs->WriteString(header, PLUG_HEADER);
		msgfuncs->WriteByte(header, svcsteam_fragpacket);
		msgfuncs->WriteByte(header, sv_fragid[worldfuncs->GetSlot(client)]);
		msgfuncs->WriteByte(header, 0);
		msgfuncs->WriteByte(header, chunkstosplit + 1);
		//sCon_Printf("sending header %i\n", header.cursize);
		msgfuncs->SV_FinishReliable(client);
		//

#if 1
		int size;
		sizebuf_t *message;
		int seek = 0;

		for (int i = 1; i <= chunkstosplit; i++, msgbuf->cursize -= FP_MAXCHUNK)
		{
			//byte tdata[2048];
			//memset(&message, 0, sizeof(sizebuf_t));
			//message.cursize = 0;
			//message.allowoverflow = true;
			//message.maxsize = sizeof(data);
			//message.data = data;

			size = min(msgbuf->cursize, FP_MAXCHUNK);
			//memcpy(tdata, msgbuf->data + seek, size);
			//seek += size;
			message = msgfuncs->SV_StartReliable(client, size + 10);

#if 0
			Con_Printf("sending chunk #%i ", i);
			for (int j = 0; j < size; j++)
				Con_Printf(" %i", data[j]);
			Con_Printf("\n");
#endif


			msgfuncs->WriteByte(message, svcfte_pluginpacket);
#if FP_MAXCHUNK > 0xFF // this is probably never gonna be true, but just in case...
			msgfuncs->WriteShort(message, size + 5);
#else
			msgfuncs->WriteShort(message, size + 4);
#endif
			msgfuncs->WriteString(message, PLUG_HEADER);
			msgfuncs->WriteByte(message, svcsteam_fragpacket);
			msgfuncs->WriteByte(message, sv_fragid[worldfuncs->GetSlot(client)]);
			msgfuncs->WriteByte(message, i);
#if FP_MAXCHUNK > 0xFF
			msgfuncs->WriteShort(message, size);
#else
			msgfuncs->WriteByte(message, size);
#endif
			msgfuncs->WriteData(message, msgbuf->data + seek, size);
			seek += size;

#if 0
			Con_Printf("sending chunk msg #%i ", i);
			for (int j = 0; j < size + 4; j++)
				Con_Printf(" %i", message.data[j]);
			Con_Printf("\n");
#endif

			msgfuncs->SV_FinishReliable(client);
			//msgfuncs->CL_SendMessage(reliable, message.data, message.cursize);
			//Con_Printf("sending chunk %i vs %i\n", message.cursize - (strlen(PLUG_HEADER) + 3), size + 4);
		}
#endif


		sv_fragid[worldfuncs->GetSlot(client)] = (sv_fragid[worldfuncs->GetSlot(client)] + 1) & 255;
		return;
	}

	sizebuf_t *message;
	message = msgfuncs->SV_StartReliable(client, msgbuf->cursize + 10);

	msgfuncs->WriteByte(message, svcfte_pluginpacket);
	msgfuncs->WriteShort(message, msgbuf->cursize);
	msgfuncs->WriteString(message, PLUG_HEADER);
	msgfuncs->WriteData(message, msgbuf->data, msgbuf->cursize);

	msgfuncs->SV_FinishReliable(client);
}





void Steam_SetName(void)
{
	char currentname[MAX_STRING];
	char name[MAX_STRING];
	PIPE_ReadString(name);
	Con_Printf("Steam: Set Name\n");

	cvarfuncs->SetString("steam_name", name);
	cvarfuncs->GetString("name", currentname, sizeof(currentname));


	if (!stricmp("player", currentname))
	{
		cvarfuncs->SetString("name", name);
		Con_Printf("New Name: %s\n", name);
	}
}


void Steam_SetSteamID(void)
{
	PIPE_ReadString(SteamID);
	Con_Printf("Set SteamID: %s\n", SteamID);

	cvar_t cv_steamid;
	memcpy(&cv_steamid.string, SteamID, sizeof(cv_steamid.string));
	cv_steamid.name = "steam_id";
	cv_steamid.flags = 1u << 5;

	const char *id = SteamID;

	cvarfuncs->SetString("steam_id", id);

	//cvarfuncs->SetString(cv_steamid.name, cv_steamid.string);
	//Cvar_Get(cv_steamid.name, cv_steamid.string, cv_steamid.flags, "steam");
	//Cvar_Register(&cv_steamid, "steam");
}

extern qboolean VARGS Q_snprintfz(char *dest, size_t size, const char *fmt, ...);
void Steam_Auth_Retrieved(void)
{
	char token[1024];
	uint32_t sz;
	PIPE_ReadCharArray(token, &sz);

	Con_Printf("Auth ticket of %i size recieved\n", (int)sz);

	//cmdfuncs->AddText("fs_flush\nvid_reload\nsetinfoblob _steam_auth \"data/_STEAMTEMP/authtoken\"\n", false);
	if (clientfuncs)
	{
		byte data[2048];
		sizebuf_t message;
		memset(&message, 0, sizeof(sizebuf_t));
		message.allowoverflow = false;
		message.maxsize = sizeof(data);
		message.data = data;

		msgfuncs->WriteByte(&message, clcsteam_auth);
		msgfuncs->WriteString(&message, SteamID);
		msgfuncs->WriteShort(&message, sz);
		msgfuncs->WriteData(&message, &token, sz);
		MSG_SendToServer(true, &message);


		//clientfuncs->SetUserInfo(0, "_steam_id", SteamID);
		//clientfuncs->SetUserInfoBlob(0, "_steam_auth", dat, sz);
	}
}


void Steam_Auth_Validated(void)
{
	int entnum = PIPE_ReadByte();
	char dat[MAX_STRING];
	PIPE_ReadString(dat);


	char cmd[MAX_STRING];
	Q_snprintf(cmd, MAX_STRING, "sv_cmd steam_authenticationpassed %i %s\n", entnum, dat);
	cmdfuncs->AddText(cmd, false);


	Con_DPrintf("Auth ticket of %s authenticated\n", dat);
}


void Steam_Avatar_Fetched(void)
{
	char steamid[MAX_STRING];
	PIPE_ReadString(steamid);
	int size = PIPE_ReadByte();

	char cmd[MAX_STRING];
	Q_snprintf(cmd, MAX_STRING, "fs_flush\ncl_cmd steam_avatarfetched %s %i\n", steamid, size);

	Con_Printf("Avatar fetched %s %i\n", steamid, size);

	cmdfuncs->AddText(cmd, false);
}


void Steam_Print(void)
{
	char msg[MAX_STRING];
	PIPE_ReadString(msg);
	Con_Printf(msg);
}


typedef struct
{
	uint64_t itemInstance;
	uint32_t itemDefinition;
} inventoryItem_t;

unsigned short inventoryArraySize;
inventoryItem_t *inventoryArray;

void Steam_Inventory_ItemList(void)
{
	if (inventoryArray != NULL)
	{
		free(inventoryArray);
		inventoryArray = NULL;
	}

	inventoryArraySize = PIPE_ReadShort();
	Con_Printf("^xf0f~~Inventory (%i items) Fetched~~\n", (int)inventoryArraySize);
	if (inventoryArraySize <= 0)
		return;

	char cmd[MAX_STRING];
	//Q_snprintf(cmd, MAX_STRING, "steam_igame_itemlist clear\n");
	//cmdfuncs->AddText(cmd, false);
	Q_snprintf(cmd, MAX_STRING, "menu_cmd steam_imenu_itemlist clear\n");
	cmdfuncs->AddText(cmd, false);

	inventoryItem_t *item;
	inventoryArray = malloc(sizeof(inventoryItem_t) * inventoryArraySize);
	for (int i = 0; i < inventoryArraySize; i++)
	{
		item = &inventoryArray[i];
		item->itemInstance = PIPE_ReadLongLong();
		item->itemDefinition = PIPE_ReadLong();
		
		//Q_snprintf(cmd, MAX_STRING, "steam_igame_itemlist %lu %llu\n", item->itemDefinition, item->itemInstance);
		//cmdfuncs->AddText(cmd, false);
		Q_snprintf(cmd, MAX_STRING, "menu_cmd steam_imenu_itemlist %u %llu\n", (uint32_t)item->itemDefinition, (uint64_t)item->itemInstance);
		cmdfuncs->AddText(cmd, false);

		Con_Printf("^xff9Item #%i: %u %llu\n", i, (uint32_t)item->itemDefinition, (uint64_t)item->itemInstance);
	}
}

void Steam_Inventory_ItemProperty(void)
{
	unsigned int itemid;
	char propertyname[MAX_STRING];
	char propertyvalue[MAX_STRING];

	itemid = PIPE_ReadLong();
	PIPE_ReadString(propertyname);
	PIPE_ReadString(propertyvalue);

	//Con_Printf("^xf0f~Property \"%lu\" Fetched~\n", itemid);
	//Con_Printf("%s: %s\n", propertyname, propertyvalue);

	char cmd[MAX_STRING];
	Q_snprintf(cmd, MAX_STRING, "sv_cmd steam_igame_itemproperty %u %s \"%s\"\n", itemid, propertyname, propertyvalue);
	cmdfuncs->AddText(cmd, false);

	Q_snprintf(cmd, MAX_STRING, "cl_cmd steam_igame_itemproperty %u %s \"%s\"\n", itemid, propertyname, propertyvalue);
	cmdfuncs->AddText(cmd, false);

	Q_snprintf(cmd, MAX_STRING, "menu_cmd steam_imenu_itemproperty %u %s \"%s\"\n", itemid, propertyname, propertyvalue);
	cmdfuncs->AddText(cmd, false);
}

void Steam_Inventory_InstanceProperty(void)
{
	unsigned int itemid;
	char propertyname[MAX_STRING];
	char propertyvalue[MAX_STRING];

	itemid = PIPE_ReadLongLong();
	PIPE_ReadString(propertyname);
	PIPE_ReadString(propertyvalue);

	//Con_Printf("^xf0f~Instance Property \"%lu\" Fetched~\n", itemid);
	//Con_Printf("%s: %s\n", propertyname, propertyvalue);
}

uint8_t inv_clientSerial[4096];
uint32_t inv_clientSerialSize;
uint64_t inv_clientLoadout[MAX_LOADOUT];
byte inv_clientLoadoutSize;
void Inventory_FlushLoadout(void)
{
	memset(inv_clientLoadout, 0, sizeof(inv_clientLoadout));
	inv_clientLoadoutSize = 0;
}

void Inventory_AddToLoadout(uint64_t instanceid)
{
	int i = 0;
	do
	{
		if (!inv_clientLoadout[i])
		{
			inv_clientLoadout[i] = instanceid;
			inv_clientLoadoutSize = i+1;
			break;
		}
	} while (i++ < MAX_LOADOUT);
}

void Inventory_RequestSerial(void)
{
	PIPE_WriteByte(CL_INV_BUILDSERIAL);
	PIPE_WriteByte(inv_clientLoadoutSize);

	for (int i = 0; i < inv_clientLoadoutSize; i++)
	{
		PIPE_WriteLongLong(inv_clientLoadout[i]);
	}

	Con_Printf("sending CL_INV_BUILDSERIAL of %i items\n", inv_clientLoadoutSize);
}

void Steam_Inventory_LocalSerial(void)
{
	PIPE_ReadCharArray(inv_clientSerial, &inv_clientSerialSize);
	Con_Printf("serial recieved of size %lu\n", inv_clientSerialSize);

	byte *data = malloc(inv_clientSerialSize + 8);
	sizebuf_t message;
	memset(&message, 0, sizeof(sizebuf_t));
	message.allowoverflow = false;
	message.maxsize = inv_clientSerialSize + 8;
	message.data = data;

	msgfuncs->WriteByte(&message, clcsteam_loadoutserial);
	msgfuncs->WriteShort(&message, inv_clientSerialSize);
	msgfuncs->WriteData(&message, &inv_clientSerial, inv_clientSerialSize);
	MSG_SendToServer(true, &message);
	free(data);
}

void Steam_Invetory_SetClientLoadout(void)
{
	char cmd[MAX_STRING];
	int clientNum = PIPE_ReadByte();
	int arraySize = PIPE_ReadByte();

	Con_Printf("sending loadout to ssqc\n");

	Q_snprintf(cmd, MAX_STRING, "sv_cmd steam_igame_loadoutflush %i\n", clientNum);
	cmdfuncs->AddText(cmd, false);

	for (int i = 0; i < arraySize; i++)
	{
		uint32_t itm = PIPE_ReadLong();
		uint64_t inst = PIPE_ReadLongLong();
		Q_snprintf(cmd, MAX_STRING, "sv_cmd steam_igame_loadoutadd %i %lu %llu\n", clientNum, itm, inst);
		cmdfuncs->AddText(cmd, false);
	}
}

void Steam_Inventory_LoadoutUpdated()
{
	char cmd[MAX_STRING];
	int clientNum = PIPE_ReadByte();
	Q_snprintf(cmd, MAX_STRING, "sv_cmd steam_igame_loadoutupdated %u\n", clientNum);
	cmdfuncs->AddText(cmd, false);
}

void Steam_Inventory_NewItem()
{
	char cmd[MAX_STRING];
	uint32_t itemid;
	char itemName[MAX_STRING];

	itemid = PIPE_ReadLong();
	PIPE_ReadString(itemName);

	Con_Printf("Item %lu:%s dropped\n", itemid, itemName);
	Q_snprintf(cmd, MAX_STRING, "say [Recieved a new item: %s]\n", itemName);
	cmdfuncs->AddText(cmd, false);
}


void Steam_Init(void)
{
	func_readarray[SV_PRINT] = Steam_Print;
	func_readarray[SV_SETNAME] = Steam_SetName;
	func_readarray[SV_STEAMID] = Steam_SetSteamID;
	func_readarray[SV_AUTH_RETRIEVED] = Steam_Auth_Retrieved;
	func_readarray[SV_AUTH_VALIDATED] = Steam_Auth_Validated;
	func_readarray[SV_AVATAR_FETCHED] = Steam_Avatar_Fetched;

	func_readarray[SV_INV_ITEMLIST] = Steam_Inventory_ItemList;
	func_readarray[SV_INV_PROPERTY] = Steam_Inventory_ItemProperty;
	func_readarray[SV_INV_INSTANCEPROPERTY] = Steam_Inventory_InstanceProperty;
	func_readarray[SV_INV_LOCALSERIAL] = Steam_Inventory_LocalSerial;
	func_readarray[SV_INV_CLIENTLOADOUT] = Steam_Invetory_SetClientLoadout;
	func_readarray[SV_INV_SERIALPASSED] = Steam_Inventory_LoadoutUpdated;
	func_readarray[SV_INV_NEWITEM] = Steam_Inventory_NewItem;

	inventoryArraySize = 0;
	inventoryArray = NULL;
	Inventory_FlushLoadout();
}





int next_readcheck;
void Steam_Tick(int *args)
{
	// turns out this shit is expensive...
	// so we need to do it sporadically. hopefully this won't cause chugging!
	if (corefuncs->GetMilliseconds() > next_readcheck)
	{
		next_readcheck = corefuncs->GetMilliseconds() + 100;

		unsigned char index = PIPE_ReadByte();
		while (index != 255)
		{
			if (index < SV_MAX)
			{
				func_readarray[index]();
			}

			index = PIPE_ReadByte();
		}
	}
	

	// this we can just do as needed, shouldn't be too bad.
	if (pipeSendBuffer.cursize)
	{
		PIPE_SendData();
	}


	fragpacket_t *lst, *hold;
	for (lst = fragpacketlist; lst; hold = lst->next, FragPacket_Finalize(lst), lst = hold); // loop through and finalize any packets that can be

}


void Steam_ExecuteCommand()
{
	char cmd[256];
	cmdfuncs->Argv(0, cmd, sizeof(cmd));
	if (!strcmp(cmd, "steam_connect") && cmdfuncs->Argc() > 1)
	{
		char steamid[64];
		cmdfuncs->Argv(1, steamid, sizeof(steamid));


		PIPE_WriteByte(CL_CONNECTSERVER);
		PIPE_WriteByte(1);
		PIPE_WriteString(steamid);
		Con_Printf("sending CL_CONNECTSERVER\n");

		
		PIPE_WriteByte(CL_AUTH_FETCH);
		Con_Printf("sending CL_AUTH_FETCH\n");


		return;
	}
	else if (!strcmp(cmd, "steam_startserver"))
	{
		PIPE_WriteByte(CL_STARTSERVER);
		PIPE_WriteByte(1);
		Con_Printf("sending CL_STARTSERVER\n");

		return;
	}
	else if (!strcmp(cmd, "steam_disconnect"))
	{
		PIPE_WriteByte(CL_DISCONNECTSERVER);
		Con_Printf("sending CL_DISCONNECTSERVER\n");

		return;
	}
	else if (!strcmp(cmd, "steam_authenticate"))
	{
		char entnum_str[4];
		cmdfuncs->Argv(1, entnum_str, sizeof(entnum_str));
		int entnum = atoi(entnum_str);

		char steamid[64];
		cmdfuncs->Argv(2, steamid, sizeof(steamid));

		if (fsfuncs)
		{
			///*
			qhandle_t handle;
			char path[128] = "data/_STEAMTEMP/";
			strcat(path, steamid);
			strcat(path, "/authtoken");

			fsfuncs->Open(path, &handle, 1);
			if (handle >= 0)
			{
				char dat[1024];
				int length = fsfuncs->Read(handle, dat, 1024);

				PIPE_WriteByte(CL_AUTH_VALIDATE);
				PIPE_WriteByte(entnum);
				PIPE_WriteString(steamid);
				Con_Printf("sending CL_AUTH_VALIDATE\n");

				PIPE_WriteCharArray(dat, length);
				fsfuncs->Close(handle);

				Con_Printf("file read success\n");
			}
			else
			{
				Con_Printf("file read failure\n");
			}
			//*/
		}

		return;
	}
	else if (!strcmp(cmd, "steam_playingwith"))
	{
		char steamid[64];
		cmdfuncs->Argv(1, steamid, sizeof(steamid));

		PIPE_WriteByte(CL_PLAYINGWITH);
		PIPE_WriteString(steamid);
		Con_Printf("sending CL_PLAYINGWITH\n");


		return;
	}
	else if (!strcmp(cmd, "steam_playingwith"))
	{
		char steamid[64];
		cmdfuncs->Argv(1, steamid, sizeof(steamid));

		PIPE_WriteByte(CL_PLAYINGWITH);
		PIPE_WriteString(steamid);
		Con_Printf("sending CL_PLAYINGWITH\n");


		return;
	}
	else if (!strcmp(cmd, "steam_ifetchitems"))
	{
		PIPE_WriteByte(CL_INV_SENDITEMS);
		Con_Printf("sending CL_INV_SENDITEMS\n");
		return;
	}
	else if (!strcmp(cmd, "steam_ifetchproperty"))
	{
		char buf[64];
		PIPE_WriteByte(CL_INV_GETPROPERTY);

		cmdfuncs->Argv(1, buf, sizeof(buf));
		PIPE_WriteLong(atoi(buf));

		cmdfuncs->Argv(2, buf, sizeof(buf));
		PIPE_WriteString(buf);

		Con_Printf("sending CL_INV_GETPROPERTY\n");
		return;
	}
	else if (!strcmp(cmd, "steam_ifetchinstanceproperty"))
	{
		char buf[64];
		PIPE_WriteByte(CL_INV_GETINSTANCEPROPERTY);

		PIPE_WriteByte(255);

		cmdfuncs->Argv(1, buf, sizeof(buf));
		PIPE_WriteLongLong(atoll(buf));

		cmdfuncs->Argv(2, buf, sizeof(buf));
		PIPE_WriteString(buf);

		Con_Printf("sending CL_INV_GETINSTANCEPROPERTY\n");
		return;
	}
	else if (!strcmp(cmd, "steam_iloadout_flush"))
	{
		Inventory_FlushLoadout();
		return;
	}
	else if (!strcmp(cmd, "steam_iloadout_add"))
	{
		char buf[64];
		cmdfuncs->Argv(1, buf, sizeof(buf));
		Inventory_AddToLoadout(atoll(buf));
		return;
	}
	else if (!strcmp(cmd, "steam_iloadout_getserial"))
	{
		Inventory_RequestSerial();
		return;
	}
	else if (!strcmp(cmd, "steam_igame_getloadout"))
	{
		char buf[64];

		PIPE_WriteByte(CL_INV_CLIENTLOADOUT);
		cmdfuncs->Argv(1, buf, sizeof(buf));
		PIPE_WriteByte(atol(buf));

		Con_Printf("sending CL_INV_CLIENTLOADOUT\n");
		return;
	}
	else if (!strcmp(cmd, "steam_igrantitem"))
	{
		char buf[64];

		PIPE_WriteByte(CL_INV_GRANTITEM);
		cmdfuncs->Argv(1, buf, sizeof(buf));
		PIPE_WriteLong(atol(buf));

		Con_Printf("sending CL_INV_GRANTITEM\n");
		return;
	}
	else if (!strcmp(cmd, "steam_richsetstate"))
	{
		char buf[64];
		PIPE_WriteByte(CL_RICHPRESCENSE);
		PIPE_WriteByte(RP_STATE);

		cmdfuncs->Argv(1, buf, sizeof(buf));
		PIPE_WriteByte(atoi(buf));
	}
	else if (!strcmp(cmd, "steam_richsetscore"))
	{
		char buf[64];
		PIPE_WriteByte(CL_RICHPRESCENSE);
		PIPE_WriteByte(RP_SCORE);

		cmdfuncs->Argv(1, buf, sizeof(buf));
		PIPE_WriteShort(atoi(buf));

		cmdfuncs->Argv(2, buf, sizeof(buf));
		PIPE_WriteShort(atoi(buf));
	}
	else if (!strcmp(cmd, "steam_richsetserver"))
	{
		char buf[128];
		PIPE_WriteByte(CL_RICHPRESCENSE);
		PIPE_WriteByte(RP_SERVER);

		cmdfuncs->Argv(1, buf, sizeof(buf));
		PIPE_WriteString(buf);

		cmdfuncs->Argv(2, buf, sizeof(buf));
		PIPE_WriteByte(atoi(buf));

		cmdfuncs->Argv(3, buf, sizeof(buf));
		PIPE_WriteString(buf);

		cmdfuncs->Argv(4, buf, sizeof(buf));
		PIPE_WriteString(buf);
	}
}


void WriteHeader(sizebuf_t *msg, int svc, int *size_seek, int *msg_start)
{
	msgfuncs->WriteByte(msg, svc);
	*size_seek = msg->cursize;
	msgfuncs->WriteShort(msg, 0);
	msgfuncs->WriteString(msg, PLUG_HEADER);
	*msg_start = msg->cursize;
}


void Network_ReadCLC(client_t *client) // server reading message from client
{
	char dat[8096];
	int sz;
	char steamid[64];
	int msg = msgfuncs->ReadByte();
	Con_Printf("steam clc %i from %i (%s)\n", msg, worldfuncs->GetSlot(client) + 1, client->name);
	switch (msg)
	{
	case clcsteam_fragpacket:;
		fragpacket_t *packet;
		int	chunkindex;
		int id;
		
		id = msgfuncs->ReadByte() | FP_ID_CLC;
		id |= (uint32_t)(worldfuncs->GetSlot(client)) << 16u; // encode player slot into id
		chunkindex = msgfuncs->ReadByte();
		
		packet = FragPacket_Find(id);

		if (chunkindex == 0)
		{
			if (!packet->totalpieces)
			{
				packet->totalpieces = msgfuncs->ReadByte();
				packet->transmitted++;

				Con_Printf("clc fragpacket header, total chunks %i\n", packet->totalpieces);
			}
			else
			{
				Con_Printf("clc fragpacket header, duplicate %i\n", packet->id & FP_ID_MASK);
				msgfuncs->ReadByte(); // throw away, we already got this
			}
		}
		else
		{
			int sz;
			byte data[FP_MAXCHUNK];
#if FP_MAXCHUNK > 0xFF // this is probably never gonna be true, but just in case...
			sz = msgfuncs->ReadShort();
#else
			sz = msgfuncs->ReadByte();
#endif
			msgfuncs->ReadData(data, sz);
			FragPacket_ReadChunk(packet, chunkindex, data, sz);

			Con_Printf("clc fragpacket chunk, size %i\n", sz);
		}
		break;

	case clcsteam_steamid:;
		PIPE_WriteString(steamid);
		break;

	case clcsteam_auth:;
		strcpy(steamid, msgfuncs->ReadString());
		sz = msgfuncs->ReadShort();
		msgfuncs->ReadData(dat, sz);

		//Con_Printf("netname: \n", client->namebuf);

		///*
		PIPE_WriteByte(CL_AUTH_VALIDATE);
		PIPE_WriteByte(worldfuncs->GetSlot(client) + 1);
		PIPE_WriteString(steamid);
		PIPE_WriteCharArray(dat, sz);
		Con_Printf("sending CL_AUTH_VALIDATE for %i\n", worldfuncs->GetSlot(client) + 1);
		//*/

		break;

	case clcsteam_loadoutserial:;
		sz = msgfuncs->ReadShort();
		msgfuncs->ReadData(dat, sz);

		PIPE_WriteByte(CL_INV_UPDATECLIENT);
		PIPE_WriteByte(worldfuncs->GetSlot(client) + 1);
		PIPE_WriteCharArray(dat, sz);
		
		Con_Printf("got clcsteam_loadoutserial with %i bytes of data\n", sz);
		Con_Printf("sending CL_INV_UPDATECLIENT for %i\n", worldfuncs->GetSlot(client) + 1);
		break;
	}
}


void Network_ReadSVC() // client reading message from server
{
	int msg = msgfuncs->ReadByte();
	Con_Printf("steam svc %i\n", msg);
	switch (msg)
	{
	case svcsteam_fragpacket:;
		fragpacket_t *packet;
		int	chunkindex;
		int id;

		id = msgfuncs->ReadByte();
		chunkindex = msgfuncs->ReadByte();

		packet = FragPacket_Find(id);

		if (chunkindex == 0)
		{
			packet->totalpieces = msgfuncs->ReadByte();
		}
		else
		{
			int sz;
			byte data[FP_MAXCHUNK];
#if FP_MAXCHUNK > 0xFF // this is probably never gonna be true, but just in case...
			sz = msgfuncs->ReadShort();
#else
			sz = msgfuncs->ReadByte();
#endif
			msgfuncs->ReadData(data, sz);
			FragPacket_ReadChunk(packet, chunkindex, data, sz);
		}
		break;
	case svcsteam_requestinfo:;
		char steamid[64];
		strcpy(steamid, msgfuncs->ReadString());

		PIPE_WriteByte(CL_CONNECTSERVER);
		PIPE_WriteByte(1);
		PIPE_WriteString(steamid);
		Con_Printf("sending CL_CONNECTSERVER\n");


		PIPE_WriteByte(CL_AUTH_FETCH);
		Con_Printf("sending CL_AUTH_FETCH\n");
		
		///*
		Con_Printf("^xf0f%s\n", steamid);
		char cmd[256];
		memcpy(cmd, "steam_connectto ", strlen("steam_connectto ") + 1);
		strcat(cmd, steamid);
		strcat(cmd, "\n");
		cmdfuncs->AddText(cmd, false);
		Con_Printf("^xf0f%s", cmd);
		//*/
		break;

	case svcsteam_requestloadout:;
		Inventory_RequestSerial();
		break;

	case svcsteam_playtimedrop:;
		PIPE_WriteByte(CL_INV_TIMEDROP);
		Con_Printf("sending CL_INV_TIMEDROP\n");
		break;
	}
}


qboolean SV_NetworkMessage(client_t *client, unsigned short length, char *plugname)
{
	//Con_Printf("clcfte_pluginpacket %s %i\n", plugname, length);
	if (strcmp(plugname, PLUG_HEADER))
		return false;

	Network_ReadCLC(client);
	return true;
}


qboolean CL_NetworkMessage(unsigned short length, char *plugname)
{
	//Con_Printf("svcfte_pluginpacket %s %i\n", plugname, length);
	//return false;

	if (strcmp(plugname, PLUG_HEADER))
		return false;

	Network_ReadSVC();
	return true;
}


void SV_SendMessage(client_t *client, sizebuf_t *msg)
{
	return;

	msg = msgfuncs->SV_StartReliable(client, 20);
	int size_seek, msg_start;
	WriteHeader(msg, svcfte_pluginpacket, &size_seek, &msg_start);
	
	// fix header to have correct size
	msg->data[size_seek] = msg->cursize - msg_start;
	msgfuncs->SV_FinishReliable(client);

}


void CL_SendMessage(sizebuf_t *msg, sizebuf_t *rmsg)
{

}

void ClientDisconnect(client_t *client)
{
	int uid;
	fragpacket_t *lst, *hold, *prev;

	uid = worldfuncs->GetSlot(client);

	for (lst = fragpacketlist; lst; prev = lst, hold = lst->next, lst = hold)
	{
		if ((lst->id >> 16) != uid)
			continue;

		if (lst == fragpacketlist)
			fragpacketlist = lst->next;
		else
		{
			prev->next = lst->next;
		}

		for (int i = 0; i < 0xFF; i++) // transfer chunks into our new message
		{
			if (!lst->chunks)
				continue;
			corefuncs->Free(lst->chunks[i]);
		}

		corefuncs->Free(lst);
	}
}


qboolean Plug_Init(void)
{
	corefuncs = (plugcorefuncs_t*)plugfuncs->GetEngineInterface(plugcorefuncs_name, sizeof(*corefuncs));
	clientfuncs = (plugclientfuncs_t*)plugfuncs->GetEngineInterface(plugclientfuncs_name, sizeof(*clientfuncs));
	fsfuncs = (plugfsfuncs_t*)plugfuncs->GetEngineInterface(plugfsfuncs_name, sizeof(*fsfuncs));
	msgfuncs = (plugmsgfuncs_t*)plugfuncs->GetEngineInterface(plugmsgfuncs_name, sizeof(*msgfuncs));
	worldfuncs = (plugworldfuncs_t*)plugfuncs->GetEngineInterface(plugworldfuncs_name, sizeof(*worldfuncs));

	plugfuncs->ExportFunction("Tick", Steam_Tick);
	plugfuncs->ExportFunction("ExecuteCommand", Steam_ExecuteCommand);
	plugfuncs->ExportFunction("SV_NetworkMessage", SV_NetworkMessage);
	plugfuncs->ExportFunction("CL_NetworkMessage", CL_NetworkMessage);
	plugfuncs->ExportFunction("SV_SendMessage", SV_SendMessage);
	plugfuncs->ExportFunction("CL_SendMessage", CL_SendMessage);
	plugfuncs->ExportFunction("SV_ClientDisconnect", ClientDisconnect);
	cmdfuncs->AddCommand("steam_connect", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_startserver", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_disconnect", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_playingwith", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_authenticate", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_test", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_ifetchitems", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_ifetchproperty", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_ifetchinstanceproperty", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_iloadout_add", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_iloadout_flush", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_iloadout_getserial", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_igame_getloadout", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_igrantitem", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_richsetstate", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_richsetscore", Steam_ExecuteCommand, NULL);
	cmdfuncs->AddCommand("steam_richsetserver", Steam_ExecuteCommand, NULL);

	char cmd[256];
	Q_snprintf(cmd, MAX_STRING, "set steam_connected \"1\"\n");
	cmdfuncs->AddText(cmd, false);

	memset(sv_fragid, 0, sizeof(sv_fragid));
	cl_fragid = 0;

	pipeSendBuffer.cursize = 0;
	initPipes();

	Steam_Init();

	/*
	LPTSTR lpszPipename = TEXT("\\\\.\\pipe\\midnight-guns-steam");

	while (1)
	{
		steamPipe = CreateFile(
			lpszPipename,   // pipe name 
			GENERIC_READ |  // read and write access 
			GENERIC_WRITE,
			0,              // no sharing 
			NULL,           // default security attributes
			OPEN_EXISTING,  // opens existing pipe 
			0,              // default attributes 
			NULL);          // no template file 


		// Break if the pipe handle is valid. 

		if (steamPipe != INVALID_HANDLE_VALUE)
			break;

		// Exit if an error other than ERROR_PIPE_BUSY occurs. 

		if (GetLastError() != ERROR_PIPE_BUSY)
		{
			Con_Printf(TEXT("Could not open pipe. GLE=%d\n"), GetLastError());
			return false;
		}

		// All pipe instances are busy, so wait for 20 seconds. 

		if (!WaitNamedPipe(lpszPipename, 20000))
		{
			Con_Printf("Could not open pipe: 20 second wait timed out.");
			return false;
		}
	}


	DWORD dwMode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
	SetNamedPipeHandleState(
		steamPipe,			// pipe handle 
		&dwMode,			// new pipe mode 
		NULL,				// don't set maximum bytes 
		NULL);				// don't set maximum time 
	*/





	PIPE_WriteByte(CL_HANDSHAKE);


	//PIPE_WriteByte(CL_STARTSERVER);
	//PIPE_WriteByte(1);


	/*
	int success = SetNamedPipeHandleState(
		steamPipe,							// pipe handle 
		PIPE_READMODE_BYTE | PIPE_NOWAIT,	// new pipe mode 
		NULL,								// don't set maximum bytes 
		NULL);								// don't set maximum time 
	if (!success)
	{
		Con_Printf(TEXT("SetNamedPipeHandleState failed. GLE=%d\n"), GetLastError());
		return false;
	}
	Con_Printf("succ: %i\n", success);
	*/




	/*
	char buff[1024];
	if (PIPE_ReadString(buff))
	{
		Con_Printf("PIPE MSG: %s\n", buff);
	}
	*/



	//PIPE_WriteString("Test string");


	return true;
}
























