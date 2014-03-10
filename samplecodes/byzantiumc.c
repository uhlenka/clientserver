/* byzantiumc.c - code for client program that communicates with a chatserver */
#define closesocket close
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

extern int errno;
#define PROTOPORT 36724 /* default protocol port number */
char localhost[] = "localhost"; /* default host name */
char uhlenka[] = "UHLENKA"; /* default client name */

/*------------------------------------------------------------------------
* Program: byzantiumc
*
* Purpose: communicate with a chatserver and play Byzantium
*
* Syntax: byzantiumc [-h] [-s server] [-p port] [-n name] [-m]
*
* -h 	  the client will print a description of its parameters and then exit
* server  IP address or name of a computer on which server is executing
* port 	  protocol port number server is using
* name 	  the name the client will attempt to use in the game
* -m 	  the client will run in manual mode
*
* All arguments are optional. The default values are as follows:
* 	server = "localhost"
* 	port = 36724
* 	name = "UHLENKA"
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
	char *host; /* pointer to host name */
	int n; /* number of characters read */
	char buf[1000]; /* buffer for data from the server */
    
    char *server = NULL;
    int port = -1;
    char *name = NULL;
    int manual = 0;
	
	fd_set total_set, read_set; /* fd_sets to use with select */
	FD_ZERO (&total_set); /* initialize fd_set */
	FD_SET (STDIN_FILENO, &total_set);
	
	memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */
	sad.sin_family = AF_INET; /* set family to Internet */
    
    /* Get values from command line. */
    int i;
    for (i=1;i<argc;i++) {
        if (server == NULL && strcmp(argv[i], "-s") == 0 && (i+1) < argc) {
            server = argv[i+1];
        }
        else if (port == -1 && strcmp(argv[i], "-p") == 0 && (i+1) < argc) {
            sscanf(argv[i+1], "%d", &port);
        }
        else if (name == NULL && strcmp(argv[i], "-n") == 0 && (i+1) < argc) {
            name = argv[i+1];
        }
        else if (manual == 0 && strcmp(argv[i], "-m") == 0) {
            manual = 1;
        }
        else if (manual == 0 && strcmp(argv[i], "-h") == 0) {
            fprintf(stderr, "This client implements the following (optional) command line parameters:\n-h 	    the client will print a description of its parameters and then exit\n-s server   server = IP address or name of a computer on which server is executing (default: \"localhost\")\n-p port     port = protocol port number server is using (default: 36724)\n-n name     name = the name the client will attempt to use in the game (default: \"UHLENKA\")\n-m 	    the client will run in manual mode\n");
            exit(0);
        }
    }
    if (server == NULL) {
        server = localhost;
    }
    if (port < 0) {
        port = PROTOPORT;
    }
    if (name == NULL) {
        name = uhlenka;
    }

    if (port > 0) /* test for legal value */
		sad.sin_port = htons((u_short)port);
    else { /* print error message and exit */
        fprintf(stderr,"bad port number %s\n",argv[2]);
        exit(1);
    }
	host = server;
	
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

	/* Send cjoin. */
	sprintf(buf, "(cjoin(%s))", name);
    write(sd, &buf, strlen(buf)*sizeof(char));
    memset(buf, 0, 1000);
    
    if (manual != 0) {
    	fprintf(stderr, "Whatever text you enter will be sent to the server, as is, with no interpretation.\n");
    	fprintf(stderr, "Likewise, whatever the server sends to this client will be printed to the screen, as is, with no interpretation.\n");
    }
	
	while (1) {
        if (manual == 0) {
            int n = recv(sd, buf, sizeof(buf), 0);
            if (n == 0) {
                closesocket(i);
                fprintf (stderr, "Connection dropped\n");
                exit(0);
            }
            else {
                write(1,buf,n); write(1, "\n", sizeof(char));
            }
            sleep(2);
            sprintf(buf, "(cchat(any)(hello))");
            write(sd, &buf, strlen(buf)*sizeof(char));
            memset(buf, 0, 1000);
		}
        else {
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
    }
	
	/* Terminate the client program gracefully. */
	closesocket(sd);
	exit(0);
}
