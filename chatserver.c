/* chatserver.c - code for server program that allows clients to chat with one another */
#define closesocket close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>

#define PROTOPORT 36724 /* default protocol port number */
#define QLEN 30 /* size of request queue */
#define MAXCLIENTS 30 /* maximum allowable number of clients */
#define BUFSIZE 481  /* server's maximum buffer size */
#define MAXMESSAGE 480 /* length of maximum allowable message */
#define NAMESIZE 12 /* length of maximum allowable name */
#define BODYSIZE 8 /* length of maximum name body */
#define SUFFIXSIZE 3 /* length of maximum name suffix */
#define CHATSIZE 80 /* maximum chat message length */

#define CLEAR 1
#define NOCLEAR 0 /* indicators for whether a client's info should be cleared on write error */

/*------------------------------------------------------------------------
* Program: chatserver
*
* Purpose: allocate a socket and then repeatedly execute the following:
* (1) wait for input from a client or a new client connection
* (2) receive client messages or accept a new client if MAXCLIENTS is not reached
* (3) respond appropriately to any client messages
* (4) go back to step (1)
*
* Syntax: chatserver
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
		int joined;
		int sent;
		char *name;
		int socket;
		char *clibuf;
		int charcount;
		int strikes;
		int resync;
	} clientinfo;
clientinfo clientarray[MAXCLIENTS]; /* structure to hold client info */
int numplayers = 0; /* total number of players that have joined */
fd_set total_set, read_set; /* fd_sets to use with select */
char buf[BUFSIZE]; /* buffer for sending and receiving messages */
int minplayers = 2; /* minimum number of players needed to start a game */
int lobbytime = 10; /* number of seconds until game begins if numplayers >= minplayers */
int timeout = 10; /* number of seconds a player has to make a move */
char listbuf[MAXMESSAGE]; /* buffer for building player list */

	
/* helper functions */
static void initialize_clientinfo(int client_no);
static void clear_clientinfo(int client_no);
static void write_to_client(int socket, int client_no, int clear);
static void read_from_client(int socket, int client_no);
static void parse_message(int client_no);
static void send_chat(char **message, char **recipients, int client_no);
static int  find_name_end(char **current);
static void convert_name(char **name);
static void assign_name(char **name, int client_no);
static void build_player_list();
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
	
	srand(time(NULL));
	
	memset(buf, '\0', BUFSIZE); /* clear read/write buffer */
	memset(listbuf, '\0', MAXMESSAGE); /* clear player list buffer */
	memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */
	sad.sin_family = AF_INET; /* set family to Internet */
	sad.sin_addr.s_addr = INADDR_ANY; /* set the local IP address */
	
	port = PROTOPORT; /* use default port number */
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
	
	/* Eliminate "Address already in use" error message. */
	int flag = 1;
	if (setsockopt(listensocket,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(int)) == -1) { 
    	perror("setsockopt"); 
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
					if (client_no < MAXCLIENTS) { /* add new connection to clientarray */
fprintf (stderr, "Accepted: Client %d\n", client_no);
						FD_SET (tempsd, &total_set);
						clientarray[client_no].used = 1;
						clientarray[client_no].socket = tempsd;
					}
					else { /* send no vacancy message and drop connection */
fprintf (stderr, "Refused: Client %d\n", client_no);
						sprintf(buf, "(snovac)");
						write_to_client(tempsd, client_no, NOCLEAR);
						closesocket(tempsd);
					}
				}
				else {
					/* data available on already-connected socket */
					int nbytes = recv (i, buf, BUFSIZE, MSG_DONTWAIT);
					for (client_no=0; client_no<MAXCLIENTS; client_no++) {
						if (clientarray[client_no].socket == i)
						break;
					}
					if (nbytes < 0) {
fprintf (stderr, "Error: recv on client %d\n", client_no);
					}
					else if (nbytes == 0) { /* client has died - drop its connection and clear its info */
						closesocket(i);
fprintf (stderr, "Dropped: Client %d - died\n", client_no);
						if (clientarray[client_no].joined != 0) {
							numplayers--;
							clientarray[client_no].joined = 0;
							build_player_list();
							int i;
							for (i=0; i<MAXCLIENTS; i++) {
								if (clientarray[i].joined != 0 && i != client_no) {
									sprintf(buf, "(sstat(%s))", listbuf);
									write_to_client(clientarray[i].socket, i, CLEAR);
								}
							}
							memset(listbuf, '\0', MAXMESSAGE);
						}
						FD_CLR (i, &total_set);
						clear_clientinfo(client_no);
					}
					else { /* transfer data to client's buffer and attempt to parse message */
						read_from_client(i, client_no);
                        memset(buf, '\0', BUFSIZE);
						parse_message(client_no);
						memset(buf, '\0', BUFSIZE);
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
	while (*tempbufp != '\0' && numchars < BUFSIZE) {
		if (isprint(*tempbufp) != 0) {
			*tempend = *tempbufp;
			tempend++;
		}
		tempbufp++;
		numchars++;
	}
	*tempend = '\0';
	strncat(clientarray[client_no].clibuf, tempstart, BUFSIZE-clientarray[client_no].charcount);
    clientarray[client_no].charcount = numchars;
	free(tempstart);
}






static void parse_message(int client_no)
{
    char *tempbufp = clientarray[client_no].clibuf;
    int numchars;
    
fprintf (stderr, "Message: '%s' from client %d\n", clientarray[client_no].clibuf, client_no);
    
    if (clientarray[client_no].resync == 0) { /* not resychronizing - parse normally */
        numchars = 0;
        if (*tempbufp != '(') {
            if (*tempbufp == '\0') {
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
            if (*tempbufp == '\0') {
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
                            else if (result == -1) { /* max message length exceeded - send strike and resynchronize */
                                send_strike(client_no, 'l');
                                if (clientarray[client_no].used != 0) {
                                	clientarray[client_no].resync = 1;
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
                                else if (result == -1) { /* max message length exceeded - send strike and resynchronize */
                                    send_strike(client_no, 'l');
                                    if (clientarray[client_no].used != 0) {
                                    	clientarray[client_no].resync = 1;
                                        sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                        clientarray[client_no].charcount = 0;
                                        parse_message(client_no);
                                    }
                                }
                                numchars++;
                                tempbufp++;
                                if (*tempbufp == ')') { /* proper cchat - truncate message if necessary and send to recipients */
fprintf (stderr, "Cchat: client %d\n", client_no);
									if (clientarray[client_no].joined != 0) {
										send_chat(&message, &recipients, client_no);
									}
									else {
										send_strike(client_no, 'm');
									}
                                    tempbufp++;
                                    if (*tempbufp == '\0') {
                                        memset(clientarray[client_no].clibuf, '\0', BUFSIZE);
                                        clientarray[client_no].charcount = 0;
                                        return;
                                    }
                                    else {
                                        sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                        clientarray[client_no].charcount = 0;
                                        parse_message(client_no);
                                    }
                                }
                                else if (*tempbufp == '\0') { /* message not finished - stop parsing */
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
                            else if (*tempbufp == '\0') {
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
                        else if (*tempbufp == '\0') {
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
                    else if (*tempbufp == '\0') {
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
                else if (*tempbufp == '\0') {
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
            else if (*tempbufp == '\0') {
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
                            else if (result == -1) { /* max message length exceeded - send strike and resynchronize */
                                send_strike(client_no, 'l');
                                if (clientarray[client_no].used != 0) {
                                	clientarray[client_no].resync = 1;
                                    sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                    clientarray[client_no].charcount = 0;
                                    parse_message(client_no);
                                }
                            }
                            numchars++;
                            tempbufp++;
                            if (*tempbufp == ')') { /* proper cjoin - apply naming algorithm if necessary and assign name */
fprintf (stderr, "Cjoin: client %d - ", client_no);
                            	if (clientarray[client_no].joined == 0) {
fprintf (stderr, "new player\n");
                                	assign_name(&name, client_no);
                                }
                                else {
fprintf (stderr, "already joined\n");
									send_strike(client_no, 'm');
                                }
fprintf (stderr, "Name: client %d: %s\n", client_no, clientarray[client_no].name);
                            	tempbufp++;
                            	if (*tempbufp == '\0') {
                                	memset(clientarray[client_no].clibuf, '\0', BUFSIZE);
                                	clientarray[client_no].charcount = 0;
                                	return;
                            	}
                            	else {
                                	sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                	clientarray[client_no].charcount = 0;
                                	parse_message(client_no);
                            	}
                        	}
                        	else if (*tempbufp == '\0') { /* message not finished - stop parsing */
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
                    	else if (*tempbufp == '\0') {
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
                	else if (*tempbufp == '\0') {
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
            	else if (*tempbufp == '\0') {
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
        	else if (*tempbufp == '\0') {
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
fprintf (stderr, "Cstat: client %d\n", client_no);
							if (clientarray[client_no].joined != 0) {
                            	build_player_list();
                            	sprintf(buf, "(sstat(%s))", listbuf);
                            	memset(listbuf, '\0', MAXMESSAGE);
                            	write_to_client(clientarray[client_no].socket, client_no, CLEAR);
                            }
                            else {
								send_strike(client_no, 'm');
							}
                            tempbufp++;
                            if (*tempbufp == '\0') {
                                memset(clientarray[client_no].clibuf, '\0', BUFSIZE);
                                clientarray[client_no].charcount = 0;
                                return;
                            }
                            else {
                                sprintf(clientarray[client_no].clibuf, "%s", tempbufp);
                                clientarray[client_no].charcount = 0;
                                parse_message(client_no);
                            }
                        }
                        else if (*tempbufp == '\0') { /* message not finished - stop parsing */
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
                    else if (*tempbufp == '\0') {
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
                else if (*tempbufp == '\0') {
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
            else if (*tempbufp == '\0') {
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
    	else if (*tempbufp == '\0') {
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
        while (*tempbufp != '\0' && numchars < MAXMESSAGE) {
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
        else if (*tempbufp == '\0') { /* reached end of message - clear buffer and stop parsing */
            memset(clientarray[client_no].clibuf, '\0', BUFSIZE);
            clientarray[client_no].charcount += numchars;
        }
    }
}






static void send_chat(char **message, char **recipients, int client_no)
{
	int zero = 0;
	char *messageend = *message;
	find_right_paren(&messageend, &zero);
	*messageend = '\0';
	
	/* Truncate chat message. */
	char short_message[CHATSIZE+1];
	snprintf(short_message, CHATSIZE+1, "%s", *message);
	
	/* Strip any illegal characters from chat message. */
	char *original = short_message;
	char *stripped = original;
	while (*original != '\0') {
		if (*original != '(') {
			*stripped = *original;
			stripped++;
		}
		original++;
	}
	*stripped = '\0';
	
	char *namestart = *recipients;
	char *nameend = namestart;
	int result = find_name_end(&nameend);
	*nameend = '\0';
	
	char convertedname[NAMESIZE+1];
	char *cnameptr = convertedname;
	sprintf(convertedname, "%s", namestart);
	convert_name(&cnameptr);

	int i;

	/* Check for "ANY" or "ALL" recipient. */
	if (result == 0) {
		if (strcasecmp("ANY", namestart) == 0) {
			if (numplayers > 1) {
				if (numplayers == 2) {
					for (i=0; i<MAXCLIENTS; i++) {
						if (i != client_no && clientarray[i].joined != 0) {
							sprintf(buf, "(schat(%s)(%s))", clientarray[client_no].name, short_message);
							write_to_client(clientarray[i].socket, i, CLEAR);
						}
					}
				}
				else {
					int numhops = (rand() % (numplayers-1)) + 1;
					int i = client_no;
					while(numhops > 0) {
						i = (i+1) % MAXCLIENTS;
						if (clientarray[i].joined != 0) {
							numhops--;
						}
					}
					sprintf(buf, "(schat(%s)(%s))", clientarray[client_no].name, short_message);
					write_to_client(clientarray[i].socket, i, CLEAR);
				}
			}
			return;
		}
		else if (strcasecmp("ALL", namestart) == 0) {
			for (i=0; i<MAXCLIENTS; i++) {
				if (clientarray[i].joined != 0) {
					sprintf(buf, "(schat(%s)(%s))", clientarray[client_no].name, short_message);
					write_to_client(clientarray[i].socket, i, CLEAR);
				}
			}
			return;
		}
	}
	
	/* Send message to all valid recipients. */
	int namefound, strikesent;
	while (result != 0) {
		namefound = 0;
		for (i=0; i<MAXCLIENTS; i++) {
			if (strcmp(clientarray[i].name, cnameptr) == 0) {
				namefound = 1;
				if (clientarray[i].sent == 0) {
					sprintf(buf, "(schat(%s)(%s))", clientarray[client_no].name, short_message);
					write_to_client(clientarray[i].socket, i, CLEAR);
					clientarray[i].sent = 1;
				}
				else if (strikesent == 0) {
					send_strike(client_no, 'm');
					strikesent = 1;
				}
			}
		}
		if (namefound == 0 && strikesent == 0) {
			send_strike(client_no, 'm');
			strikesent = 1;
		}
		nameend++;
		namestart = nameend;
		result = find_name_end(&nameend);
		*nameend = '\0';
		memset(convertedname, '\0', NAMESIZE+1);
		sprintf(convertedname, "%s", namestart);
		convert_name(&cnameptr);
	}
	namefound = 0;
	for (i=0; i<MAXCLIENTS; i++) {
		if (strcmp(clientarray[i].name, cnameptr) == 0) {
			namefound = 1;
			if (clientarray[i].sent == 0) {
				sprintf(buf, "(schat(%s)(%s))", clientarray[client_no].name, short_message);
				write_to_client(clientarray[i].socket, i, CLEAR);
				clientarray[i].sent = 1;
			}
			else if (strikesent == 0) {
				send_strike(client_no, 'm');
				strikesent = 1;
			}
		}
	}
	if (namefound == 0 && strikesent == 0) {
		send_strike(client_no, 'm');
		strikesent = 1;
	}
	
	/* Reset 'sent' flag for all players. */
	for (i=0; i<MAXCLIENTS; i++) {
		clientarray[i].sent = 0;
	}
}





static int find_name_end(char **current)
{
    char *pos = *current;
    
    while (*pos != ')' && *pos != ',') {
        pos++;
    }
    *current = pos;
    if (*pos == ')') {
        return 0;
    }
    else {
        return 1;
    }
}




static void convert_name(char **name)
{
	char *namepos = *name;
	char temp[480];
	char *temppos = temp;
	
	/* Copy name into temp string. */
	while (*namepos != ')') {
		if (*namepos != ' ') {
			*temppos = *namepos;
			temppos++;
		}
		namepos++;
	}
	*temppos = '\0';

	/* Remove any illegal characters (non-alphanumeric/dot). */
	temppos = temp;
	char *copy = temppos;
	while (*temppos != '\0') {
		if (isalnum(*temppos) != 0 || *temppos == '.') {
			*copy = *temppos;
			copy++;
		}
		temppos++;
	}
	*copy = '\0';
	
	/* Remove leading and trailing periods. */
	temppos = temp;
	copy = temppos;
	int removing = 1;
	while (*temppos != '\0') {
		if (*temppos != '.') {
			*copy = *temppos;
			copy++;
			removing = 0;
		}
		else if (removing == 0) {
			*copy = *temppos;
			copy++;
		}
		temppos++;
	}
	*copy = '\0';
	copy--;
	while (copy != temp) {
		if (*copy != '.') {
			break;
		}
		copy--;
	}
	copy++;
	*copy = '\0';
	
	/* Remove all but last dot. */
	int num_dots = 0;
	temppos = temp;
	while (*temppos != '\0') {
		if (*temppos == '.') {
			num_dots++;
		}
		temppos++;
	}
	char *last_dot;
	if (num_dots > 0) {
		char extension[480];
		while (*copy != '.') {
			copy--;
		}
		last_dot = copy;
		sprintf(extension, "%s", last_dot);
		temppos = temp;
		copy = temppos;
		while (temppos != last_dot) {
			if (*temppos != '.') {
				*copy = *temppos;
				copy++;
			}
			temppos++;
		}
		*copy = '\0';
		strcat(temp, extension);
		for (last_dot = temp; *last_dot != '.'; last_dot++);
	}
	
	/* Convert name to uppercase. */
	temppos = temp;
	while (*temppos != '\0') {
		*temppos = toupper(*temppos);
		++temppos;
	}
	
	/* Truncate name before dot to 8 characters, and name after dot to 3 characters. */
	char short_name[13];
	if (num_dots > 0) {
		*last_dot = '\0';
		snprintf(short_name, BODYSIZE+1, "%s", temp);
		*last_dot = '.';
		strncat(short_name, last_dot, SUFFIXSIZE+1);
		sprintf(temp, "%s", short_name);
		last_dot = temp;
		while (*last_dot != '.') {
			last_dot++;
		}
	}
	else {
		snprintf(short_name, BODYSIZE+1, "%s", temp);
		sprintf(temp, "%s", short_name);
	}
	
	/* Place converted name into destination string. */
	sprintf(*name, "%s", temp);
}






static void assign_name(char **name, int client_no)
{
	char temp[480];
	char *temppos = temp;
	sprintf(temp, "%s", *name);
	convert_name(&temppos);
	
	/* Send strike if name is empty. */
	if (strlen(temp) == 0) { /* zero length name - send strike */
		send_strike(client_no, 'm');
		return;
	}
	
	/* Send strike if name is a reserved word. */
	if (strcmp("ALL", temp) == 0 || strcmp("ANY", temp) == 0) {
		send_strike(client_no, 'm');
		return;
	}

	/* Check for matches. */
	int i, j, match = 0;
	for (i=0; i<MAXCLIENTS; i++) {
		if (strcmp(clientarray[i].name, temp) == 0) {
			match = 1;
			break;
		}
	}
	if (match == 0) { /* no matches - assign name */
		sprintf(clientarray[client_no].name, "%s", temp);
	}
	else { /* match found - assign first unmatched alternative */
		char tentative[NAMESIZE+1];
		char number[5];
		int offset = 1;
		int num_dots = 0;
		while (*temppos != '\0') {
			if (*temppos == '.') {
				num_dots = 1;
				break;
			}
			temppos++;
		}
		for (j=1; j<31; j++) {
			memset(tentative, '\0', NAMESIZE+1);
			memset(number, '\0', 5);
			if (j < 10) {
				snprintf(tentative, BODYSIZE-1, "%s", temp);
			}
			else if (j < 100) {
				snprintf(tentative, BODYSIZE-2, "%s", temp);
			}
			else {
				snprintf(tentative, BODYSIZE-3, "%s", temp);
			}
			sprintf(number, "~%d", j);
			strcat(tentative, number);
			if (num_dots > 0) {
				strcat(tentative, temppos);
			}
			for (i=0; i<MAXCLIENTS; i++) {
				match = 0;
				if (strcmp(clientarray[i].name, tentative) == 0) {
					match = 1;
					offset++;
					break;
				}
			}
			if (match == 0) {
				break;
			}
		}
		sprintf(clientarray[client_no].name, "%s", tentative);
	}

	/* update player information, send sjoin to new player and sstat to all others */
	clientarray[client_no].joined = 1;
	numplayers++;
	build_player_list();
	sprintf(buf, "(sjoin(%s)(%s)(%d,%d,%d))", clientarray[client_no].name, listbuf, minplayers, lobbytime, timeout);
	write_to_client(clientarray[client_no].socket, client_no, CLEAR);
	for (i=0; i<MAXCLIENTS; i++) {
		if (clientarray[i].joined == 1 && i != client_no) {
			sprintf(buf, "(sstat(%s))", listbuf);
			write_to_client(clientarray[i].socket, i, CLEAR);
		}
	}
	memset(listbuf, '\0', MAXMESSAGE);
}






static void build_player_list()
{
	int added = 0;
	int i;
	for (i=0; i<MAXCLIENTS; i++) {
		if (clientarray[i].joined != 0) {
			if (added == 0) {
				sprintf(listbuf, "%s", clientarray[i].name);
			}
			else {
				strcat(listbuf, ",");
				strcat(listbuf, clientarray[i].name);
			}
			added++;
		}
	}
	if (added == numplayers) {
	}
	else {
fprintf(stderr, "Error: player list does not agree with numplayers\n");
	}
}






static int find_right_paren(char **current, int *numchars)
{
    char *pos = *current; pos++;
    int chars = *numchars;
    
    while (*pos != '\0' && chars < MAXMESSAGE) {
        chars++;
        if (*pos == ')') {
            break;
        }
        pos++;
    }
    *numchars = chars;
    if (*pos == '\0') {
    	*current = pos;
        return 0;
    }
    else if (chars >= MAXMESSAGE) {
    	*current = pos;
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
    
    if (reason == 'm') { /* send 'malformed' strike */
        sprintf(buf, "(strike(%d)(malformed))", clientarray[client_no].strikes);
        clientarray[client_no].resync = 1;
        clientarray[client_no].charcount = 0;
    }
    else if (reason == 'b') { /* send 'badint' strike */
        sprintf(buf, "(strike(%d)(badint))", clientarray[client_no].strikes);
        clientarray[client_no].resync = 1;
        clientarray[client_no].charcount = 0;
    }
    else if (reason == 't') { /* send 'timeout' strike */
        sprintf(buf, "(strike(%d)(timeout))", clientarray[client_no].strikes);
    }
    else if (reason == 'l') { /* send 'toolong' strike */
        sprintf(buf, "(strike(%d)(toolong))", clientarray[client_no].strikes);
        clientarray[client_no].resync = 1;
        clientarray[client_no].charcount = 0;
    }
    write_to_client(clientarray[client_no].socket, client_no, CLEAR);
fprintf(stderr, "Strike: %d to client %d\n", clientarray[client_no].strikes, client_no);
    
    if (clientarray[client_no].used != 0 && clientarray[client_no].strikes == 3) { /* 3rd strike - drop client connection */
        closesocket(clientarray[client_no].socket);
fprintf (stderr, "Dropped: Client %d - 3 strikes\n", client_no);
        if (clientarray[client_no].joined != 0) { /* client had a name - send sstat to all players */
				numplayers--;
				clientarray[client_no].joined = 0;
				build_player_list();
				int i;
				for (i=0; i<MAXCLIENTS; i++) {
					if (clientarray[i].joined != 0 && i != client_no) {
						sprintf(buf, "(sstat(%s))", listbuf);
						write_to_client(clientarray[i].socket, i, CLEAR);
					}
				}
				memset(listbuf, '\0', MAXMESSAGE); 
		}
        FD_CLR (clientarray[client_no].socket, &total_set);
        clear_clientinfo(client_no);
    }
}






void write_to_client(int socket, int client_no, int clear)
{
	if (write(socket, &buf, strlen(buf)*sizeof(char)) < 0) {
fprintf (stderr, "Dropped: Client %d - Write error\n", client_no);
		if (clear == CLEAR) {
			if (clientarray[client_no].joined != 0) {
				numplayers--;
				clientarray[client_no].joined = 0;
				build_player_list();
				int i;
				for (i=0; i<MAXCLIENTS; i++) {
					if (clientarray[i].joined != 0 && i != client_no) {
						sprintf(buf, "(sstat(%s))", listbuf);
						write_to_client(clientarray[i].socket, i, CLEAR);
					}
				}
				memset(listbuf, '\0', MAXMESSAGE);
			}
			FD_CLR (socket, &total_set);
			clear_clientinfo(client_no);
		}
	}
	memset(buf, '\0', BUFSIZE);
}






void clear_clientinfo(int client_no)
{
	clientarray[client_no].used = 0;
	clientarray[client_no].joined = 0;
	clientarray[client_no].sent = 0;
	clientarray[client_no].socket = -1;
	memset(clientarray[client_no].name, '\0', NAMESIZE+1);
	memset(clientarray[client_no].clibuf, '\0', BUFSIZE);
	clientarray[client_no].charcount = 0;
	clientarray[client_no].strikes = 0;
	clientarray[client_no].resync = 0;
}






void initialize_clientinfo(int client_no)
{
	clientarray[client_no].used = 0;
	clientarray[client_no].joined = 0;
	clientarray[client_no].sent = 0;
	clientarray[client_no].socket = -1;
	clientarray[client_no].name = malloc((NAMESIZE+1)*sizeof(char));
	clientarray[client_no].clibuf = malloc(BUFSIZE*sizeof(char));
	clientarray[client_no].charcount = 0;
	clientarray[client_no].strikes = 0;
	clientarray[client_no].resync = 0;
}
