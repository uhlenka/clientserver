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
char *uhlenka = "UHLENKA"; /* default client name */

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

static void parse_message();
static char * find_next_paren(char *start);
static char * find_next_comma(char *start);
static int find_next_comma_list(char **end, char *start);
static int find_player_end(char **end, char *start);

char buf[1000]; /* buffer for receiving data from the server */
char name[200] = "\0"; /* client's name */
char *server = NULL; /* server to connect to */
int port = -1; /* port to connect to */
int manual = 0; /* manual mode indicator */

int rwsocket; /* socket the client is connected to */

int main(int argc, char *argv[])
{
	struct hostent *ptrh; /* pointer to a host table entry */
	struct protoent *ptrp; /* pointer to a protocol table entry */
	struct sockaddr_in sad; /* structure to hold an IP address */
	int sd; /* socket descriptor */
	char *host; /* pointer to host name */
	int n; /* number of characters read */
	
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
        else if (strlen(name) == 0 && strcmp(argv[i], "-n") == 0 && (i+1) < argc) {
            sprintf(name, "%s", argv[i+1]);
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
    if (strlen(name) == 0) {
        sprintf(name, "%s", uhlenka);
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
	rwsocket = sd;
	FD_SET (sd, &total_set);

	/* Send cjoin. */
	sprintf(buf, "(cjoin(%s))", name);
    write(sd, &buf, strlen(buf)*sizeof(char));
    memset(buf, 0, 1000);
    
    if (manual != 0) {
    	fprintf(stderr, "Whatever text you enter will be sent to the server, as is, with no interpretation by the client.\n");
    }
	
	/* Main loop */
	while (1) {
        read_set = total_set;
        if (select (FD_SETSIZE, &read_set, NULL, NULL, NULL) < 0) {
            perror ("select");
            exit (1);
        }
        if (manual != 0 && FD_ISSET (STDIN_FILENO, &read_set)) {
            int n = read(STDIN_FILENO, buf, 1000);
            if (strncmp(buf, "\n", 1) != 0 && strncmp(buf, "\r", 1) != 0) {
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
            	parse_message();
            }
        }
        memset(buf, 0, 1000);
    }
	
	/* Terminate the client program gracefully. */
	closesocket(sd);
	exit(0);
}



static void parse_message()
{
    char * typestart; char * typeend;
    typestart = buf; typestart++;
    	
    typeend = find_next_paren(typestart); *typeend = '\0';
    	
    if (strcmp("schat", typestart) == 0) {
    	/* schat message - print "SENDER: message" */
		char * senderstart; char * senderend; char * messagestart; char * messageend;
    	senderstart = typeend; senderstart++;
    	senderend = find_next_paren(senderstart); *senderend = '\0';
    	write(1,senderstart,strlen(senderstart)); write(1,": ",2);
    	messagestart = senderend; messagestart++; messagestart++;
    	messageend = find_next_paren(messagestart); *messageend = '\0';
    	write(1,messagestart,strlen(messagestart)); write(1,"\n",1);
    	if (manual == 0) {
    		//auto respond to schat
    		if (strcmp("SERVER",senderstart) == 0) { /* SERVER message - parse and respond appropriately */
    			char * roundstart; char * roundend;
    			messageend = find_next_comma(messagestart); *messageend = '\0';
    			roundstart = messageend; roundstart++;
    			find_next_comma_list(&roundend, roundstart); *roundend = '\0';
    			if (strcmp("PLAN", messagestart) == 0) { /* PLAN message - send PASS */
    				char plan[200];
    				sprintf(plan, "(cchat(SERVER)(PLAN,%s,PASS))", roundstart);
    				write(rwsocket, &plan, strlen(plan));
    				roundend++; roundend++;
    				if (*roundend != '\0') {
    					char tempbuf[200];
    					sprintf(tempbuf, "%s", roundend);
    					sprintf(buf, "%s", tempbuf);
    					parse_message();
    				}
    			}
    			else if (strcmp("OFFER", messagestart) == 0 || strcmp("OFFERL", messagestart) == 0) { /* OFFER message - send DECLINE */
    				char *allystart; char *allyend;
    				allystart = roundend; allystart++;
    				if (*allystart != ')' && *allystart != '\0') {
    					allyend = find_next_comma(allystart); *allyend = '\0';
    					char response[200];
    					sprintf(response, "(cchat(SERVER)(DECLINE,%s,%s))", roundstart, allystart);
    					write(rwsocket, &response, strlen(response));
    					allyend = find_next_paren(allyend); allyend++; allyend++;
    					if (*allyend != '\0') {
    						char tempbuf[200];
    						sprintf(tempbuf, "%s", allyend);
    						sprintf(buf, "%s", tempbuf);
    						parse_message();
    					}	
    				}
    				if (*allystart != '\0') {
    					allystart++;
    				}
    				if (*allystart != '\0') {
    					char tempbuf[200];
    					sprintf(tempbuf, "%s", allystart);
    					sprintf(buf, "%s", tempbuf);
    					parse_message();
    				}
    			}
    			else if (strcmp("ACTION", messagestart) == 0) { /* ACTION message - ATTACK the user called BOBBY */
    				char action[200];
    				sprintf(action, "(cchat(SERVER)(ACTION,%s,ATTACK,BOBBY))", roundstart);
    				write(rwsocket, &action, strlen(action));
    				messageend++; messageend++;
    				if (*messageend != '\0') {
    					char tempbuf[200];
    					sprintf(tempbuf, "%s", messageend);
    					sprintf(buf, "%s", tempbuf);
    					parse_message();
    				}
    			}
    		}
    		//else {
    		//	if (strcmp(senderstart,name) != 0) { /* chat from another player - reply */
    		/*		char reply[200];
    				sprintf(reply, "(cchat(%s)(You sent me a chat!))", senderstart);
    				write(rwsocket, &reply, strlen(reply));
    			}
    		}*/
    	}
    }
    else if (strcmp("sjoin", typestart) == 0) {
    	/* sjoin message - print "Name: name; Players: playerlist; Minplayers: minplayers; Lobbytime: lobbytime; Timeout: timeout;" */
    	char * namestart; char * nameend; char * playerstart; char * playerend; char * numstart; char * numend;
    	write(1,"Name: ",6);
    	namestart = typeend; namestart++;
    	nameend = find_next_paren(namestart); *nameend = '\0';
    	sprintf(name, "%s", namestart);
    	write(1,namestart,strlen(namestart));
    	write(1,"; Players: ",11);
    	playerstart = nameend; playerstart++; playerstart++;
    	int result = find_next_comma_list(&playerend, playerstart); *playerend = '\0';
    	while (result > 0) {
    		write(1,playerstart,strlen(playerstart)); write(1,",",1);
    		playerstart = playerend; playerstart++;
    		result = find_next_comma_list(&playerend, playerstart); *playerend = '\0';
    	}
    	write(1,playerstart,strlen(playerstart));
    	write(1,"; Minplayers: ",14);
    	numstart = playerend; numstart++; numstart++;
    	numend = find_next_comma(numstart); *numend = '\0';
    	write(1,numstart,strlen(numstart));
    	write(1,"; Lobbytime: ",13);
    	numstart = numend; numstart++;
    	numend = find_next_comma(numstart); *numend = '\0';
    	write(1,numstart,strlen(numstart));
    	write(1,"; Timeout: ",11);
    	numstart = numend; numstart++;
    	numend = find_next_paren(numstart); *numend = '\0';
    	write(1,numstart,strlen(numstart));
    	write(1,"\n",1);
    	if (manual == 0) {
    		//auto respond to sjoin - introductory message
    		char intro[200];
    		sprintf(intro,"(cchat(ALL)(COOLTRAINER %s wants to battle!))", name);
    		write(rwsocket, &intro, strlen(intro));
    	}
    	numend++; numend++;
    	if (*numend != '\0') {
    		char tempbuf[200];
    		sprintf(tempbuf, "%s", numend);
    		sprintf(buf, "%s", tempbuf);
    		parse_message();
    	}
    }
    else if (strcmp("sstat", typestart) == 0) {
    	/* sstat message - print playerinfo \n playerinfo ..." */
    	char * playerstart; char * playerend;\
    	playerstart = typeend; playerstart++;
    	int result = find_player_end(&playerend, playerstart); *playerend = '\0';
    	while (result > 0) {
    		write(1,playerstart,strlen(playerstart)); write(1,"\n",1);
    		playerstart = playerend; playerstart++;
    		result = find_player_end(&playerend, playerstart); *playerend = '\0';
    	}
    	write(1,playerstart,strlen(playerstart)); write(1,"\n",1);
    	playerend++; playerend++;
    	if (*playerend != '\0') {
    		char tempbuf[200];
    		sprintf(tempbuf, "%s", playerend);
    		sprintf(buf, "%s", tempbuf);
    		parse_message();
    	}
    }
    else if (strcmp("strike", typestart) == 0) {
    	/* strike message - print "Strike strike# - reason" */
    	char *numstart; char *numend; char *reasonstart; char *reasonend;
    	numstart = typeend; numstart++;
    	numend = numstart; numend++; *numend = '\0';
    	reasonstart = numend; reasonstart++; reasonstart++;
    	reasonend = find_next_paren(reasonstart); *reasonend = '\0';
    	write(1,"Strike ",7);
    	write(1,numstart,1);
    	write(1," - ",3);
    	write(1,reasonstart,strlen(reasonstart));
    	write(1,"\n",1);
    	reasonend++; reasonend++;
    	if (*reasonend != '\0') {
    		char tempbuf[200];
    		sprintf(tempbuf, "%s", reasonend);
    		sprintf(buf, "%s", tempbuf);
    		parse_message();
    	}
    }
    else {
    	/* message type not recognized - print to screen as-is */
    	*typeend = '(';
    	write(1,buf,strlen(buf)); write(1,"\n",sizeof(char));
    }
}
    
    
    
static char * find_next_paren(char *start)
{
    char *pos = start;
    	
    while (*pos != '(' && *pos != ')') {
    	pos++;
    }
    return pos;
}



static char * find_next_comma(char *start)
{
    char *pos = start;
    	
    while (*pos != ',') {
    	pos++;
    }
    return pos;
}



static int find_next_comma_list(char **end, char *start)
{
    char *pos = start;
    	
    while (*pos != ',' && *pos != ')') {
    	pos++;
    }
    *end = pos;
    if (*pos == ',') {
    	return 1;
    }
    else {
    	return 0;
    }
}



static int find_player_end(char **end, char *start)
{
	int commas = 0;
	char *pos = start;
	
	while (*pos != ')') {
		if (*pos == ',') {
			commas++;
			if (commas == 3) {
				break;
			}
		}
		pos++;
	}
	*end = pos;
	if (*pos == ',') {
		return 1;
	}
	else {
		return 0;
	}
}
