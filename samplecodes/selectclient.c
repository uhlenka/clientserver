/* client.c - code for example client program that uses TCP */
#ifndef unix
#define WIN32
#include <windows.h>
#include <winsock.h>
#else
#define closesocket close
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#endif
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define PROTOPORT 36724 /* default protocol port number */
extern int errno;
char localhost[] = "localhost"; /* default host name */
char *dname = "max"; /* default client name */
/*------------------------------------------------------------------------
* Program: client
*
* Purpose: allocate a socket, connect to a server, and print all output
*
* Syntax: client [ name ]
*
* host - name of a computer on which server is executing
* port - protocol port number server is using
*
* Note: Both arguments are optional. If no host name is specified,
* the client uses "localhost"; if no protocol port is
* specified, the client uses the default given by PROTOPORT.
*
*------------------------------------------------------------------------
*/

main(argc, argv)
int argc;
char *argv[];
{
	struct hostent *ptrh; /* pointer to a host table entry */
	struct protoent *ptrp; /* pointer to a protocol table entry */
	struct sockaddr_in sad; /* structure to hold an IP address */
	int sd; /* socket descriptor */
	int port; /* protocol port number */
	char *host; /* pointer to host name */
	int n; /* number of characters read */
	char buf[1000]; /* buffer for data from the server */
	
	fd_set total_set, read_set; /* fd_sets to use with select */
	FD_ZERO (&total_set); /* initialize fd_set */
	FD_SET (STDIN_FILENO, &total_set);
	
	#ifdef WIN32
	WSADATA wsaData;
	WSAStartup(0x0101, &wsaData);
	#endif
	
	memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */
	sad.sin_family = AF_INET; /* set family to Internet */
	
	port = PROTOPORT; /* use default port number */
	if (port > 0) /* test for legal value */
		sad.sin_port = htons((u_short)port);
	else { /* print error message and exit */
		fprintf(stderr,"bad port number %s\n",argv[2]);
		exit(1);
	}

	host = localhost;
	
	/* Convert host name to equivalent IP address and copy to sad. */
	ptrh = gethostbyname(host);
	if ( ((char *)ptrh) == NULL ) {
		fprintf(stderr,"invalid host: %s\n", host);
		exit(1);
	}
	memcpy(&sad.sin_addr, ptrh->h_addr, ptrh->h_length);
	
	/* Map TCP transport protocol name to protocol number. */
	if ( ((long)(ptrp = getprotobyname("tcp"))) == 0) {
		fprintf(stderr, "cannot map \"tcp\" to protocol number");
		exit(1);
	}
	
	/* Create a socket. */
	sd = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
	if (sd < 0) {
		fprintf(stderr, "socket creation failed\n");
		exit(1);
	}
	
	/* Connect the socket to the specified server. */
	if (connect(sd, (struct sockaddr *)&sad, sizeof(sad)) < 0) {
		fprintf(stderr,"connect failed\n");
		exit(1);
	}
	FD_SET (sd, &total_set);
	
	int i;
	int time = 0;
	
	while (1) {
		if (time == 0) {
			if (argc > 1) { /* use supplied name */
				sprintf(buf, "(cjoin(%s))", argv[1]);
			} else {
				sprintf(buf, "(cjoin(%s))", dname); /* use default name */
			}
			write(sd, &buf, strlen(buf)*sizeof(char));
			memset(buf, 0, 1000);
			time++;
		}
		
		read_set = total_set;
		if (select (FD_SETSIZE, &read_set, NULL, NULL, NULL) < 0) {
			perror ("select");
			exit (1);
		}
		if (FD_ISSET (STDIN_FILENO, &read_set)) {
			int n = read(STDIN_FILENO, buf, 1000);
			if (strncmp(buf, "\n", 1 && strncmp(buf, "\r", 1) != 0) != 0) {
				write(sd, &buf, n);
			}
		}
		if (FD_ISSET (sd, &read_set)) {
			n = recv(sd, buf, sizeof(buf), 0);
			if (n == 0) {
				closesocket(i);
				fprintf (stderr, "Connection dropped\n");
				exit(1);
			}
			else {
				write(1,buf,n); write(1, "\n", sizeof(char));
			}
		}
		memset(buf, 0, 1000);
	}
	
	/* Terminate the client program gracefully. */
	closesocket(sd);
	exit(0);
}
