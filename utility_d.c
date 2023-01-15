 /*
 * utility_d.c:     file contenente definizioni macro, strutture dati e funzioni di routine utilizzate dai device.
 *
 * Samayandys Sisa Ruiz Muenala, 10 novembre 2022
 * 
 */


/*---------------------------------------
*           LIBRERIE DI SISTEMA
*----------------------------------------*/

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>


/*---------------------------------------
*               MACRO
*----------------------------------------*/

#define BUFF_SIZE 1024              // dimensione massima di un buffer di ausilio
#define CMD_SIZE 3                  // dimensione di una stringa comando
#define RES_SIZE 1                  // dimensione di una stringa responso
#define USER_LEN 50                 // massima lunghezza di un username
#define MSG_LEN 1024                // massima lunghezza di un messaggio
#define TIME_LEN 20                 // dimensione di una stringa timestamp
#define CRYPT_SALT 0xFACA           // salt per la funzione di criptazione
extern char host_user[USER_LEN+1];  // username dell'utente associato al device


/*---------------------------------------
*           STRUTTURE DATI
*----------------------------------------*/
/* 
* message: struttura che descrive un messaggio. 
*/
struct message {
    char sender[USER_LEN+1];
    char recipient[USER_LEN+1];   
    char time_stamp[TIME_LEN+1];
    char group[USER_LEN+6];      // '-' se non fa parte di una conversazione di gruppo
    char text[MSG_LEN];
    char status[3];              // '*': se non ancora letto dal destinatario, '**': altrimenti
    struct message* next;
};

/*
* con_peer: struttura che contiene informazioni sulle connessioni attive peer to peer
*/
struct con_peer {
    char username[USER_LEN+1];
    int socket_fd;          
    struct con_peer* next;
};

/*
* chat: struttura che descrive una conversazione di gruppo o a due
*/
struct chat {
    char recipient[USER_LEN+1];
    char group[USER_LEN+6];
    int sck;
    bool on;                    // se true la conversazione è correntemente visualizzata a video
    uint8_t users_counter;
    struct con_peer* members;
    struct chat* next;
};


/*-------------------------------
*         UTILITY FUNCTIONS
*--------------------------------*/

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
 * Function:  add_to_con
 * -----------------------
 * aggiunge una nuova connessione peer alla lista 'head'
 */
struct con_peer* add_to_con(struct con_peer *head, int sck, char* u)
{
    struct con_peer *newNode = (struct con_peer*)malloc(sizeof(struct con_peer));
    strcpy(newNode->username, u);
    newNode->socket_fd = sck;
    newNode->next = head;

    printf("[+]New peer added to connections.\n");

    return (newNode);
}


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
* Function: get_conn_peer
* ---------------------------
* se con lo user 'p' è già stata instaurata una connessione TCP restituisce il socket relativo
* altrimenti restituisce -1
*/
int get_conn_peer(struct con_peer* list, char* p){

    int sck = -1;
    struct con_peer* temp = list;
    while (temp){
        if (strcmp(temp->username, p)==0){
            sck = temp->socket_fd;
            break;
        }
        temp = temp->next;   
    }
    return sck;
}


/*
* Function: check_contact_list
* ---------------------------
* data la stringa 'name' controlla che esista un contatto con tale nome;
* se esiste viene restituito il numero di porta del contatto altrimenti -1.
*/
int check_contact_list(char* name){

    FILE* fptr;
    int cur_port;
    char buff[USER_LEN+1];
    char cur_name[USER_LEN+1];
    char fn[BUFF_SIZE]="./";

    memset(buff, 0, sizeof(buff));

    strcat(fn, host_user);
    strcat(fn, "/contact_list.txt");

    fptr = fopen(fn, "r");
    if (fptr==NULL){
        return -1;
    }

    while ( fgets( buff, sizeof buff, fptr ) != NULL )
    {   
        memset(cur_name, 0, sizeof(cur_name));
        sscanf (buff, "%s %d", cur_name, &cur_port);            

        if ( strcmp(cur_name, name) == 0 )
        {   printf("[+]Username found in contact list.\n");
            return cur_port;
        }
    }
    return -1;
}

/*
* Function: add_contact_list
* ----------------------------
* dati user e porta provvede ad aggiungere il nuovo contatto nella rubrica
*/
void add_contact_list(char* name, int po){

    FILE* fptr;
    char fn[BUFF_SIZE];

    strcpy(fn, "./");
    strcat(fn, host_user);
    strcat(fn, "/contact_list.txt");
    fptr = fopen(fn, "a");
    
    fprintf(fptr, "%s %d\n", name, po);
    fclose(fptr);
}

/*
* Function: compare_timestamp
* -------------------------------
* confronta il timestamp di due oggetti messagge
*/
int compare_timestamp(const struct message *a, const struct message *b) {

     return strcmp(a->time_stamp, b->time_stamp);
}

/*
* Function: insert_sorted
* --------------------------
* inserisce un nuovo elemento nella lista 'headptr'. L'inserimento è ordinato in base al timestamp.
*/
void insert_sorted(struct message* headptr, struct message *new_node) {

    struct message *ptr = new_node;

    if (headptr == NULL || compare_timestamp(ptr, headptr) < 0) {
        ptr->next = headptr;
        return;
    } else {
        struct message *cur = headptr;
        while (cur->next != NULL && compare_timestamp(ptr, cur->next) >= 0) {
            cur = cur->next;
        }
        ptr->next = cur->next;
        cur->next = ptr;
        return;
    }
}

/*
* Function: sort_messages
* --------------------------
* ordina il contenuto dei file chat in base al timestamp
*/
void sort_messages(char fn[], char fn1[]){

    FILE *fp, *fp1;
    char buff_info[BUFF_SIZE];
    char buff_chat[MSG_LEN];
    struct message* new_list = NULL;
    struct message* temp;

    if ( (fp = fopen(fn,"r+"))==NULL || (fp1 = fopen(fn1, "r+"))==NULL){
        perror("[-]Error opening users files");
        return;
    }
    printf("[+]Chat files correctly opened.\n");

    // scorro le righe dei file
    while( fgets(buff_info, BUFF_SIZE, fp)!=NULL && fgets(buff_chat, MSG_LEN, fp1)!=NULL ) {

        char day[TIME_LEN];
        char hour[TIME_LEN];

        struct message* new_msg = (struct message*)malloc(sizeof(struct message));
        if (new_msg == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }


        memset(day, 0, sizeof(day));
        memset(hour, 0, sizeof(hour));

        // memorizzo i singoli dati riportati in ogni riga in una struct message
        sscanf (buff_info, "%s %s %s %s", day, hour, new_msg->group, new_msg->sender);
        sprintf(new_msg->time_stamp, "%s %s", day, hour);
        sscanf (buff_chat, "%s %[^\n]", new_msg->status, new_msg->text);
        // aggiungo il nuovo oggetto message ad una lista 
        insert_sorted(new_list, new_msg);   // inserimento ordinato
    }

    temp = new_list;
    while (new_list){

        new_list = new_list->next;

        fprintf(fp, "%s %s %s\n", temp->time_stamp, temp->group, temp->sender);
        fflush(fp);
        fprintf(fp1, "%s %s\n", temp->status, temp->text);
        fflush(fp1);

        free(temp);
        temp = new_list;
    }

    fclose(fp);
    fclose(fp1);

    printf("[+]Cache sorted.\n");
}


/*
* Function: get_name_from_sck
* -------------------------------
* cerca nella lista 'p' il peer avente socket pari a 's' e restitusce lo username del peer.
*/
const char* get_name_from_sck(struct con_peer* p, int s){

    struct con_peer* temp = p;

    while (temp){

        if (temp->socket_fd==s){
            return temp->username;
        }
        temp = temp->next;
    }

    return NULL;
}


/*
* Function: encrypt
* ----------------------
* data la stringa 'password' provvede a criptarla.
*/
void encrypt(char password[],int key)
{
    unsigned int i;
    for(i=0;i<strlen(password);++i)
    {
        password[i] = password[i] - key;
    }
}


/*
 * Function:  remove_from_peers
 * ------------------------------
 * elimina la connessione del peer avente username 'key' dalla lista peers.
 */
void remove_from_peers(struct con_peer** list, const char* key)
{
    struct con_peer *temp;
 
    if (strcmp((*list)->username, key)==0) {

        temp = *list; 
        close(temp->socket_fd);
        *list = (*list)->next;
        free(temp); 
    }
    else{
        struct con_peer* current = *list;
        while(current->next!=NULL){

            if(strcmp(current->next->username, key)==0){

                temp = current->next;
                // chiudo il socket associato
                close(temp->socket_fd);
                current->next = current->next->next;
                free(temp);
                break;
            }
            else
                current = current->next;
        }
    }

    printf("[+]Peer connection removed.\n");
}


/*
* Function: store_message
* ----------------------------
* funzione invocata ogni qualvolta devo inviare un messaggio ad un destinatario offline
* e anche il server risulta offline. Tale messaggio è salvato in file locali.
*/
void store_message(struct message* msg){

    FILE* fp, *fp1;
    char fn[BUFF_SIZE], fn1[BUFF_SIZE];

    // memorizzo in file il messaggio da inviare
    strcpy(fn, "./");
    strcat(fn, host_user);
    strcat(fn, "/buffered_info.txt");

    strcpy(fn1, "./");
    strcat(fn1, host_user);
    strcat(fn1, "/buffered_texts.txt");

    fp = fopen(fn, "a");
    fp1 = fopen(fn1, "a");

    fprintf(fp, "%s %s %s\n",  msg->time_stamp, host_user, msg->recipient);
    fprintf(fp1, "%s", msg->text);

    fclose(fp);
    fclose(fp1);
}


/*
* Function: print_message
* -------------------------
* formatta un messaggio aggiungendovi altre informazioni e lo stampa
*/
void print_message(struct message* msg){
    
    printf("\n");

    printf("%s %s %s\n", msg->time_stamp, msg->group, msg->sender);
    
    printf("%s %s\n", msg->status, msg->text);
    printf("\n");  
}


/*
* Function: save_message
* --------------------------
* salva il messaggio nella cache apposita
*/
void save_message(struct message* msg){

    FILE* fp, *fp1;
    char file_name0[USER_LEN+20];
    char file_name1[USER_LEN+20];
    struct stat st = {0};


    printf("[+]Caching message...\n");

    // ottengo i nomi dei file
    strcpy(file_name0, "./");
    strcat(file_name0, host_user);
    strcat(file_name0, "/cache/");

    // se ancora non esiste creo la subdirectory
    if (stat(file_name0, &st) == -1) {
        if (!mkdir(file_name0, 0700)){
            printf("[+]Subdirectory created.\n");
        }
        else{
            perror("[-]Error while creating subdirectory");
            exit(-1);
        }
    }

    strcpy(file_name1, "./");
    strcat(file_name1, host_user);
    strcat(file_name1, "/cache/");

    if (strcmp(msg->status, "-")==0){
        if (strcmp(msg->group, "-")==0){
            strcat(file_name0, msg->sender);
            strcat(file_name1, msg->sender);
        }
        else{
            strcat(file_name0, msg->group);
            strcat(file_name1, msg->group);
        }
    }
    else{
        strcat(file_name0, msg->recipient);
        strcat(file_name1, msg->recipient);
    }

    strcat(file_name0, "_info.txt");
    strcat(file_name1, "_texts.txt");

    // salvo le informazioni sul messaggio in file
    fp = fopen(file_name0, "a");    // info

    fprintf(fp, "%s %s %s\n", msg->time_stamp, msg->group, msg->sender);
    fflush(fp);
    fclose(fp);

    if (strcmp(msg->sender, host_user)!=0){
        strcpy(msg->status, "-");
    }
    
    // salvo il messaggio in file
    fp1 = fopen(file_name1, "a");   // text
    fprintf(fp1, "%s %s\n", msg->status, msg->text);
    fflush(fp1);
    fclose(fp1);

    sort_messages(file_name0, file_name1);

    printf("[+]Message cached.\n");

}


/*
* Function:  update_ack
* ------------------------
* aggiorna lo stato dei messaggi ora visualizzati dal destinatario
*/
void update_ack(char* dest){

    FILE* fp;
    char buff[BUFF_SIZE];
    char fn[FILENAME_MAX];
    struct message* list = NULL;
    struct message* next;
    
    // ricavo il nome del file
    strcpy(fn, "./");
    strcat(fn, host_user);
    strcat(fn, "/cache/");
    strcat(fn, dest);
    strcat(fn, "_texts.txt");

    fp = fopen(fn, "r+");

    while( fgets(buff, BUFF_SIZE, fp)!=NULL ){

        printf("[+]Fetching cached messages.\n");

        char status[3];
        char text[MSG_LEN];
        struct message* cur_msg = (struct message*)malloc(sizeof(struct message));

        memset(status, 0, sizeof(status));
        memset(text, 0, sizeof(text));
        if (cur_msg==NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }

        sscanf(buff, "%s %[^\n]", status, text);
        
        // aggiorno lo stato dei messaggi
        if (strcmp(status, "*")==0){
            printf("[+]Updating message status.\n");
            strcpy(cur_msg->status, "**");
        }
        else{
            strcpy(cur_msg->status, status);
        }
       
        strcpy(cur_msg->text, text);
        cur_msg->next = NULL;

        if(list==NULL){
            list = cur_msg;
        }
        else{
            struct message* lastNode = list;
            while(lastNode->next != NULL)
                lastNode = lastNode->next;
            lastNode->next = cur_msg;
        }

    }

    // ricopio tutta la lista nel file
    rewind(fp);

    while (list!=NULL){

        fprintf(fp, "%s %s\n", list->status, list->text);
        next = list->next;
        free(list);
        list = next;
    }

    printf("[+]Cache updated.\n");

    fclose(fp);
}