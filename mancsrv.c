#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80 /* maximum permitted name size, not including \0 */
#define NPITS 6 /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* initial number of pebbles per pit */

int port = 3000;
int listenfd;

struct player {
    int fd;
    char name[MAXNAME+1]; 
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits 
                        // pits[NPITS] is the end pit
    struct player *next;
};
struct player *playerlist = NULL;


extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s, int size);  /* you need to write this one */
extern int check_name(char *buffer, int length);
extern void printboard();
extern struct player* removefd(int rfd, struct player *from);
extern void playgame(struct player *turn, int pitnum);


int main(int argc, char **argv) {
    char msg[MAXMESSAGE];
    char buffer[MAXNAME + 1] = {'\0'};

    parseargs(argc, argv);
    makelistener();
    int newfd;
    int max_fd = listenfd;
    fd_set all_fd;
    FD_SET(listenfd, &all_fd);
    struct player *turn = NULL;
    struct player *preturn = NULL;
    struct player *waitinglist = NULL;

    while (!game_is_over()) {

        //if turn changed, ask move for next turn and show other the turn.
        if (preturn != turn){
            snprintf(msg, MAXMESSAGE, "Your move?\r\n");
            write(turn->fd, msg, strlen(msg));
            snprintf(msg, MAXMESSAGE, "It is %s's move.\r\n", turn->name);
            for (struct player *a = playerlist; a; a = a->next){
                if (a->fd != turn->fd){
                    write(a->fd, msg, strlen(msg));
                }
            }
            preturn = turn;
        }

        //build a listen_fd list for select method
        fd_set listen_fd = all_fd;
        if(select(max_fd + 1, &listen_fd, NULL, NULL, NULL) == -1){
            perror("server: select");
            exit(1);
        }

        //If new player connect to the server, add them to waiting list to enter name.
        if(FD_ISSET(listenfd, &listen_fd)){
            newfd = accept(listenfd, NULL, NULL);
            if(newfd < 0){
                perror("server: accept");
                close(listenfd);
                exit(1);
            }
            if(newfd > max_fd){
                max_fd = newfd;
            }
            FD_SET(newfd, &all_fd);
            printf("New player connected at fd %d.\n", newfd);
            snprintf(msg, MAXMESSAGE, "Welcome to Mancala. What is your name?\r\n");
            write(newfd, msg, strlen(msg));
            struct player *newplayer = malloc(sizeof(struct player));
            newplayer->fd = newfd;
            newplayer->next = waitinglist;
            waitinglist = newplayer;
        }

        //For player that not their turn, chekc wheather they are disconnected or type useless message.
        for (struct player *b = playerlist; b; b = b->next){
            if(FD_ISSET(b->fd, &listen_fd) && b->fd != turn->fd){
                //the player disconnected from the game.
                if(read(b->fd, buffer, MAXMESSAGE) == 0){
                   printf("Player disconnected at fd %d.\n", b->fd);
                   FD_CLR(b->fd, &all_fd);
                   playerlist = removefd(b->fd, playerlist);
                   snprintf(msg, MAXMESSAGE, "Player %s leave the game.\r\n", b->name);
                   broadcast(msg, strlen(msg));
                   printboard();
                   close(b->fd);
                   free(b);
                   memset(b->name, 0, strlen(b->name));
               }
               //the play type some useless messages.
               else{
                   snprintf(msg, MAXMESSAGE, "It is not your move.\r\n");
                   write(b->fd, msg, strlen(msg));
               }
            }
        }

        //For player that is his turn, check whether he is disconneted or enter the pit number.
        if (turn){
            if (FD_ISSET(turn->fd, &listen_fd)){
                memset(buffer, 0, strlen(buffer));
                //the player disconnected from the game, move to the next turn.
                if (read(turn->fd, buffer, MAXMESSAGE) == 0){
                    struct player *temp = turn;
                    printf("Player disconnected at fd %d\n", temp->fd);
                    FD_CLR(temp->fd, &all_fd);
                    playerlist = removefd(temp->fd, playerlist);
                    if (turn->next){
                        turn = turn->next;
                    }
                    else{
                        turn = playerlist;
                    }
                    preturn = NULL;
                    snprintf(msg, MAXMESSAGE, "Player %s leave the game.\r\n", temp->name);
                    broadcast(msg, strlen(msg));
                    printboard();
                    close(temp->fd);
                    free(temp);
                    memset(temp->name, 0, strlen(temp->name));
                }
                //the player enter the pit number.
                else{
                    int pitnum = strtol(buffer, NULL, 10);
                    //the pit number is not valid.
                    if(pitnum < 0 || pitnum >= NPITS || (turn->pits)[pitnum] == 0){
                        snprintf(msg, MAXMESSAGE, "This is not a valid move\r\n");
                        write(turn->fd, msg, strlen(msg));
                    }
                    //the pit number is valid, start to play game and then move to next turn.
                    else{
                        printf("Player at fd %d moves from pit %d\n", turn->fd, pitnum);
                        snprintf(msg, MAXMESSAGE, "%s moves from pit %d\r\n", turn->name, pitnum);
                        broadcast(msg, strlen(msg));
                        playgame(turn, pitnum);
                        printboard();
                        if (turn->next){
                            turn = turn->next;
                        }
                        else{
                            turn = playerlist;
                        }
                        preturn = NULL;
                    }
                }
            }
        }

        //check player in waiting list enter the full name or disconnected from the game.
        for (struct player *c = waitinglist; c; c = c->next){
            if(FD_ISSET(c->fd, &listen_fd)){
                char *after = &(c->name[strlen(c->name)]);
                int room = MAXNAME + 1 - (strlen(c->name));
                int bytes = read(c->fd, after, room);
                int check = check_name(c->name, strlen(c->name));
                //the player disconnected from the game or enter the repeated name or the name is too long.
                if( bytes == 0 || check == 2){
                    printf("Player disconnected at fd %d.\n", c->fd);
                    FD_CLR(c->fd, &all_fd);
                    waitinglist = removefd(c->fd, waitinglist);
                    close(c->fd);
                    free(c);
                    memset(c->name, 0, strlen(c->name));
                }
                //the player enter a valid full name, remove him from waiting list and add him to playerlist.
                else if( check == 0){
                    printf("fd %d enters name %s.\n", c->fd, c->name);
                    snprintf(msg, MAXMESSAGE, "New player %s join the game!\r\n", c->name);
                    broadcast(msg, strlen(msg));
                    for(int i = 0; i < NPITS; i++){
                        (c->pits)[i] = compute_average_pebbles();
                    }
                    if(!playerlist){
                        turn = c;
                    }
                    else{
                        snprintf(msg, MAXMESSAGE, "It is %s's move.\r\n", turn->name);
                        write(c->fd, msg, strlen(msg));
                    }
                    waitinglist = removefd(c->fd, waitinglist);
                    c->next = playerlist;
                    playerlist = c;
                    printboard();
                }
            }
        }
    }

    broadcast("Game over!\r\n", 12);
    printf("Game over!\n");
    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        printf("%s has %d points\n", p->name, points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg, strlen(msg));
    }

    return 0;
}


void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);  
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}


void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}



/* call this BEFORE linking the new player in to the list */
int compute_average_pebbles() { 
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


int game_is_over() { /* boolean */
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}

//send message to all player in playerlist
void broadcast(char *s, int size){
    for (struct player *p = playerlist; p; p = p->next){
        write(p->fd, s, size);
    }
}

//check the given name is complete, repeated or valid.
//return 0 if the name is valid and complete.
//return 1 if the name is not complete.
//return 2 if the name is repeated.
int check_name(char *buffer, int length){
    int flag = 1;
    if (buffer[0] != '\n' && buffer[0] != '\r'){
        for (int i = 0; i < length; i++){
            if (buffer[i] == '\n' || buffer[i] == '\r'){
                buffer[i] = '\0';
                flag = 0;
            }
        }
    }
    else{
        memset(buffer, 0, strlen(buffer));
    }

    if (playerlist && flag == 0){
        for (struct player *p = playerlist; p; p = p->next){
            if (strcmp(p->name, buffer) == 0){
                return 2;
            }
        }
    }
    return flag;
}

//print the game board for all player in playerlist.
void printboard(){
    char buffer[MAXMESSAGE];
    char msg[MAXMESSAGE];
    for (struct player *p = playerlist; p; p = p->next){
        snprintf(msg, MAXMESSAGE, "%s:  ", p->name);
        for (int j = 0; j < NPITS; j++){
            snprintf(buffer, MAXMESSAGE, "[%d]%d ", j, p->pits[j]);
            strcat(msg, buffer);
        }
        snprintf(buffer, MAXMESSAGE, " [end pit]%d\r\n", p->pits[NPITS]);
        strcat(msg, buffer);
        broadcast(msg, strlen(msg));
    }
}

//remove a player from given linklist and return a new head of the linklist.
struct player* removefd(int rfd, struct player *from){
    struct player *pre = from;
    if(from->fd == rfd){
        return from->next;
    }
    else{
        for (struct player *p = from->next; p; p = p->next){
            if (p->fd == rfd){
                pre->next = p->next;
            }
            pre = p;
        }
        return from;
    }
}

//playing the game by given player and the pit number.
void playgame(struct player *turn, int pitnum){
    int number = (turn->pits)[pitnum];
    struct player* target = turn;
    int pfd = turn->fd;
    (turn->pits)[pitnum] = 0;
    pitnum ++;
    while(number != 0){
        if(target->fd == pfd || pitnum != NPITS ){
            (target->pits)[pitnum] = (target->pits)[pitnum] + 1;
            number --;
        }
        pitnum ++;
        if(pitnum > NPITS){
            pitnum = 0;
            if(target->next){
                target = target->next;
            }
            else{
                target = playerlist;
            }
        }
    }
}