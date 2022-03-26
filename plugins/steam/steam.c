
#include "../plugin.h"
#include "windows.h"
#include "steam.h"
#include "fte_steam.h"


plugclientfuncs_t *clientfuncs;
plugfsfuncs_t *fsfuncs;


typedef unsigned char byte;

char SteamID[MAX_STRING];
HANDLE steamPipe;
void(*func_readarray[SV_MAX])();


typedef struct
{
	byte	data[MAX_BUFFSIZE];
	int		cursize;
} pipebuff_t;

pipebuff_t pipeSendBuffer;


int PIPE_SendData()
{
	unsigned long bytes_written;
	int succ = WriteFile(steamPipe, pipeSendBuffer.data, pipeSendBuffer.cursize, &bytes_written, NULL);

	if (succ)
		pipeSendBuffer.cursize -= bytes_written;

	return succ;
}





float PIPE_ReadFloat()
{
	float dat;
	int succ = ReadFile(steamPipe, &dat, 2, NULL, NULL);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


signed long PIPE_ReadLong()
{
	signed long dat;
	int succ = ReadFile(steamPipe, &dat, 2, NULL, NULL);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


signed short PIPE_ReadShort()
{
	signed short dat;
	int succ = ReadFile(steamPipe, &dat, 2, NULL, NULL);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


unsigned char PIPE_ReadByte()
{
	DWORD dat;
	int succ = ReadFile(steamPipe, &dat, 1, NULL, NULL);
	if (!succ)
	{
		return -1;
	}
	
	return (unsigned char)dat;
}


int PIPE_ReadString(char *buff)
{
	unsigned long amount_written;

	int i;
	for (i = 0; i < MAX_STRING; i++)
	{
		ReadFile(steamPipe, buff + i, 1, &amount_written, NULL);
		if (buff[i] == 0)
			break;
	}
	amount_written = i;

	return amount_written;
}


void PIPE_ReadCharArray(char *into, unsigned long *size)
{
	*size = (unsigned long)PIPE_ReadShort();
	int succ = ReadFile(steamPipe, &into, *size, size, NULL);
	if (!succ)
	{
		*size = 0;
	}
}








int PIPE_WriteFloat(float dat_float)
{
	long dat;
	memcpy(&dat, &dat_float, sizeof(long));

	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;
	pipeSendBuffer.data[seek + 2] = (dat >> 16) & 0xFF;
	pipeSendBuffer.data[seek + 3] = (dat >> 24) & 0xFF;

	pipeSendBuffer.cursize += 4;

	return true;
}


int PIPE_WriteLong(signed long dat)
{
	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;
	pipeSendBuffer.data[seek + 2] = (dat >> 16) & 0xFF;
	pipeSendBuffer.data[seek + 3] = (dat >> 24) & 0xFF;

	pipeSendBuffer.cursize += 4;

	return true;
}


int PIPE_WriteShort(signed short dat)
{
	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;

	pipeSendBuffer.cursize += 2;

	return true;
}


int PIPE_WriteByte(unsigned char dat)
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


int PIPE_WriteCharArray(char *dat, unsigned long size)
{
	PIPE_WriteShort((signed short)size);

	int seek = pipeSendBuffer.cursize;
	memcpy(&(pipeSendBuffer.data[seek]), dat, size);
	pipeSendBuffer.cursize += size;

	return true;
}







void Steam_SetName(void)
{
	Con_Printf("Steam: Set Name\n");

	char name[MAX_STRING];
	PIPE_ReadString(&name);


	Con_Printf("New Name: %s\n", name);
}


void Steam_SetSteamID(void)
{
	PIPE_ReadString(&SteamID);
	Con_Printf("Set SteamID: %s\n", SteamID);




	vmcvar_t cv_steamid;
	memcpy(&cv_steamid.string, SteamID, 256);
	cv_steamid.name = "steam_id";
	cv_steamid.group = "steam";
	cvarfuncs->Register(cv_steamid.name, cv_steamid.string, 1u<<5, cv_steamid.group);
}


void Steam_Auth_Retrieved(void)
{
	char dat[1024];
	unsigned long sz;
	PIPE_ReadCharArray(&dat, &sz);

	Con_Printf("Auth ticket of %i size recieved\n", (int)sz);

	//cmdfuncs->AddText("fs_flush\nvid_reload\nsetinfoblob _steam_auth \"data/_STEAMTEMP/authtoken\"\n", false);
	if (clientfuncs)
	{
		clientfuncs->SetUserInfo(0, "_steam_id", SteamID);
		clientfuncs->SetUserInfoBlob(0, "_steam_auth", &dat, sz);
	}
}


void Steam_Auth_Validated(void)
{
	char dat[MAX_STRING];
	PIPE_ReadString(&dat);

	Con_Printf("Auth ticket of %s authenticated\n", dat);
}




void Steam_Init(void)
{
	func_readarray[SV_SETNAME] = Steam_SetName;
	func_readarray[SV_STEAMID] = Steam_SetSteamID;
	func_readarray[SV_AUTH_RETRIEVED] = Steam_Auth_Retrieved;
	func_readarray[SV_AUTH_VALIDATED] = Steam_Auth_Validated;
}






void Steam_Tick(int *args)
{
	unsigned char index = PIPE_ReadByte();
	while (index != 255)
	{
		if (index < SV_MAX)
		{
			func_readarray[index]();
		}

		index = PIPE_ReadByte();
	}


	if (pipeSendBuffer.cursize)
	{
		PIPE_SendData();
	}





	return;
}


void Steam_ExecuteCommand()
{
	char cmd[256];
	cmdfuncs->Argv(0, cmd, sizeof(cmd));
	if (!strcmp(cmd, "steam_connect") && cmdfuncs->Argc() > 1)
	{
		char steamid[64];
		cmdfuncs->Argv(1, steamid, sizeof(steamid));


		//PIPE_WriteByte(CL_CONNECTSERVER);
		//PIPE_WriteByte(1);
		//PIPE_WriteString(steamid);

		
		PIPE_WriteByte(CL_AUTH_FETCH);


		return true;
	}
	else if (!strcmp(cmd, "steam_startserver"))
	{
		PIPE_WriteByte(CL_STARTSERVER);
		PIPE_WriteByte(1);

		return true;
	}
	else if (!strcmp(cmd, "steam_disconnect"))
	{
		PIPE_WriteByte(CL_DISCONNECTSERVER);

		return true;
	}
	else if (!strcmp(cmd, "steam_authenticate"))
	{
		char steamid[64];
		cmdfuncs->Argv(1, steamid, sizeof(steamid));

		if (fsfuncs)
		{
			///*
			qhandle_t handle;
			//fsfuncs->Open(strcat(strcat("data/_STEAMTEMP/", steamid), "/authtoken"), &handle, 0);
			fsfuncs->Open("data/_STEAMTEMP/76561198020080134/authtoken", &handle, 1);
			if (handle >= 0)
			{
				char dat[1024];
				int length = fsfuncs->Read(handle, &dat, 1024);

				PIPE_WriteByte(CL_AUTH_VALIDATE);
				PIPE_WriteString(steamid);

				PIPE_WriteCharArray(&dat, length);
				fsfuncs->Close(handle);

				Con_Printf("file read success\n");
			}
			else
			{
				Con_Printf("file read failure\n");
			}
			//*/
		}

		return true;
	}
	
	return false;
}


qboolean Plug_Init(void)
{
	clientfuncs = (plugclientfuncs_t*)plugfuncs->GetEngineInterface(plugclientfuncs_name, sizeof(*clientfuncs));
	fsfuncs = (plugfsfuncs_t*)plugfuncs->GetEngineInterface(plugfsfuncs_name, sizeof(*fsfuncs));


	plugfuncs->ExportFunction("Tick", Steam_Tick);
	plugfuncs->ExportFunction("ExecuteCommand", Steam_ExecuteCommand);
	cmdfuncs->AddCommand("steam_connect");
	cmdfuncs->AddCommand("steam_startserver");
	cmdfuncs->AddCommand("steam_disconnect");




	pipeSendBuffer.cursize = 0;

	Steam_Init();

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
























