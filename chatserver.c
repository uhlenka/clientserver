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
#include <signal.h>

#define PROTOPORT 36724 /* default protocol port number */
#define QLEN 30 /* size of request queue */
#define MAXCLIENTS 2 /* maximum allowable number of clients */
#define BUFSIZE 240 /* server's maximum buffer size */

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

/* global variables */
typedef struct {
		int used;
		char *name;
		int socket;
		char *clibuf;
		int charcount;
		int strikes;
		int resync;
	} clientinfo;
clientinfo clientarray[MAXCLIENTS]; /* structure to hold client info */
fd_set total_set, read_set; /* fd_sets to use with select */
char buf[BUFSIZE]; /* buffer for sending and receiving messages */
char *bufp = buf;
	
/* internal functions */
static void initialize_clientinfo(int client_no);
static void clear_clientinfo(int client_no);
static void write_to_client(int socket, int client_no, int clear);
static void read_from_client(int socket, int client_no);


/* Main */
int main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);

	struct hostent *ptrh; /* pointer to a host table entry */
	struct protoent *ptrp; /* pointer to a protocol table entry */
	struct sockaddr_in sad; /* structure to hold server's address */
	struct sockaddr_in cad; /* structure to hold client address */
	struct timeval timeout; /* structure to hold timeout info for select */
	int listensocket, tempsd; /* socket descriptors for listen port and acceptance */
	int port; /* protocol port number */
	int alen; /* length of address */
	
	timeout.tv_sec = 0; timeout.tv_usec = 0; /*initialize timeval struct */
	FD_ZERO (&total_set); /* initialize fd_set */
	int i;
	for (i=0;i<30;i++) { /* initialize client info structure */
		initialize_clientinfo(i);
	}
	
	#ifdef WIN32
	WSADATA wsaData;
	WSAStartup(0x0101, &wsaData);
	#endif
	
	memset(buf, 0, sizeof(buf)); /* clear read/write buffer */
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
		perror ("socket");
		exit(1);
	}
	
	/* Bind a local address to the socket */
	if (bind(listensocket, (struct sockaddr *)&sad, sizeof(sad)) < 0) {
		perror ("bind");
		exit(1);
	}
	
	/* Specify size of request queue */
	if (listen(listensocket, QLEN) < 0) {
		perror ("listen");
		exit(1);
	}
	FD_SET (listensocket, &total_set);
	
	int client_no;
	
	/* Main server loop - accept and handle requests */
	while (1) {
		read_set = total_set;
		if (select (FD_SETSIZE, &read_set, NULL, NULL, NULL) < 0) {
			perror ("select");
			exit (1);
		}		
		for (i=0; i<FD_SETSIZE; i++) {
			if (FD_ISSET (i, &read_set)) {
				if (i == listensocket) {
					/* connection ready to be accepted */
					alen = sizeof(cad);
					if ((tempsd = accept(listensocket, (struct sockaddr *)&cad, &alen)) < 0) {
						perror ("accept");
						exit (1);
					}
					for (client_no=0; client_no<MAXCLIENTS; client_no++) {
						if (clientarray[client_no].used == 0)
						break;
					}
					if (client_no < MAXCLIENTS) { /*add new connection to clientarray */
						fprintf (stderr, "Client %d accepted\n", client_no);
						FD_SET (tempsd, &total_set);
						clientarray[client_no].used = 1;
						clientarray[client_no].socket = tempsd;
						sprintf(buf, "You are client number %d.\n", client_no); //don't send anything here
						write_to_client(tempsd, client_no, 0);
					}
					else { /* send no vacancy message and drop connection */
						fprintf (stderr, "Client %d refused\n", client_no);
						sprintf(buf, "(snovac)"); //change to properly formatted message
						write_to_client(tempsd, client_no, 0);
						closesocket(tempsd);
					}
				}
				else {
					/* data available on already-connected socket */
					int nbytes = read (i, buf, BUFSIZE);
					for (client_no=0; client_no<MAXCLIENTS; client_no++) {
						if (clientarray[client_no].socket == i)
						break;
					}
					if (nbytes < 0) {
						perror ("read");
						exit (1);
					}
					else if (nbytes == 0) { /* client has died - drop its connection and clear its info */
						closesocket(i);
						fprintf (stderr, "Client %d dropped\n", client_no);
						FD_CLR (i, &total_set);
						clear_clientinfo(client_no);
					}
					else { /* transfer data to client's buffer and attempt to parse message */
						fprintf (stderr, "Data available for client %d\n", client_no);
						read_from_client(i, client_no);
						//attempt to parse message here
						fprintf (stderr, "Server: got message: '%s' from client %d\n", clientarray[client_no].clibuf, client_no);
						memset(buf, 0, BUFSIZE);
					}
				}
			}
		}
	}
	
	exit(0);
}


void read_from_client(int socket, int client_no)
{
	char *tempstart = malloc(BUFSIZE*sizeof(char));
	char *tempend = tempstart;
	char *tempbufp = buf;
	int numchars = 0;
	while (*tempbufp != 0) {
		if (isprint(*tempbufp) != 0) {
			*tempend = *tempbufp;
			tempend++;
		}
		tempbufp++;
		numchars++;
	}
	*tempend = *tempbufp;
	strncat(clientarray[client_no].clibuf, tempstart, BUFSIZE-clientarray[client_no].charcount);
	free(tempstart);
}


void write_to_client(int socket, int client_no, int clear)
{
	if (write(socket, &buf, strlen(buf)*sizeof(char)) < 0) {
fprintf (stderr, "Write error\n");
		if (clear != 0) {
			FD_CLR (socket, &total_set);
			clear_clientinfo(client_no);
		}
	}
	memset(buf, 0, BUFSIZE);
}


void clear_clientinfo(int client_no)
{
	clientarray[client_no].used = 0;
	clientarray[client_no].socket = -1;
	clientarray[client_no].name = NULL;
	memset(clientarray[client_no].clibuf, 0, BUFSIZE);
	clientarray[client_no].charcount = 0;
	clientarray[client_no].strikes = 0;
	clientarray[client_no].resync = 0;
}


void initialize_clientinfo(int client_no)
{
	clientarray[client_no].used = 0;
	clientarray[client_no].socket = -1;
	clientarray[client_no].name = NULL;
	clientarray[client_no].clibuf = malloc(BUFSIZE*sizeof(char));
	clientarray[client_no].charcount = 0;
	clientarray[client_no].strikes = 0;
	clientarray[client_no].resync = 0;
}
