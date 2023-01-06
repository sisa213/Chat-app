 /*
 * utility_s.c:     file contenente definizioni macro, strutture dati e funzioni di routine utilizzate dal server.
 *
 * Samayandys Sisa Ruiz Muenala, 10 novembre 2022
 * 
 */

/*---------------------------------------
*           LIBRERIE DI SISTEMA
*----------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <netdb.h>
#include <time.h>


/*---------------------------------------
*               MACRO
*----------------------------------------*/

#define BUFF_SIZE 1024                          // dimensione massima di un buffer di ausilio
#define DEFAULT_SV_PORT 4242                    // porta su cui ascolta il server
#define CMD_SIZE 3                              // dimensione di una stringa comando
#define RES_SIZE 1                              // dimensione di una stringa responso
#define USER_LEN 50                             // massima lunghezza di un username
#define MSG_LEN 1024                            // massima lunghezza di un messaggio
#define TIME_LEN 20                             // dimensione di una stringa timestamp
#define NA_LOGOUT "                    "        // timestamp di logout di una sessione attiva o sospesa (disconnessione irregolare)


/*--------------------------
*       STRUTTURE DATI
*---------------------------*/
/* 
* session_log: struttura che descrive una sessione di lavoro di un dispositivo
* una sessione viene creata ogni volta che l'utente fa il login
*/
struct session_log {
    char username[USER_LEN+1];
    int port;      
    char timestamp_login[TIME_LEN+1];
    char timestamp_logout[TIME_LEN+1];
    int socket_fd;                   // -1 se si tratta di una sessione vecchia da aggiornare
    struct session_log* next;
};

/* 
* message: struttura che descrive un messaggio. 
*/
struct message {
    char sender[USER_LEN+1];
    char recipient[USER_LEN+1];   
    char time_stamp[TIME_LEN+1];
    char text[MSG_LEN];
    char status[3];                 // '*': se non ancora letto dal destinatario, '**': altrimenti
    struct message* next;
};

/* 
* ack: contiene informazioni sulla ricezione o memorizzazione di uno o più messaggi 
*/
struct ack{
    char sender[USER_LEN+1];        
    char recipient[USER_LEN+1];
    char start_time[TIME_LEN+1];   
    int port_recipient;             // porta del destinatario del messaggio
    int status;                     // 1: memorizzato dal server, 2: inviato al destinatario 
    struct ack* next;
}; 

/*
* preview_user: struttura riportante informazioni sui messaggi pendenti di un utente per un altro
*/
struct preview_user{
    char user[USER_LEN+1];
    int messages_counter;
    char timestamp[TIME_LEN+1];     // timestamp del messaggio più recente
    struct preview_user* next;
};


/*----------------------------
*       UTILITY FUNCTIONS
*-----------------------------*/

/*
* Function: basic_send
* -----------------------
* gestisce l'invio di una stringa tramite il socket 'sck'
*/
void basic_send(int sck, char* mes){

    uint16_t lmsg;
    char buff[BUFF_SIZE];
    
    memset(buff, 0, sizeof(buff));

    strcpy(buff, mes);
    lmsg = strlen(mes)+1;
    lmsg = htons(lmsg);
    send(sck, (void*)&lmsg, sizeof(uint16_t), 0);
    send(sck, (void*)buff, strlen(buff)+1, 0);

}


/*
* Function: basic_receive
* -----------------------
* gestisce la ricezione di una stringa tramite il socket 'sck'
*/
void basic_receive(int sck, char* buff){

    uint16_t lmsg;
    
    recv(sck, (void*)&lmsg, sizeof(uint16_t), 0);
    lmsg = ntohs(lmsg);
    recv(sck, (void*)buff, lmsg, 0);

}


/*
* Function: send_server_message
* -------------------------------
* invia messaggi sull'esito o sullo stato del servizio richiesto dal dispositivo-client
*/
void send_server_message(int socket, char* message, bool error){

    char c[RES_SIZE+1];
    char buffer[BUFF_SIZE];
    uint16_t message_len;

    memset(c, 0, sizeof(c));
    memset(buffer, 0, sizeof(buffer));

    printf("[+]Sending outcome of request to client...\n");

    if (error)
        strcpy(c,"E");
    else strcpy(c,"S");
    // invio prima l'esito della richiesta
    send(socket, (void*)c, 2, 0);

    // invio poi un eventuale messaggio al dispositivo
    if (message!=NULL){

        strcpy(buffer, "\n\t");
        strcat(buffer, message);
        strcat(buffer, "\n");

        //invio prima la dimensione
        message_len = strlen(buffer)+1;
        message_len = htons(message_len);
        send(socket, (void*)&message_len, sizeof(uint16_t), 0);
        //invio ora il messaggio
        send(socket, (void*)buffer, strlen(buffer)+1, 0);
    }

    printf("[+]Message sent to client.\n");
}


/*
* Function: prompt_user
* ----------------------
* richiede all'utente un input
*/
void prompt_user(){
    
    printf("\n>> ");
    fflush(stdout);
}


/*
* Function: name_checked
* -------------------------
* restituisce un puntatore all'elemento preview_user che ha come user 'name'
*/
struct preview_user* name_checked(struct preview_user* list, char* name){

    struct preview_user* temp = list;

    while (temp){
        if ( strcmp(temp->user, name)==0 ){
            break;
        }
        temp = temp->next;
    }
    return temp;
}


/*
* Function: check_username
* ----------------------------
* se nel file degli utenti registrati trova un user colnome 'name restituisce 1, altrimenti -1.
*/
int check_username(char* name){

    FILE *fp;
    char buff[BUFF_SIZE];
    char cur_name[USER_LEN+1];
    int ret = -1;
    
    fp = fopen("users.txt", "r");
    if (fp == NULL){
        printf("[+]No registered useres yet.\n");
        return -1;
    }
    printf("[+]File users.txt opened.\n");

    while (fgets(buff, BUFF_SIZE, fp)!=NULL)
    {
        memset(cur_name, 0, sizeof(cur_name));
        sscanf(buff, "%s %*s %*d", cur_name);
        if (strcmp(cur_name, name)==0){
            printf("[+]Username found.\n");
            ret=1;
            break;
        }
    }

    if(ret==-1){
        printf("[-]Username not found.\n");
    }

    fclose(fp);
    return ret;
}


/*
* Function: get_name_from_sck
* -------------------------------
* cerca nella lista 'list' il device avente socket uguale a 's'.
* Restitusce lo username associato al device.
*/
char* get_name_from_sck(struct session_log* list, int s){

    struct session_log* temp = list;
    while (temp){
        if (strcmp(temp->timestamp_logout, NA_LOGOUT)==0 && temp->socket_fd==s){
            return temp->username;
        }
        temp = temp->next;
    }
    return NULL;
}


/*
* Function: add_to_stored_messages
* ------------------------------------
* salva nei file dei messaggi il messaggio m
*/
void add_to_stored_messages(struct message* m){
    
    FILE* fp, *fp1;

    fp = fopen("./chat_info.txt", "a");
    fp1 = fopen("./chats.txt", "a");

    fprintf(fp, "%s %s %s\n", m->sender, m->recipient, m->time_stamp);
    fprintf(fp1, "%s\n", m->text);

    fclose(fp);
    fclose(fp1);
}

/*
 * Function:  get_port
 * ---------------------
 * returns port of user, if not existent returns -1 
 */
int get_port(char* user){

    FILE* fp;
    char cur_us[USER_LEN+1];
    char buff[BUFF_SIZE];
    int cur_port;

    fp = fopen("./users.txt", "r");
    while( fgets(buff, BUFF_SIZE, fp)!=NULL ){

        memset(cur_us, 0, sizeof(cur_us));

        sscanf(buff, "%s %*s %d", cur_us, &cur_port);
        if (strcmp(cur_us, user)==0){
            return cur_port;
        }
    }

    return -1;
}