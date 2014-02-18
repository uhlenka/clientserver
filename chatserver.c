/* chatserver.c - code for server program that allows clients to chat with one another */
//#ifndef unix
//#define WIN32
//#include <windows.h>
//#include <winsock.h>
//#else
#define closesocket close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
//#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <netdb.h>

#define PROTOPORT 36724 /* default protocol port number */
#define QLEN 30 /* size of request queue */
#define MAXCLIENTS 2 /* maximum allowable number of clients */
#define BUFSIZE 426 /* server's maximum buffer size */
#define MAXMESSAGE 425 /* length of maximum allowable message */
#define NAMESIZE 12 /* maximum client name length */


// TODO: respond to successful message parse (& test), command line arguments, naming algorithm
// don't forget to set MAXCLIENTS to 30 and uncomment WINDOWS lines above and below


/*------------------------------------------------------------------------
* Program: chatserver
*
* Purpose: allocate a socket and then repeatedly execute the following:
* (1) wait for input from a client or a new client connection
* (2) receive client messages or accept a new client if MAXCLIENTS is not reached
* (3) respond appropriately to any client messages
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
	
/* helper functions */
static void initialize_clientinfo(int client_no);
static void clear_clientinfo(int client_no);
static void write_to_client(int socket, int client_no, int clear);
static void read_from_client(int socket, int client_no);
static void parse_message(int client_no);
static int  find_right_paren(char **current, int *numchars);
static void send_strike(int client_no, char reason);


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
	//WSADATA wsaData;
	//WSAStartup(0x0101, &wsaData);
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
	
	/* Main server loop */
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
						fprintf (stderr, "Client %d dropped - died\n", client_no);
						FD_CLR (i, &total_set);
						clear_clientinfo(client_no);
					}
					else { /* transfer data to client's buffer and attempt to parse message */
						fprintf (stderr, "Data available for client %d\n", client_no);
						read_from_client(i, client_no);
                        memset(buf, 0, BUFSIZE);
						parse_message(client_no);
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
	int numchars = clientarray[client_no].charcount;
	while (*tempbufp != 0 && numchars < BUFSIZE) {
		if (isprint(*tempbufp) != 0) {
			*tempend = *tempbufp;
			tempend++;
		}
		tempbufp++;
		numchars++;
	}
	*tempend = 0;
	strncat(clientarray[client_no].clibuf, tempstart, BUFSIZE-clientarray[client_no].charcount);
    clientarray[client_no].charcount = numchars;
	free(tempstart);
}


static void parse_message(int client_no)
{
    char *tempbufp = clientarray[client_no].clibuf;
    int numchars;
    //char buffer[BUFSIZE];
    
    if (clientarray[client_no].resync == 0) { /* not resychronizing - parse normally */
        numchars = 0;
        if (*tempbufp != '(') {
            if (*tempbufp == 0) {
                return;
            }
            send_strike(client_no, 'm');
            if (clientarray[client_no].used != 0) {
                sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                clientarray[client_no].charcount = 0;
                parse_message(client_no);
            }
        }
        numchars++;
        tempbufp++;
        if (*tempbufp != 'c') {
            if (*tempbufp == 0) {
                return;
            }
            send_strike(client_no, 'm');
            if (clientarray[client_no].used != 0) {
                sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                clientarray[client_no].charcount = 0;
                parse_message(client_no);
            }
        }
        numchars++;
        tempbufp++;
        if (*tempbufp == 'c') { /* look for chat message */
            numchars++;
            tempbufp++;
            if (*tempbufp == 'h') {
                numchars++;
                tempbufp++;
                if (*tempbufp == 'a') {
                    numchars++;
                    tempbufp++;
                    if (*tempbufp == 't') {
                        numchars++;
                        tempbufp++;
                        if (*tempbufp == '(') {
                            char *recipients = tempbufp; recipients++;
                            int result = find_right_paren(&tempbufp, &numchars);
                            if (result == 0) {
                                return;
                            }
                            else if (result = -1) {
                                send_strike(client_no, 'l');
                                if (clientarray[client_no].used != 0) {
                                    sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                    clientarray[client_no].charcount = 0;
                                    parse_message(client_no);
                                }
                            }
                            numchars++;
                            tempbufp++;
                            if (*tempbufp == '(') {
                                char *message = tempbufp; message++;
                                int result = find_right_paren(&tempbufp, &numchars);
                                if (result == 0) {
                                    return;
                                }
                                else if (result = -1) {
                                    send_strike(client_no, 'l');
                                    if (clientarray[client_no].used != 0) {
                                        sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                        clientarray[client_no].charcount = 0;
                                        parse_message(client_no);
                                    }
                                }
                                numchars++;
                                tempbufp++;
                                if (*tempbufp == ')') { /* proper cchat - truncate message if necessary and send to recipients */
                                    
                                    tempbufp++;
                                    if (*tempbufp == 0) {
                                        memset(clientarray[client_no].clibuf, 0, BUFSIZE);
                                        clientarray[client_no].charcount = 0;
                                        return;
                                    }
                                    else {
                                        sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                        clientarray[client_no].charcount = 0;
                                        parse_message(client_no);
                                    }
                                }
                                else if (*tempbufp == 0) { /* message not finished - stop parsing */
                                    return;
                                }
                                else { /* message malformed - send strike and resynchronize */
                                    send_strike(client_no, 'm');
                                    if (clientarray[client_no].used != 0) {
                                        sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                        clientarray[client_no].charcount = 0;
                                        parse_message(client_no);
                                    }
                                }
                            }
                            else if (*tempbufp == 0) {
                                return;
                            }
                            else {
                                send_strike(client_no, 'm');
                                if (clientarray[client_no].used != 0) {
                                    sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                    clientarray[client_no].charcount = 0;
                                    parse_message(client_no);
                                }
                            }
                        }
                        else if (*tempbufp == 0) {
                            return;
                        }
                        else {
                            send_strike(client_no, 'm');
                            if (clientarray[client_no].used != 0) {
                                sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                clientarray[client_no].charcount = 0;
                                parse_message(client_no);
                            }
                        }
                    }
                    else if (*tempbufp == 0) {
                        return;
                    }
                    else {
                        send_strike(client_no, 'm');
                        if (clientarray[client_no].used != 0) {
                            sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                            clientarray[client_no].charcount = 0;
                            parse_message(client_no);
                        }
                    }
                }
                else if (*tempbufp == 0) {
                    return;
                }
                else {
                    send_strike(client_no, 'm');
                    if (clientarray[client_no].used != 0) {
                        sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                        clientarray[client_no].charcount = 0;
                        parse_message(client_no);
                    }
                }
            }
            else if (*tempbufp == 0) {
                return;
            }
            else {
                send_strike(client_no, 'm');
                if (clientarray[client_no].used != 0) {
                    sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                    clientarray[client_no].charcount = 0;
                    parse_message(client_no);
                }
            }
        }
        else if (*tempbufp == 'j') { /* look for join message */
            numchars++;
            tempbufp++;
            if (*tempbufp == 'o') {
                numchars++;
                tempbufp++;
                if (*tempbufp == 'i') {
                    numchars++;
                    tempbufp++;
                    if (*tempbufp == 'n') {
                        numchars++;
                        tempbufp++;
                        if (*tempbufp == '(') {
                            char *name = tempbufp; name++;
                            int result = find_right_paren(&tempbufp, &numchars);
                            if (result == 0) {
                                return;
                            }
                            else if (result = -1) {
                                send_strike(client_no, 'l');
                                if (clientarray[client_no].used != 0) {
                                    sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                    clientarray[client_no].charcount = 0;
                                    parse_message(client_no);
                                }
                            }
                            numchars++;
                            tempbufp++;
                            if (*tempbufp == ')') { /* proper cjoin - apply naming algorithm if necessary and assign name */
                                
                                tempbufp++;
                                if (*tempbufp == 0) {
                                    memset(clientarray[client_no].clibuf, 0, BUFSIZE);
                                    clientarray[client_no].charcount = 0;
                                    return;
                                }
                                else {
                                    sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                    clientarray[client_no].charcount = 0;
                                    parse_message(client_no);
                                }
                            }
                            else if (*tempbufp == 0) { /* message not finished - stop parsing */
                                return;
                            }
                            else { /* message malformed - send strike and resynchronize */
                                send_strike(client_no, 'm');
                                if (clientarray[client_no].used != 0) {
                                    sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                    clientarray[client_no].charcount = 0;
                                    parse_message(client_no);
                                }
                            }
                        }
                        else if (*tempbufp == 0) {
                            return;
                        }
                        else {
                            send_strike(client_no, 'm');
                            if (clientarray[client_no].used != 0) {
                                sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                clientarray[client_no].charcount = 0;
                                parse_message(client_no);
                            }
                        }
                    }
                    else if (*tempbufp == 0) {
                        return;
                    }
                    else {
                        send_strike(client_no, 'm');
                        if (clientarray[client_no].used != 0) {
                            sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                            clientarray[client_no].charcount = 0;
                            parse_message(client_no);
                        }
                    }
                }
                else if (*tempbufp == 0) {
                    return;
                }
                else {
                    send_strike(client_no, 'm');
                    if (clientarray[client_no].used != 0) {
                        sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                        clientarray[client_no].charcount = 0;
                        parse_message(client_no);
                    }
                }
            }
            else if (*tempbufp == 0) {
                return;
            }
            else {
                send_strike(client_no, 'm');
                if (clientarray[client_no].used != 0) {
                    sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                    clientarray[client_no].charcount = 0;
                    parse_message(client_no);
                }
            }
        }
        else if (*tempbufp == 's') { /* look for stat message */
            tempbufp++;
            if (*tempbufp == 't') {
                tempbufp++;
                if (*tempbufp == 'a') {
                    tempbufp++;
                    if (*tempbufp == 't') {
                        tempbufp++;
                        if (*tempbufp == ')') { /* proper cstat - respond with list of players */
                            
                            tempbufp++;
                            if (*tempbufp == 0) {
                                memset(clientarray[client_no].clibuf, 0, BUFSIZE);
                                clientarray[client_no].charcount = 0;
                                return;
                            }
                            else {
                                sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                clientarray[client_no].charcount = 0;
                                parse_message(client_no);
                            }
                        }
                        else if (*tempbufp == 0) { /* message not finished - stop parsing */
                            return;
                        }
                        else { /* message malformed - send strike and resynchronize */
                            send_strike(client_no, 'm');
                            if (clientarray[client_no].used != 0) {
                                sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                clientarray[client_no].charcount = 0;
                                parse_message(client_no);
                            }
                        }
                    }
                    else if (*tempbufp == 0) {
                        return;
                    }
                    else {
                        send_strike(client_no, 'm');
                        if (clientarray[client_no].used != 0) {
                            sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                            clientarray[client_no].charcount = 0;
                            parse_message(client_no);
                        }
                    }
                }
                else if (*tempbufp == 0) {
                    return;
                }
                else {
                    send_strike(client_no, 'm');
                    if (clientarray[client_no].used != 0) {
                        sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                        clientarray[client_no].charcount = 0;
                        parse_message(client_no);
                    }
                }
            }
            else if (*tempbufp == 0) {
                return;
            }
            else {
                send_strike(client_no, 'm');
                if (clientarray[client_no].used != 0) {
                    sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                    clientarray[client_no].charcount = 0;
                    parse_message(client_no);
                }
            }
        }
        else if (*tempbufp == 0) {
            return;
        }
        else {
            send_strike(client_no, 'm');
            if (clientarray[client_no].used != 0) {
                sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                clientarray[client_no].charcount = 0;
                parse_message(client_no);
            }
        }
    }
    else { /* resynchronizing - look for "(c" sequence */
        numchars = clientarray[client_no].charcount;
        int success = 0;
        while (*tempbufp != 0 && numchars < MAXMESSAGE) {
            if (*tempbufp == '(') {
                char *peek = tempbufp; peek++;
                if (*peek == 'c') {
                    success = 1;
                    break;
                }
            }
            tempbufp++;
            numchars++;
        }
        if (success != 0) {
            clientarray[client_no].resync = 0;
            sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
            clientarray[client_no].charcount = 0;
            parse_message(client_no);
        }
        else if (numchars > MAXMESSAGE) { /* exceeded max message length - send strike and resynchronize */
            send_strike(client_no, 'l');
            if (clientarray[client_no].used != 0) {
                sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                clientarray[client_no].charcount = 0;
                parse_message(client_no);
            }
        }
        else if (*tempbufp == 0) { /* reached end of message - clear buffer and stop parsing */
            memset(clientarray[client_no].clibuf, 0, BUFSIZE);
            clientarray[client_no].charcount += numchars;
        }
    }
}


static int find_right_paren(char **current, int *numchars)
{
    char *pos = *current; pos++;
    int chars = *numchars;
    
    while (*pos != 0 && chars < MAXMESSAGE) {
        chars++;
        if (*pos == ')') {
            break;
        }
        pos++;
    }
    *numchars = chars;
    if (*pos == 0) {
        return 0;
    }
    else if (chars >= MAXMESSAGE) {
        return -1;
    }
    else {
        *current = pos;
        return 1;
    }
}


void send_strike(int client_no, char reason)
{
    clientarray[client_no].strikes += 1;
    
    if (reason == 'm') {
        sprintf(buf, "(strike(%d)(malformed))", clientarray[client_no].strikes);
        clientarray[client_no].resync = 1;
        clientarray[client_no].charcount = 0;
    }
    else if (reason == 'b') {
        sprintf(buf, "(strike(%d)(badint))", clientarray[client_no].strikes);
        clientarray[client_no].resync = 1;
        clientarray[client_no].charcount = 0;
    }
    else if (reason == 't') {
        sprintf(buf, "(strike(%d)(timeout))", clientarray[client_no].strikes);
    }
    else if (reason == 'l') {
        sprintf(buf, "(strike(%d)(toolong))", clientarray[client_no].strikes);
        clientarray[client_no].resync = 1;
        clientarray[client_no].charcount = 0;
    }
    write_to_client(clientarray[client_no].socket, client_no, 1);
    
    if (clientarray[client_no].used != 0 && clientarray[client_no].strikes == 3) {
        closesocket(clientarray[client_no].socket);
        fprintf (stderr, "Client %d dropped - 3 strikes\n", client_no);
        FD_CLR (clientarray[client_no].socket, &total_set);
        clear_clientinfo(client_no);
    }
}


void write_to_client(int socket, int client_no, int clear)
{
	if (write(socket, &buf, strlen(buf)*sizeof(char)) < 0) {
fprintf (stderr, "Client %d dropped - Write error\n", client_no);
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
	memset(clientarray[client_no].name, 0, NAMESIZE);
	memset(clientarray[client_no].clibuf, 0, BUFSIZE);
	clientarray[client_no].charcount = 0;
	clientarray[client_no].strikes = 0;
	clientarray[client_no].resync = 0;
}


void initialize_clientinfo(int client_no)
{
	clientarray[client_no].used = 0;
	clientarray[client_no].socket = -1;
	clientarray[client_no].name = malloc(NAMESIZE*sizeof(char));
	clientarray[client_no].clibuf = malloc(BUFSIZE*sizeof(char));
	clientarray[client_no].charcount = 0;
	clientarray[client_no].strikes = 0;
	clientarray[client_no].resync = 0;
}
