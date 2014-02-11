/* server.c - code for example server program that uses TCP */
#ifndef unix
#define WIN32
#include <windows.h>
#include <winsock.h>
#else
#define closesocket close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#define PROTOPORT 36724 /* default protocol port number */
#define QLEN 6 /* size of request queue */
int visits = 0; /* counts client connections */
/*------------------------------------------------------------------------
* Program: server
*
* Purpose: allocate a socket and then repeatedly execute the following:
* (1) wait for the next connection from a client
* (2) send a short message to the client
* (3) close the connection
* (4) go back to step (1)
*
* Syntax: server [ port ]
*
* port - protocol port number to use
*
* Note: The port argument is optional. If no port is specified,
* the server uses the default given by PROTOPORT.
*
*------------------------------------------------------------------------
*/

typedef struct { /* structure to hold client info */
		int used;
		char *name;
		int socket;
		char *bufstart;
		char *bufend;
		int charcount;
		int strikes;
		int resync;
	} clientinfo;
	
static void clear_clientinfo(clientinfo *info);

int main(int argc, char **argv)
{
	struct hostent *ptrh; /* pointer to a host table entry */
	struct protoent *ptrp; /* pointer to a protocol table entry */
	struct sockaddr_in sad; /* structure to hold server's address */
	struct sockaddr_in cad; /* structure to hold client address */
	
	clientinfo clientarray[30]; /* array of clientinfo structures */
	int i;
	for (i=0;i<30;i++) {
		clear_clientinfo(&clientarray[i]);
	}
	
	int listensocket, tempsd; /* socket descriptors for listen port and acceptance */
	int port; /* protocol port number */
	int alen; /* length of address */
	char buf[240]; /* buffer for string the server sends */
	
	#ifdef WIN32
	WSADATA wsaData;
	WSAStartup(0x0101, &wsaData);
	#endif
	
	memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */
	sad.sin_family = AF_INET; /* set family to Internet */
	sad.sin_addr.s_addr = INADDR_ANY; /* set the local IP address */
	
	/* Check command-line argument for protocol port and extract */
	/* port number if one is specified. Otherwise, use the default */
	/* port value given by constant PROTOPORT */
	if (argc > 1) { /* if argument specified */
		port = atoi(argv[1]); /* convert argument to binary */
	} else {
		port = PROTOPORT; /* use default port number */
	}
	if (port > 0) { /* test for illegal value */
		sad.sin_port = htons((u_short)port);
	} else { /* print error message and exit */
		fprintf(stderr,"bad port number %s\n",argv[1]);
		exit(1);
	}
	
	/* Map TCP transport protocol name to protocol number */
	if ( ((long)(ptrp = getprotobyname("tcp"))) == 0) {
		fprintf(stderr, "cannot map \"tcp\" to protocol number");
		exit(1);
	}
	
	/* Create a socket */
	listensocket = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
	if (listensocket < 0) {
		fprintf(stderr, "socket creation failed\n");
		exit(1);
	}
	
	/* Bind a local address to the socket */
	if (bind(listensocket, (struct sockaddr *)&sad, sizeof(sad)) < 0) {
		fprintf(stderr,"bind failed\n");
		exit(1);
	}
	
	/* Specify size of request queue */
	if (listen(listensocket, QLEN) < 0) {
		fprintf(stderr,"listen failed\n");
		exit(1);
	}
	
	/* Main server loop - accept and handle requests */
	while (visits < 3) {
		alen = sizeof(cad);
		if ( (tempsd=accept(listensocket, (struct sockaddr *)&cad, &alen)) < 0) {
			fprintf(stderr, "accept failed\n");
			exit(1);
		}
		for (i=0;i<30;i++) {
			if (clientarray[i].used == 0)
				break;
		}
		clientarray[i].used = 1;
		clientarray[i].socket = tempsd;
		visits++;
		sprintf(buf,"You are client number %d.\n", i);
		send(clientarray[i].socket, buf, strlen(buf), 0);
		sprintf(buf,"This server has been contacted %d time%s\n", visits, visits==1?".":"s.");
		send(clientarray[i].socket, buf, strlen(buf), 0);
		closesocket(clientarray[i].socket);
		if (i > 0)
		clear_clientinfo(&clientarray[i]);
	}
	
	printf("This server was contacted %d times.\n", visits);
	exit(0);
}


void clear_clientinfo(clientinfo *info)
{
	info[0].used = 0;
	info[0].bufend = info[0].bufstart;
	info[0].charcount = 0;
	info[0].strikes = 0;
	info[0].resync = 0;
}
