/*NEXUS IRC session BNC, by Subsentient. This software is public domain.*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

#include "server.h"
#include "netcore.h"
#include "config.h"

/**This file has our IRC pseudo-server that we run ourselves and its interaction with clients.**/

#define NEXUS_FAKEHOST "NEXUS"

//Globals
struct ClientTree *ClientTreeCore;

//Functions

struct ClientTree *Server_ClientTree_Lookup(const int Descriptor)
{
	struct ClientTree *Worker = ClientTreeCore;
	
	for (; Worker; Worker = Worker->Next)
	{
		if (Worker->Descriptor == Descriptor)
		{
			return Worker;
		}
	}
	
	return NULL;
}

struct ClientTree *Server_ClientTree_Add(const struct ClientTree *const InStruct)
{
	struct ClientTree *Worker = ClientTreeCore, *TempNext, *TempPrev;
	
	if (!ClientTreeCore)
	{
		Worker = ClientTreeCore = calloc(1, sizeof(struct ClientTree)); //use calloc to zero it out
	}
	else
	{
		while (Worker->Next) Worker = Worker->Next;
		Worker->Next = calloc(1, sizeof(struct ClientTree));
		Worker->Next->Prev = Worker;
		Worker = Worker->Next;
	}
	
	TempNext = Worker->Next;
	TempPrev = Worker->Prev;
	
	*Worker = *InStruct;
	
	Worker->Next = TempNext;
	Worker->Prev = TempPrev;
	
	return Worker;
}


void Server_ClientTree_Shutdown(void)
{
	struct ClientTree *Worker = ClientTreeCore, *Next;

	for (; Worker; Worker = Next)
	{
		Next = Worker->Next;
		free(Worker);
	}
	
	ClientTreeCore = NULL;
}

bool Server_ClientTree_Del(const int Descriptor)
{
	struct ClientTree *Worker = ClientTreeCore;
	
	if (!ClientTreeCore) return false;
	
	for (; Worker; Worker = Worker->Next)
	{
		if (Worker->Descriptor == Descriptor)
		{ //Match.
			if (Worker == ClientTreeCore)
			{
				if (Worker->Next)
				{ //We're the first one but there are others ahead of us.
					ClientTreeCore = Worker->Next;
					ClientTreeCore->Prev = NULL;
					free(Worker);
				}
				else
				{ //Just us.
					free(Worker);
					ClientTreeCore = NULL;
				}
			}
			else
			{
				Worker->Prev->Next = Worker->Next;
				if (Worker->Next) Worker->Next->Prev = Worker->Prev;
				free(Worker);
			}
			
			return true;
		}
	}
	
	return false;
}

bool Server_ForwardToAll(const char *const InStream)
{ //This function sends the provided text stream to all clients. Very simple.
	struct ClientTree *Worker = ClientTreeCore;
	
	if (!Worker) return false;
	
	for (; Worker; Worker = Worker->Next)
	{
		Net_Write(Worker->Descriptor, InStream, strlen(InStream));
	}
	return true;
}

void Server_SendQuit(const int Descriptor)
{ //Tells all clients to quit.
	struct ClientTree *Worker = ClientTreeCore;
	char OutBuf[2048];
	
	for (; Worker; Worker = Worker->Next)
	{
		//If not on "everyone" mode we check if the descriptor matches.
		if (Descriptor != -1 && Descriptor != Worker->Descriptor) continue; 
		
		snprintf(OutBuf, sizeof OutBuf, ":%s!%s@%s QUIT :Quit forced by NEXUS\r\n", IRCConfig.Nick, Worker->Ident, Worker->IP);
		Net_Write(Worker->Descriptor, OutBuf, strlen(OutBuf));
	}
}

void Server_SendIRCWelcome(const int ClientDescriptor)
{
	char OutBuf[2048];
	struct ClientTree *Client = Server_ClientTree_Lookup(ClientDescriptor);
	
	if (!Client) return;
	
	//First thing we send is our greeting, telling them they're connected OK.
	snprintf(OutBuf, sizeof OutBuf, ":" NEXUS_FAKEHOST " 001 %s :NEXUS is forwarding you to server %s:%hu\r\n",
			Client->OriginalNick, IRCConfig.Server, IRCConfig.PortNum);
	Net_Write(Client->Descriptor, OutBuf, strlen(OutBuf));
	
	//Tell them to change their nickname to match what we have on file.
	snprintf(OutBuf, sizeof OutBuf, ":%s!%s@%s NICK :%s\r\n", Client->OriginalNick, Client->Ident, Client->IP, IRCConfig.Nick);
	printf("%s", OutBuf);
	Net_Write(Client->Descriptor, OutBuf, strlen(OutBuf));
	
	snprintf(OutBuf, sizeof OutBuf, ":NEXUS!NEXUS@NEXUS PRIVMSG %s :Welcome to NEXUS %s.\r\n", IRCConfig.Nick, IRCConfig.Nick);
	Net_Write(Client->Descriptor, OutBuf, strlen(OutBuf));
	
}

struct ClientTree *Server_AcceptLoop(void)
{
	struct ClientTree TempClient;
	struct NetReadReturn NRR;
	char InBuf[2048];
	struct ClientTree *Client = NULL;
	bool UserProvided = false, NickProvided = false;
		
	if (!Net_AcceptClient(&TempClient.Descriptor, TempClient.IP, sizeof TempClient.IP))
	{ //No client.
		return NULL;
	}


	///Apparently there is a client.
	Client = Server_ClientTree_Add(&TempClient); //Store their information.

	/**Continuously try to read their replies until we get them.**/
	while (!UserProvided || !NickProvided)
	{ //Wait for their greeting.
		while (1)
		{
			NRR = Net_Read(Client->Descriptor, InBuf, sizeof InBuf, true);
			
			if (NRR.Status == 0 || (NRR.Status == -1 && NRR.Errno != EWOULDBLOCK))
			{ //We lost them.
				close(Client->Descriptor); //Close their connection.
				Server_ClientTree_Del(Client->Descriptor); //Delete their record.
				return NULL;
			}
			else if (NRR.Status == -1 && NRR.Errno == EWOULDBLOCK)
			{ //They just didn't reply to us yet.
				continue;
				usleep(1500);
			}
			else break;
		}
		
		//Does it start with USER?
		if (!strncmp(InBuf, "USER", sizeof "USER" - 1) || !strncmp(InBuf, "user", sizeof "user" - 1))
		{ //This information is needed to fool the IRC clients.
			const char *TWorker = InBuf + sizeof "USER";
			unsigned Inc = 0;
			
			while (*TWorker == ' ' || *TWorker == ':') ++TWorker;
			
			for (; TWorker[Inc] != ' ' && TWorker[Inc] != '\0' && Inc < sizeof Client->Ident - 1; ++Inc)
			{ //Copy in the ident they sent us.
				Client->Ident[Inc] = TWorker[Inc];
			}
			Client->Ident[Inc] = '\0';
		
			UserProvided = true;
			
		}
		else if (!strncmp(InBuf, "NICK", sizeof "NICK" - 1) || !strncmp(InBuf, "nick", sizeof "nick" - 1))
		{
			const char *TWorker = InBuf + sizeof "nick";
			
			while (*TWorker == ' ' || *TWorker == ':') ++TWorker;
				
			strncpy(Client->OriginalNick, TWorker, sizeof Client->OriginalNick - 1); //Copy in their chosen nick.
			Client->OriginalNick[sizeof Client->OriginalNick - 1] = '\0';
			
			NickProvided = true;
			
		}
		continue;
	}
	
	//Time to welcome them.
	Server_SendIRCWelcome(Client->Descriptor);
	
	//Return the client we found.
	return Client;
}

void Server_Loop(void)
{ //main loop for the NEXUS server.	
	Server_AcceptLoop();
	
	
}
