/* byzantiums.c - code for server program that allows clients to chat and play Byzantium with one another */
#define closesocket close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
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
#define BUFSIZE 610  /* server's maximum buffer size */
#define MAXMESSAGE 480 /* length of maximum allowable message */
#define NAMESIZE 12 /* length of maximum allowable name */
#define BODYSIZE 8 /* length of maximum name body */
#define SUFFIXSIZE 3 /* length of maximum name suffix */
#define CHATSIZE 80 /* maximum chat message length */

#define CLEAR 1
#define NOCLEAR 0 /* indicators for whether a client's info should be cleared on write error */

/*------------------------------------------------------------------------
* Program: byzantiums
*
* Purpose: allocate a socket and then repeatedly execute the following:
* (1) wait for input from a client or a new client connection
* (2) receive client messages or accept a new client if MAXCLIENTS is not reached
* (3) respond appropriately to any client messages
* (4) implement the game
* (4) go back to step (1)
*
* Syntax: byzantiums [-m minplayers] [-l lobbytime] [-t timeout] [-f forcesize]
*
* minplayers    minimum number of players needed to start a game
* lobbytime     number of seconds until game begins if numusers >= minplayers
* timeout       number of seconds a player has to make a move
* forcesize 	number of troops each player starts with
*
* All arguments are optional. The default values are as follows:
* 	minplayers = 3
* 	lobbytime = 10
* 	timeout = 30
*   forcesize = 1000
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
        int playing;
        int fighting;
		int sent;
        int offersent;
		char *name;
		int socket;
		char *clibuf;
		int charcount;
		int strikes;
		int resync;
        int troops;
		int plangiven;
		int offers;
	} clientinfo;
clientinfo clientarray[MAXCLIENTS]; /* structure to hold client info */
int numusers = 0; /* total number of users that have joined */
fd_set total_set, read_set; /* fd_sets to use with select */
char buf[BUFSIZE]; /* buffer for sending and receiving messages */
int minplayers = 3; /* minimum number of players needed to start a game - default 3 */
int lobbytime = 10; /* number of seconds until game begins if numusers >= minplayers - default 10 */
int timeout = 30; /* number of seconds a player has to make a move - default 30 */
int startingforce = 1000; /* number of troops each player starts with - default 1000 */
char listbuf[BUFSIZE]; /* buffer for building user list */

typedef struct {
        int used;
        int target;
    } offerinfo;
offerinfo offergrid[MAXCLIENTS][MAXCLIENTS] = {{{0}}}; /* 2-d array for keeping track of offer info */
int attackgrid[MAXCLIENTS][MAXCLIENTS] = {{0}}; /* 2-d array for keeping track of attack info */
int battlegrid[MAXCLIENTS][MAXCLIENTS] = {{0}}; /* 2-d array for keeping track of battle info */
typedef struct {
        int count;
        int first;
        int second;
        int third;
    } die;
die a = {0}; die b = {0}; /* variables for keeping track of dice rolls during battles */
int roundnum = 1; int phase = 0; /* variables for keeping track of where we are in the game */
int waiting = 0; int waitingfor = -1; int responseto = -1; /* variables for keeping track of what message the server is waiting for */
time_t timestart; /* struct for implementing timeouts */
int timerset = 0; /* variable for keeping track of whether the timer has been set */ 


/* helper functions */
static void initialize_clientinfo(int client_no);
static void clear_clientinfo(int client_no);
static void zero_grids();
static void write_to_client(int socket, int client_no, int clear);
static void read_from_client(int socket, int client_no);
static void parse_message(int client_no);
static void send_chat(char **message, char **recipients, int client_no);
static int  find_name_end(char **current);
static void convert_name(char **name);
static void assign_name(char **name, int client_no);
static void build_user_list_names();
static void build_user_list();
static int  find_right_paren(char **current, int *numchars);
static void send_strike(int client_no, char reason);
static void send_notifies();
static void do_battle();
static void sort_rolls();





/* Main */
int main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);

	//struct hostent *ptrh; /* pointer to a host table entry */
	struct protoent *ptrp; /* pointer to a protocol table entry */
	struct sockaddr_in sad; /* structure to hold server's address */
	struct sockaddr_in cad; /* structure to hold client address */
	struct timeval selecttime; /* structure to hold timeout info for select */
	int listensocket, tempsd; /* socket descriptors for listen port and acceptance */
	int port; /* protocol port number */
	int alen; /* length of address */
	
	selecttime.tv_sec = 0; selecttime.tv_usec = 0; /*initialize timeval struct */
	FD_ZERO (&total_set); /* initialize fd_set */
	int i;
	for (i=0;i<30;i++) { /* initialize client info structure */
		initialize_clientinfo(i);
	}
    
    /* Get values from command line. */
    for (i=1;i<argc;i++) {
        if (strcmp(argv[i], "-m") == 0 && (i+1) < argc) {
            sscanf(argv[i+1], "%d", &minplayers);
        }
        else if (strcmp(argv[i], "-l") == 0 && (i+1) < argc) {
            sscanf(argv[i+1], "%d", &lobbytime);
        }
        else if (strcmp(argv[i], "-t") == 0 && (i+1) < argc) {
            sscanf(argv[i+1], "%d", &timeout);
        }
        else if (strcmp(argv[i], "-f") == 0) {
            sscanf(argv[i+1], "%d", &startingforce);
        }
    }
    if (minplayers < 0) {
        minplayers = 3;
    }
    if (lobbytime < 0) {
        lobbytime = 10;
    }
    if (timeout < 0) {
        timeout = 30;
    }
    if (startingforce < 0) {
        startingforce = 1000;
    }
	
	srand(time(NULL));
	
	memset(buf, '\0', BUFSIZE); /* clear read/write buffer */
	memset(listbuf, '\0', MAXMESSAGE); /* clear user list buffer */
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
		if (select (FD_SETSIZE, &read_set, NULL, NULL, &selecttime) < 0) {
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
						if (clientarray[client_no].joined != 0) { /* client had joined - send sstat to all users */
							numusers--;
							clientarray[client_no].joined = 0;
							build_user_list();
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
        /* Game logic */
        if (phase == 0) { /* we are in the lobby */
            if (timerset != 0) { /* check if timer has been set */
            	if (numusers >= minplayers) { /* check if minplayers has been met */
                	if (difftime(time(NULL), timestart) >= (double)lobbytime) { /* check if timer has expired */
                    	/* Minplayers has been met and lobbytime has expired - enter phase 1. */
                    	for (i=0; i<MAXCLIENTS; i++) {
                       		if (clientarray[i].joined != 0) {
                            	clientarray[i].playing = 1;
                            	clientarray[i].troops = startingforce;
                        	}
                    	}
                    	timerset = 0;
                    	phase = 1;
                    	waitingfor = -1;
                    	fprintf(stderr, "-------- Phase 1: entering phase 1 --------\n");
                	}
                }
                else {
                	timerset = 0;
                }
            }
            else {
            	if (numusers >= minplayers) { /* check if minplayers has been met */
                	/* Minplayers has been met and lobbytime has not been started - start timer. */
                	fprintf(stderr, "-------- Phase 0: starting countdown --------\n");
                	time(&timestart);
                	timerset = 1;
                }
            }
        }
        else if (phase == 1) { /* we are in the planning phase */
            if (waitingfor < 0) {
                waitingfor = 0;
            }
            if (waitingfor < MAXCLIENTS) {
                if (clientarray[waitingfor].playing > 0) {
                    if (timerset == 0) {
                        /* Send PLAN message to waitingfor and start timer. */
                        fprintf(stderr, "Sending to %s\n", clientarray[waitingfor].name);
                        sprintf(buf, "(schat(SERVER)(PLAN,%d))", roundnum);
                        write_to_client(clientarray[waitingfor].socket, waitingfor, CLEAR);
                        time(&timestart);
                        timerset = 1;
                    }
                    else { /* check if timer has expired */
                        if (difftime(time(NULL), timestart) >= (double)timeout) {
                            /* waitingfor has timed out - send strike and move on to next player. */
                            send_strike(waitingfor, 't');
                            fprintf(stderr, "%s timed out\n", clientarray[waitingfor].name);
                            waitingfor++;
                            timerset = 0;
                        }
                    }
                }
                else { /* waitingfor is not playing - move to next client */
                    waitingfor++;
                }
            }
            else {
                /* Phase 1 finished - enter phase 2. */
                fprintf(stderr, "-------- Phase 2: entering phase 2 --------\n");
                waitingfor = -1;
                phase = 2;
            }
        }
        else if (phase == 2) { /* we are in the offer/response phase */
            if (waitingfor < 0) {
                waitingfor = 0;
            }
            if (responseto < 0) {
                responseto = 0;
            }
            if (waitingfor < MAXCLIENTS) {
                if (responseto < MAXCLIENTS) {
                    if (clientarray[waitingfor].playing > 0) { /* check if waitingfor is playing */
                        if (offergrid[waitingfor][responseto].used != 0) { /* check if waitingfor has an offer from responseto */
                            if (timerset == 0) {
                                /* Send OFFER message to waitingfor, decrement waitingfor's offers, and start timer. */
                                if (clientarray[waitingfor].offers > 1) {
                                    //send offer with OFFER message
                                    fprintf(stderr, "Sending %s's offer to %s\n", clientarray[responseto].name, clientarray[waitingfor].name);
                                    clientarray[waitingfor].offersent = 1;
                                    int target = offergrid[waitingfor][responseto].target;
                                    sprintf(buf, "(schat(SERVER)(OFFER,%d,%s,%s))", roundnum, clientarray[responseto].name, clientarray[target].name);
                                    write_to_client(clientarray[waitingfor].socket, waitingfor, CLEAR);
                                    clientarray[waitingfor].offers -= 1;
                                }
                                else if (clientarray[waitingfor].offers == 1) {
                                    //send last offer with OFFERL message
                                    fprintf(stderr, "Sending %s's offer to %s\n", clientarray[responseto].name, clientarray[waitingfor].name);
                                    clientarray[waitingfor].offersent = 1;
                                    int target = offergrid[waitingfor][responseto].target;
                                    sprintf(buf, "(schat(SERVER)(OFFERL,%d,%s,%s))", roundnum, clientarray[responseto].name, clientarray[target].name);
                                    write_to_client(clientarray[waitingfor].socket, waitingfor, CLEAR);
                                    clientarray[waitingfor].offers -= 1;
                                }
                                time(&timestart);
                                timerset = 1;
                            }
                            else { /* check if timer has expired */
                                if (difftime(time(NULL), timestart) >= (double)timeout) {
                                    /* waitingfor has timed out - send strike and move on to next offer. */
                                    send_strike(waitingfor, 't');
                                    fprintf(stderr, "%s has timed out on %s\n", clientarray[waitingfor].name, clientarray[responseto].name);
                                    responseto++;
                                    timerset = 0;
                                }
                            }
                        }
                        else if (clientarray[waitingfor].offers == 0 && clientarray[waitingfor].offersent == 0) { /* no offers for waitingfor - send empty OFFERL message and move to next client */
                        	fprintf(stderr, "Sending empty message to %s\n", clientarray[waitingfor].name);
                            sprintf(buf, "(schat(SERVER)(OFFERL,%d))", roundnum);
                            write_to_client(clientarray[waitingfor].socket, waitingfor, CLEAR);
                            waitingfor++;
                        }
                        else { /* no offer from responseto - move to next offer */
                            responseto++;
                        }
                    }
                    else { /* waitingfor is not playing - move to next client */
                        waitingfor++;
                    }
                }
                else { /* done with offers for waitingfor - reset offersent and responseto and move to next client */
                    clientarray[waitingfor].offersent = 0;
                    waitingfor++;
                    responseto = 0;
                }
            }
            else {
                /* Phase 2 finished - enter phase 3. */
                fprintf(stderr, "-------- Phase 3: entering phase 3 --------\n");
                waitingfor = -1;
                phase = 3;
            }
        }
        else if (phase == 3) { /* we are in the action phase */
            if (waitingfor < 0) {
                waitingfor = 0;
            }
            if (waitingfor < MAXCLIENTS) {
                if (clientarray[waitingfor].playing > 0) {
                    if (timerset == 0) {
                        /* Send ACTION message to waitingfor and start timer. */
                        fprintf(stderr, "Sending to %s\n", clientarray[waitingfor].name);
                        sprintf(buf, "(schat(SERVER)(ACTION,%d))", roundnum);
                        write_to_client(clientarray[waitingfor].socket, waitingfor, CLEAR);
                        time(&timestart);
                        timerset = 1;
                    }
                    else { /* check if timer has expired */
                        if (difftime(time(NULL), timestart) >= (double)timeout) {
                            /* waitingfor has timed out - send strike and move on to next player. */
                            send_strike(waitingfor, 't');
                            fprintf(stderr, "%s has timed out\n", clientarray[waitingfor].name);
                            waitingfor++;
                            timerset = 0;
                        }
                    }
                }
                else { /* waitingfor is not playing - move to next client */
                    waitingfor++;
                }
            }
            else {
                /* Phase 3 messages finished - enter battle. */
                fprintf(stderr, "-------- Phase 3: entering battle --------\n");
                send_notifies();
                do_battle();
                build_user_list();
                memset(buf, '\0', BUFSIZE);
                for (i=0; i<MAXCLIENTS; i++) { /* send sstat to all users */
                    if (clientarray[i].joined != 0) {
                        sprintf(buf, "(sstat(%s))", listbuf);
                        write_to_client(clientarray[i].socket, i, CLEAR);
                    }
                }
                memset(listbuf, '\0', BUFSIZE);
                zero_grids(); /* zero out offergrid and attackgrid */
                int numplayers = 0;
                for (i=0; i<MAXCLIENTS; i++) {
                    if (clientarray[i].playing > 0) {
                        numplayers++;
                    }
                }
                if (numplayers > 1) { /* game is not over - increment roundnum, add any newly joined users, and enter phase 1 */
                    roundnum++;
                    if (roundnum > 99999) {
                        roundnum = 1;
                    }
                    for (i=0; i<MAXCLIENTS; i++) {
                        if (clientarray[i].joined != 0 && clientarray[i].playing == 0) {
                            clientarray[i].playing = 1;
                            clientarray[i].troops = startingforce;
                        }
                    }
                    waitingfor = -1;
                    phase = 1;
                    fprintf(stderr, "-------- Phase 1: entering phase 1 --------\n");
                }
                else { /* game is over - set roundnum to 1, set all joined users' playing status to 0, enter phase 0 */
                    roundnum = 1;
                    for (i=0; i<MAXCLIENTS; i++) {
                        if (clientarray[i].joined != 0) {
                            clientarray[i].playing = 0;
                            clientarray[i].troops = 0;
                        }
                    }
                    waitingfor = -1;
                    phase = 0;
                    fprintf(stderr, "-------- Phase 0: entering lobby --------\n");
                }
            }
        }
        else {
            fprintf(stderr, "Error: phase is out of bounds\n");
            exit(1);
        }
	}
	
	exit(0);
}




static void send_notifies()
{
	int attacker, target, user;
	for (attacker=0; attacker<MAXCLIENTS; attacker++) {
		for (target=0; target<MAXCLIENTS; target++) {
			if (attackgrid[attacker][target] == 1) {
				for (user=0; user<MAXCLIENTS; user++) {
					if (clientarray[user].joined != 0) {
						sprintf(buf, "(schat(SERVER)(NOTIFY,%d,%s,%s))", roundnum, clientarray[attacker].name, clientarray[target].name);
						write_to_client(clientarray[user].socket, user, CLEAR);
					}
				}
			}
		}
	}
}




static void do_battle()
{
    int opponents = 0;
    int player = 0;
    int i;
    int starta, startb;
    
    /* Distribute each player's troops among their skirmishes. */
    for (player=0; player<MAXCLIENTS; player++) {
        if (clientarray[player].playing > 0) {
            for (i=0; i<MAXCLIENTS; i++) {
                if (attackgrid[player][i] == 1 || attackgrid[i][player] == 1) {
                    opponents++;
//fprintf(stderr, "%s is fighting %s\n", clientarray[player].name, clientarray[i].name);
                }
            }
//fprintf(stderr, "%s has %d opponents\n", clientarray[player].name, opponents);
            if (opponents > 0) {
                for (i=0; i<MAXCLIENTS; i++) {
                    if (attackgrid[player][i] == 1 || attackgrid[i][player] == 1) {
                        battlegrid[player][i] = clientarray[player].troops/opponents;
                    }
                }
                int leftover = clientarray[player].troops % ((clientarray[player].troops/opponents)*opponents);
                i = 0;
                while (leftover > 0) {
                    if (attackgrid[player][i] == 1 || attackgrid[i][player] == 1) {
                        battlegrid[player][i] += 1;
                        leftover--;
                    }
                    i++;
                }
            }
        }
        opponents = 0;
    }
    
    /* Do skirmishes. */
    for (player=0; player<MAXCLIENTS; player++) {
//if (clientarray[player].playing == 1) fprintf(stderr, "Start: %s: %d\n", clientarray[player].name, clientarray[player].troops);
        for (i=0; i<MAXCLIENTS; i++) {
            if (i > player && (attackgrid[player][i] == 1 || attackgrid[i][player] == 1)) {
            	clientarray[player].fighting = 1;
            	clientarray[i].fighting = 1;
                if (attackgrid[player][i] == 1) { /* player is attacking - 3 rolls */
                    a.count = 3;
fprintf(stderr, "%s (attacking) vs. %s ", clientarray[player].name, clientarray[i].name);
                }
                else if (attackgrid[i][player] == 1) { /* player is not attacking - 2 rolls */
                    a.count = 2;
fprintf(stderr, "%s (defending) vs. %s ", clientarray[player].name, clientarray[i].name);
                }
                if (attackgrid[i][player] == 1) { /* i is attacking - 3 rolls */
                    b.count = 3;
fprintf(stderr, "(attacking)\n");
                }
                else if (attackgrid[player][i] == 1) { /* i is not attacking - 2 rolls */
                    b.count = 2;
fprintf(stderr, "(defending)\n");
                }
                starta = battlegrid[player][i]; // a = player
                startb = battlegrid[i][player]; // b = i
fprintf(stderr, "Start: %s: %d, %s: %d\n", clientarray[player].name, battlegrid[player][i], clientarray[i].name, battlegrid[i][player]);
                if (starta >= 10 && startb >= 10) { /* both sides have at least 10 troops - fight until one has lost half */
                    starta = starta/2;
                    startb = startb/2;
                }
                else { /* one or both sides has less than 10 troops - fight to the death! */
                    starta = 0;
                    startb = 0;
                }
                while (battlegrid[player][i] > starta && battlegrid[i][player] > startb) { /* skirmish */
                    a.first = (rand()%10)+1;
                    a.second = (rand()%10)+1;
                    if (a.count == 3) {
                        a.third = (rand()%10)+1;
                    }
                    b.first = (rand()%10)+1;
                    b.second = (rand()%10)+1;
                    if (b.count == 3) {
                        b.third = (rand()%10)+1;
                    }
                    sort_rolls();
                    //compare highest rolls
                    if (a.first > b.first) {
                        //b loses a troop
                        battlegrid[i][player] -= 1;
                    }
                    else if (a.first < b.first) {
                        //a loses a troop
                        battlegrid[player][i] -= 1;
                    }
                    //compare second highest rolls
                    if (a.second > b.second) {
                        //b loses a troop
                        battlegrid[i][player] -= 1;
                    }
                    else if (a.second < b.second) {
                        //a loses a troop
                        battlegrid[player][i] -= 1;
                    }
                }
fprintf(stderr, "Result: %s: %d, %s: %d\n", clientarray[player].name, battlegrid[player][i], clientarray[i].name, battlegrid[i][player]);
            }
        }
    }
    
    /* Do cleanup. */
    for (player=0; player<MAXCLIENTS; player++) {
        if (clientarray[player].playing != 0 && clientarray[player].fighting != 0) {
            int remaining = 0;
            for (i=0; i<MAXCLIENTS; i++) { /* count up remaining troops */
                if (battlegrid[player][i] > 0) {
                    remaining += battlegrid[player][i];
                }
            }
//fprintf(stderr, "Final: %s: %d\n", clientarray[player].name, remaining);
            clientarray[player].troops = remaining;
            if (remaining <= 0) {
fprintf(stderr, "%s was killed!\n", clientarray[player].name);
                clientarray[player].playing = -1;
                clientarray[player].troops = 0;
                int j;
                for (j=0; j<MAXCLIENTS; j++) { /* award new troops to any who contributed to a knockout */
                    if (attackgrid[j][player] == 1) {
fprintf(stderr, "%s got new troops for killing %s\n", clientarray[j].name, clientarray[player].name);
                        clientarray[j].troops += startingforce;
                        if (clientarray[j].troops > 99999) {
                            clientarray[j].troops = 99999;
                        }
                    }
                }
            }
        }
    }
    for (player=0; player<MAXCLIENTS; player++) {
    	clientarray[player].fighting = 0;
    }
}




static void sort_rolls()
{
    int temp;
    
    if (a.first < a.second) {
        temp = a.first;
        a.first = a.second;
        a.second = temp;
    }
    if (a.first < a.third) {
        temp = a.first;
        a.first = a.third;
        a.third = temp;
    }
    if (a.second < a.third) {
        temp = a.second;
        a.second = a.third;
        a.third = temp;
    }
    if (b.first < b.second) {
        temp = b.first;
        b.first = b.second;
        b.second = temp;
    }
    if (b.first < b.third) {
        temp = b.first;
        b.first = b.third;
        b.third = temp;
    }
    if (b.second < b.third) {
        temp = b.second;
        b.second = b.third;
        b.third = temp;
    }
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
                                if (*tempbufp == ')') { /* proper cchat - send to valid recipients */
//fprintf (stderr, "Cchat: client %d\n", client_no);
									if (clientarray[client_no].joined != 0) {
										send_chat(&message, &recipients, client_no);
									}
									else {
//fprintf(stderr, "Not joined\n");
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
//fprintf(stderr, "No ')'\n");
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
//fprintf(stderr, "No second '('\n");
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
//fprintf(stderr, "No first '('\n");
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
//fprintf(stderr, "No 't'\n");
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
//fprintf(stderr, "No 'a'\n");
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
//fprintf(stderr, "No 'h'\n");
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
fprintf (stderr, "new user\n");
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
                        if (*tempbufp == ')') { /* proper cstat - respond with sstat */
fprintf (stderr, "Cstat: client %d\n", client_no);
							if (clientarray[client_no].joined != 0) {
fprintf (stderr, "Sending sstat to client %d\n", client_no);
                            	build_user_list();
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
    char *fieldstart = *message;
    char *fieldend = fieldstart;
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
            /* Cchat to ANY - send to valid user. */
			if (numusers > 1) {
				if (numusers == 2) {
					for (i=0; i<MAXCLIENTS; i++) {
						if (i != client_no && clientarray[i].joined != 0) {
							sprintf(buf, "(schat(%s)(%s))", clientarray[client_no].name, short_message);
							write_to_client(clientarray[i].socket, i, CLEAR);
						}
					}
				}
				else {
					int numhops = (rand() % (numusers-1)) + 1;
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
            /* Cchat to ALL - send to all users. */
			for (i=0; i<MAXCLIENTS; i++) {
				if (clientarray[i].joined != 0) {
					sprintf(buf, "(schat(%s)(%s))", clientarray[client_no].name, short_message);
					write_to_client(clientarray[i].socket, i, CLEAR);
				}
			}
			return;
		}
        /* Check for SERVER message. */
        if (strcmp("SERVER", namestart) == 0) {
            // Process SERVER message.
            if (client_no != waitingfor) {
                // Not expecting a SERVER message from this client - strike and return.
fprintf(stderr, "SERVER: waiting for %d, got message from %d\n", waitingfor, client_no);
                send_strike(client_no, 'm');
                return;
            }
            result = find_name_end(&fieldend);
            *fieldend = '\0';
            if (result != 1) { // malformed - strike, increment waitingfor, reset timer, and return
                send_strike(client_no, 'm');
                waitingfor++;
                timerset = 0;
                return;
            }
            if (phase == 1) {
                if (strcmp("PLAN", fieldstart) == 0) { // look for PLAN type message
                    fieldend++;
                    fieldstart = fieldend;
                    result = find_name_end(&fieldend);
                    *fieldend = '\0';
                    if (result != 1) { // not enough fields - strike, increment waitingfor, reset timer, and return
                        send_strike(client_no, 'm');
                        waitingfor++;
                        timerset = 0;
                        return;
                    }
                    int givenround = (int) strtol(fieldstart, NULL, 10); // look for correct round number
                    if (givenround > 99999) { // badint - strike, increment waitingfor, reset timer, and return
                        send_strike(client_no, 'b');
                        waitingfor++;
                        timerset = 0;
                        return;
                    }
                    else if (givenround != roundnum) { // client has wrong round number - strike, increment waitingfor, reset timer, and return
                        send_strike(client_no, 'm');
                        waitingfor++;
                        timerset = 0;
                        return;
                    }
                    fieldend++;
                    fieldstart = fieldend;
                    result = find_name_end(&fieldend);
                    *fieldend = '\0';
                    if (result == -1 && strcmp("PASS", fieldstart) == 0) { // check for PASS action
                        // Player passes - increment waitingfor, reset timer, and return.
                        fprintf(stderr, "PASS: %s\n", clientarray[client_no].name);
                        waitingfor++;
                        timerset = 0;
                        return;
                    }
                    else if (result == 1 && strcmp("APPROACH", fieldstart) == 0) { // check for APPROACH action
                        // Player wants to make an offer - check validity.
                        fieldend++;
                        fieldstart = fieldend;
                        result = find_name_end(&fieldend);
                        *fieldend = '\0';
                        if (result != 1) { // not enough fields - strike, increment waitingfor, reset timer, and return
                            send_strike(client_no, 'm');
                            waitingfor++;
                            timerset = 0;
                            return;
                        }
                        int ally;
                        for (ally=0; ally<MAXCLIENTS; ally++) {
                            if (strcmp(clientarray[ally].name, fieldstart) == 0) {
                                break;
                            }
                        }
                        if (ally < MAXCLIENTS) { // check for valid ally (message ignored if ally = self)
                            if (ally != client_no) {
                                offergrid[ally][client_no].used = 1;
                                clientarray[ally].offers += 1;
                            }
                            fieldend++;
                            fieldstart = fieldend;
                            result = find_name_end(&fieldend);
                            *fieldend = '\0';
                            if (result != -1) { // too many fields - strike, increment waitingfor, reset timer, and return
                                send_strike(client_no, 'm');
                                waitingfor++;
                                timerset = 0;
                                return;
                            }
                            int target;
                            for (target=0; target<MAXCLIENTS; target++) {
                                if (strcmp(clientarray[target].name, fieldstart) == 0) {
                                    break;
                                }
                            }
                            if (target < MAXCLIENTS && clientarray[target].used != 0) { // check for valid target
                                // Player has made a valid offer - add info to offergrid, increment waitingfor, reset timer, and return.
                                if (ally != client_no) {
                                    fprintf(stderr, "APPROACH: %s to %s, attacking %s\n", clientarray[client_no].name, clientarray[ally].name, clientarray[target].name);
                                    offergrid[ally][client_no].target = target;
//fprintf(stderr, "offergrid[ally][client_no].used = %d\n", offergrid[ally][client_no].used);
//fprintf(stderr, "offergrid[ally][client_no].target = %d\n", offergrid[ally][client_no].target);
                                }
                                else {
                                    fprintf(stderr, "APPROACH: %s to self, attacking %s\n", clientarray[client_no].name, clientarray[target].name);
//fprintf(stderr, "offergrid[ally][client_no].used = %d\n", offergrid[ally][client_no].used);
//fprintf(stderr, "offergrid[ally][client_no].target = %d\n", offergrid[ally][client_no].target);
                                }
                                waitingfor++;
                                timerset = 0;
                                return;
                            }
                            else { // invalid target - erase any changes to offergrid, strike, increment waitingfor, reset timer, and return
                                if (ally != client_no) {
                                    offergrid[ally][client_no].used = 0;
                                    clientarray[ally].offers -= 1;
                                }
                                send_strike(client_no, 'm');
                                waitingfor++;
                                timerset = 0;
                                return;
                            }
                        }
                        else { // invalid ally - strike, increment waitingfor, reset timer, and return
                            send_strike(client_no, 'm');
                            waitingfor++;
                            timerset = 0;
                            return;
                        }
                    }
                    else { // invalid action or wrong number of fields - strike, increment waitingfor, reset timer, and return
                        send_strike(client_no, 'm');
                        waitingfor++;
                        timerset = 0;
                        return;
                    }
                }
                else { // invalid message type - strike, increment waitingfor, reset timer, and return
                    send_strike(client_no, 'm');
                    waitingfor++;
                    timerset = 0;
                    return;
                }
            }
            else if (phase == 2) {
                if (strcmp("ACCEPT", fieldstart) == 0 || strcmp("DECLINE", fieldstart) == 0) { // look for ACCEPT or DECLINE type message
                    char *action = fieldstart;
                    fieldend++;
                    fieldstart = fieldend;
                    result = find_name_end(&fieldend);
                    *fieldend = '\0';
                    if (result != 1) { // not enough fields - strike, increment responseto, reset timer, and return
                        send_strike(client_no, 'm');
                        responseto++;
                        timerset = 0;
                        return;
                    }
                    int givenround = (int) strtol(fieldstart, NULL, 10); // look for correct round number
                    if (givenround > 99999) { // badint - strike, increment responseto, reset timer, and return
                        send_strike(client_no, 'b');
                        responseto++;
                        timerset = 0;
                        return;
                    }
                    else if (givenround != roundnum) { // client has wrong round number - strike, increment responseto, reset timer, and return
                        send_strike(client_no, 'm');
                        responseto++;
                        timerset = 0;
                        return;
                    }
                    fieldend++;
                    fieldstart = fieldend;
                    result = find_name_end(&fieldend);
                    *fieldend = '\0';
                    if (result != -1) { // too many fields - strike, increment responseto, reset timer, and return
                        send_strike(client_no, 'm');
                        responseto++;
                        timerset = 0;
                        return;
                    }
                    if (strcmp(clientarray[responseto].name, fieldstart) == 0) {
                        // Valid offer response - send response to ally, increment responseto, reset timer, and return.
                        char actionbuf[8];
                        sprintf(actionbuf, "%s", action);
						fprintf(stderr, "%s: %s to %s\n", actionbuf, clientarray[client_no].name, clientarray[responseto].name);
                        sprintf(buf, "(schat(SERVER)(%s,%d,%s))", actionbuf, roundnum, clientarray[client_no].name);
                        write_to_client(clientarray[responseto].socket, responseto, CLEAR);
                        responseto++;
                        timerset = 0;
                        return;
                    }
                    else { // response to wrong user - strike, increment responseto, reset timer, and return
                        send_strike(client_no, 'm');
                        responseto++;
                        timerset = 0;
                        return;
                    }
                }
                else { // invalid message type - strike, increment responseto, reset timer, and return
                    send_strike(client_no, 'm');
                    responseto++;
                    timerset = 0;
                    return;
                }
            }
            else if (phase == 3) {
                //if not malformed, add action to offergrid (if applicable) and send notify to all joined users
                //else, strike and assume PASS
                if (strcmp("ACTION", fieldstart) == 0) { // look for ACTION type message
                    fieldend++;
                    fieldstart = fieldend;
                    result = find_name_end(&fieldend);
                    *fieldend = '\0';
                    if (result != 1) { // not enough fields - strike, increment waitingfor, reset timer, and return
                        send_strike(client_no, 'm');
                        waitingfor++;
                        timerset = 0;
                        return;
                    }
                    int givenround = (int) strtol(fieldstart, NULL, 10); // look for correct round number
                    if (givenround > 99999) { // badint - strike, increment waitingfor, reset timer, and return
                        send_strike(client_no, 'b');
                        waitingfor++;
                        timerset = 0;
                        return;
                    }
                    else if (givenround != roundnum) { // client has wrong round number - strike, increment waitingfor, reset timer, and return
                        send_strike(client_no, 'm');
                        waitingfor++;
                        timerset = 0;
                        return;
                    }
                    fieldend++;
                    fieldstart = fieldend;
                    result = find_name_end(&fieldend);
                    *fieldend = '\0';
                    if (result == -1 && strcmp("PASS", fieldstart) == 0) {
                        // Player passes - increment waitingfor, reset timer, and return.
                        fprintf(stderr, "PASS: %s\n", clientarray[client_no].name);
                        waitingfor++;
                        timerset = 0;
                        return;
                    }
                    else if (result == 1 && strcmp("ATTACK", fieldstart) == 0) {
                        // Player wants to attack - check validity.
                        fieldend++;
                        fieldstart = fieldend;
                        result = find_name_end(&fieldend);
                        *fieldend = '\0';
                        if (result == -1) {
                            for (i=0; i<MAXCLIENTS; i++) {
                                if (strcmp(clientarray[i].name, fieldstart) == 0 && clientarray[i].playing == 1) {
                                    break;
                                }
                            }
                            if (i < MAXCLIENTS) {
                                // Valid attack message - update attackgrid, increment waitingfor, reset timer, and return.
                                fprintf(stderr, "ATTACK: %s to %s\n", clientarray[client_no].name, clientarray[i].name);
                                if (i != client_no) {
                                	attackgrid[client_no][i] = 1;
                                }
                                waitingfor++;
                                timerset = 0;
                                return;
                            }
                            else { // invalid target - strike, increment waitingfor, reset timer, and return
                                send_strike(client_no, 'm');
                                waitingfor++;
                                timerset = 0;
                                return;
                            }
                        }
                        else { // too many fields - strike, increment waitingfor, reset timer, and return
                            send_strike(client_no, 'm');
                            waitingfor++;
                            timerset = 0;
                            return;
                        }
                    }
                    else { // invalid action or wrong number of fields - strike, increment waitingfor, reset timer, and return
                        send_strike(client_no, 'm');
                        waitingfor++;
                        timerset = 0;
                        return;
                    }
                }
                else { // invalid message type - strike, increment waitingfor, reset timer, and return
                    send_strike(client_no, 'm');
                    waitingfor++;
                    timerset = 0;
                    return;
                }
            }
            else {
                fprintf(stderr, "Error: phase is out of bounds\n");
                exit(1);
            }
        }
	}
	
	/* Send message to all valid recipients. */
	int namefound = 0; int strikesent = 0;
	while (result != 0) {
		namefound = 0;
		for (i=0; i<MAXCLIENTS; i++) {
			if (strcmp(clientarray[i].name, namestart) == 0) {
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
		if (strcmp(clientarray[i].name, namestart) == 0) {
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
	
	/* Reset 'sent' flag for all users. */
	for (i=0; i<MAXCLIENTS; i++) {
		clientarray[i].sent = 0;
	}
}





static int find_name_end(char **current)
{
    char *pos = *current;
    
    while (*pos != ')' && *pos != ',' && *pos != '\0') {
        pos++;
    }
    *current = pos;
    if (*pos == ')') {
        return 0;
    }
    else if (*pos == ',') {
        return 1;
    }
    else {
        return -1;
    }
}




static void convert_name(char **name)
{
	char *namepos = *name;
	char temp[480];
	char *temppos = temp;
	
	/* Copy name into temp string. */
	while (*namepos != ')' && *namepos != '\0') {
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
	if (strcmp("ALL", temp) == 0 || strcmp("ANY", temp) == 0 || strcmp("SERVER", temp) == 0) {
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

	/* update user information, send sjoin to new user and sstat to all other users */
	clientarray[client_no].joined = 1;
	numusers++;
	build_user_list_names();
	sprintf(buf, "(sjoin(%s)(%s)(%d,%d,%d))", clientarray[client_no].name, listbuf, minplayers, lobbytime, timeout);
	write_to_client(clientarray[client_no].socket, client_no, CLEAR);
	memset(listbuf, '\0', MAXMESSAGE);
	build_user_list();
	for (i=0; i<MAXCLIENTS; i++) {
		if (clientarray[i].joined == 1 && i != client_no) {
			sprintf(buf, "(sstat(%s))", listbuf);
			write_to_client(clientarray[i].socket, i, CLEAR);
		}
	}
	memset(listbuf, '\0', MAXMESSAGE);
}




static void build_user_list_names()
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
	if (added == numusers) {
	}
	else {
fprintf(stderr, "Error: user list does not agree with numusers\n");
	}
}






static void build_user_list()
{
    char triple[21];
	int added = 0;
	int i;
	for (i=0; i<MAXCLIENTS; i++) {
		if (clientarray[i].joined != 0) {
			if (added == 0) {
				sprintf(listbuf, "%s,%d,%d", clientarray[i].name, clientarray[i].strikes, clientarray[i].troops);
			}
			else {
				strcat(listbuf, ",");
                sprintf(triple, "%s,%d,%d", clientarray[i].name, clientarray[i].strikes, clientarray[i].troops);
				strcat(listbuf, triple);
                memset(triple, '\0', 21);
			}
			added++;
		}
	}
	if (added == numusers) {
	}
	else {
fprintf(stderr, "Error: user list does not agree with numusers\n");
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
        if (clientarray[client_no].joined != 0) { /* client had joined - send sstat to all users */
				numusers--;
				clientarray[client_no].joined = 0;
				build_user_list();
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
			if (clientarray[client_no].joined != 0) { /* client had joined - send sstat to all users */
				numusers--;
				clientarray[client_no].joined = 0;
				build_user_list();
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




static void zero_grids()
{
    int i, j;
    for (i=0; i<MAXCLIENTS; i++) {
        for (j=0; j<MAXCLIENTS; j++) {
            offergrid[i][j].used = 0;
            attackgrid[i][j] = 0;
            battlegrid[i][j] = 0;
        }
    }
}






void clear_clientinfo(int client_no)
{
	clientarray[client_no].used = 0;
	clientarray[client_no].joined = 0;
    clientarray[client_no].playing = 0;
    clientarray[client_no].fighting = 0;
	clientarray[client_no].sent = 0;
    clientarray[client_no].offersent = 0;
	clientarray[client_no].socket = -1;
	memset(clientarray[client_no].name, '\0', NAMESIZE+1);
	memset(clientarray[client_no].clibuf, '\0', BUFSIZE);
	clientarray[client_no].charcount = 0;
	clientarray[client_no].strikes = 0;
	clientarray[client_no].resync = 0;
    clientarray[client_no].troops = 0;
	clientarray[client_no].plangiven = 0;
	clientarray[client_no].offers = 0;
}






void initialize_clientinfo(int client_no)
{
	clientarray[client_no].used = 0;
	clientarray[client_no].joined = 0;
    clientarray[client_no].playing = 0;
    clientarray[client_no].fighting = 0;
	clientarray[client_no].sent = 0;
    clientarray[client_no].offersent = 0;
	clientarray[client_no].socket = -1;
	clientarray[client_no].name = malloc((NAMESIZE+1)*sizeof(char));
	clientarray[client_no].clibuf = malloc(BUFSIZE*sizeof(char));
	clientarray[client_no].charcount = 0;
	clientarray[client_no].strikes = 0;
	clientarray[client_no].resync = 0;
    clientarray[client_no].troops = 0;
	clientarray[client_no].plangiven = 0;
	clientarray[client_no].offers = 0;
}
