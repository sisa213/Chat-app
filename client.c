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
#include "utility.c"


fd_set master;              // set principale gestito dal programmatore con le macro
fd_set read_fds;            // set di lettura gestito dalla select
int listener; // Socket per l'ascolto
bool SERVER_ON = false;         // tiene traccia del server
int server_sck;
int client_port;
char host_user[USER_LEN+1];
struct chat* ongoing_chats;
struct chat* current_chat = NULL;
struct con_peer* peers = NULL;
int grp_id = 0;



void menu_client(){

    system("clear");

    printf("***********************************");
    printf(" Bentornato! ");
    printf("***********************************\n");
    printf("Digita uno dei seguenti comandi:\n\n");
    printf("1)  hanging          -->  per visualizzare gli utenti che ti hanno inviato un messaggio mentre eri offline.\n");
    printf("2)  show {username}  -->  per visualizzare i messaggi pendenti da {username}.\n");
    printf("3)  chat {username}  -->  per avviare una chat con l'utente {username}.\n");
    printf("4)  out              -->  per disconnetersi. \n\n");
}


void add_to_con(struct con_peer **head, int sck, char* u)
{
    //create a new node
    struct con_peer *newNode = malloc(sizeof(struct con_peer));
    strcpy(newNode->username, u);
    newNode->socket_fd = sck;
    newNode->next = NULL;

    //if head is NULL, it is an empty list
    if(*head == NULL)
         *head = newNode;
    //Otherwise, find the last node and add the newNode
    else
    {
        struct con_peer *lastNode = *head;
        
        //last node's next address will be NULL.
        while(lastNode->next != NULL)
        {
            lastNode = lastNode->next;
        }

        //add the newNode at the end of the linked list
        lastNode->next = newNode;
    }
}


int setup_new_con(int peer_port, char* user){

    int peer_sck, ret;
    struct sockaddr_in peer_addr;

    /* Creazione socket */
    peer_sck = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(peer_sck, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    /* Creazione indirizzo del server */
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET ;
    peer_addr.sin_port = htons(peer_port);
    inet_pton(AF_INET, "127.0.0.1", &peer_addr.sin_addr);

    ret = connect(peer_sck, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
    
    if(ret==-1){
        close(peer_sck);
        printf("[-]User %s is offline.\n", user);
        return -1;
    }
    else{
        add_to_con( (struct con_peer**)peers, peer_sck, user);
        return peer_sck;
    }
}

int get_conn_peer(char* p){

    int sck = -1;
    struct con_peer* temp = peers;
    while (temp){
        if (strcmp(temp->username, p)==0){
            sck = temp->socket_fd;
            break;
        }
        temp = temp->next;   
    }
    return sck;
}


void first_ack_peer(int peer_sck, bool send_cmd){

    uint16_t lmsg, host_port;
    char cmd [CMD_SIZE+1] = "FAK";

    // invio info di chi sono ossia nome e porta
    // invio il comando
    if (send_cmd==true){
        send(peer_sck, (void*)cmd, CMD_SIZE+1, 0);
    }
    // invio il nome
    lmsg = strlen(host_user)+1;
    lmsg = htons(lmsg);
    send(peer_sck, (void*)&lmsg, sizeof(uint16_t), 0);
    send(peer_sck, (void*)host_user, strlen(host_user)+1, 0);
    // invio porta
    host_port = htons(client_port);
    send(peer_sck, (void*)&host_port, sizeof(uint16_t), 0);

}


/* Given a reference (pointer to pointer) to the head of a
   list and a key, deletes the first occurrence of key in
   linked list */
void remove_from_peers(struct con_peer** head_ref, char* key)
{
    // Store head node
    struct con_peer *temp = *head_ref, *prev;
 
    // If head node itself holds the key to be deleted
    if (temp != NULL && strcmp(temp->username, key)==0) {
        *head_ref = temp->next; // Changed head
        free(temp); // free old head
        return;
    }
 
    // Search for the key to be deleted, keep track of the
    // previous node as we need to change 'prev->next'
    while (temp != NULL && strcmp(temp->username, key)!=0) {
        prev = temp;
        temp = temp->next;
    }
 
    // If key was not present in linked list
    if (temp == NULL)
        return;
 
    // Unlink the node from linked list
    prev->next = temp->next;

    // chiudo il socket associato
    close(temp->socket_fd);
    // Tolgo il descrittore del socket connesso dal
    // set dei monitorati
    FD_CLR(temp->socket_fd, &master);
 
    free(temp); // Free memory
}


int check_contact_list(char* name){

    FILE* fptr;
    char buff[USER_LEN+1];
    char cur_name[USER_LEN+1];
    int cur_port;

    fptr = fopen("./contact_list.txt", "r");
    if (fptr==NULL){
        printf("[-]Error while opening contact list file.\n");
        return -1;
    }
    printf("[+]Contact list file correctly opened for reading.\n");

    while ( fgets( buff, sizeof buff, fptr ) != NULL )
    {
        sscanf (buff, "%s %d", cur_name, &cur_port);            

        if ( strcmp(cur_name, name) == 0 )
        {   printf("[+]Username found in contact list.\n");
            return cur_port;
        }
    }
    return -1;
}


int setup_conn_server(){

    int ret;
    struct sockaddr_in srv_addr;

    /* Creazione socket */
    server_sck = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_sck, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    
    /* Creazione indirizzo del server */
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(4242);
    inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr);
    
    /* Connessione */
    ret = connect(server_sck, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
    if(ret < 0){
        perror("[-]Server may be offline.\n");
        SERVER_ON = false;
        return -1;
    }
    SERVER_ON = true;
    return 1;
}


int send_message_to_server(struct message* msg){
    
    int ret; 
    uint16_t len;
    char cmd[CMD_SIZE+1] = "SOM";

    if (SERVER_ON==false){

        ret = setup_conn_server();
        if(ret==-1){
            return -1;
        }
    }

    ret = send(server_sck, (void*)cmd, CMD_SIZE+1, 0);
    if (ret<=0){
        printf("[-]Server may be offline.\n");
        SERVER_ON = false;
        return -1;
    }

    // invio prima il sender
    len = strlen(host_user)+1;
    len = htons(len);
    send(server_sck, (void*)&len, sizeof(uint16_t), 0);
    len = ntohs(len);
    send(server_sck, (void*)host_user, len, 0);

    // invio poi il receiver
    len = strlen(msg->recipient)+1;
    len = htons(len);
    send(server_sck, (void*)&len, sizeof(uint16_t), 0);
    len = ntohs(len);
    send(server_sck, (void*)msg->recipient, len, 0);

    // invio il timestamp
    send(server_sck, (void*)msg->time_stamp, TIME_LEN+1, 0);
    
    // invio il gruppo
    len = strlen(msg->group)+1;
    len = htons(len);
    send(server_sck, (void*)&len, sizeof(uint16_t), 0);
    len = ntohs(len);
    send(server_sck, (void*)msg->group, len, 0);

    // invio il messaggio
    len = strlen(msg->text)+1;
    len = htons(len);
    send(server_sck, (void*)&len, sizeof(uint16_t), 0);
    len = ntohs(len);
    send(server_sck, (void*)msg->text, len, 0);

    return 1;
}

int send_message_to_peer(struct message* m, char* user){

    int ret, sck, port;
    uint16_t len;
    char cmd[CMD_SIZE+1] = "SMP";

    sck = get_conn_peer(user);
    if (sck==-1){
        port = check_contact_list(user);
        sck = setup_new_con(port, user);
    }
    if (sck==-1) return -1;

    ret = send(sck, (void*)cmd,CMD_SIZE+1, 0);
    if (ret<=0){
        remove_from_peers((struct con_peer**)peers, user);
        printf("[-]Peer may be offline.\n");
        return -1;
    }

    // invio il gruppo
    len = strlen(m->group)+1;
    len = htons(len);
    ret = send(sck, (void*)&len, sizeof(uint16_t), 0);
    len = ntohs(len);
    send(sck, (void*)m->group, len, 0);

    // invio la data
    send(sck, (void*)m->time_stamp, TIME_LEN+1, 0);

    // invio il messaggio
    len = strlen(m->text)+1;
    len = htons(len);
    send(sck, (void*)&len, sizeof(uint16_t), 0);
    len = ntohs(len);
    send(sck, (void*)m->text, len, 0);
    printf("[+]Message sent.\n");

    return 1;

}

void preview_hanging(){

    int len;
    uint16_t lmsg;
    char presence [2];
    char message [BUFFER_SIZE+1];
    char command [CMD_SIZE+1] = "HNG";

    // prima si invia la richiesta del servizio
    printf("[+]Sending request to server.\n");
    send(server_sck, (void*) command, CMD_SIZE+1, 0);

    // poi invia il nome dello user
    len = strlen(host_user);
    lmsg = htons(len);
    printf("[+]Sending host name to server.\n");
    send(server_sck, (void*)&lmsg, sizeof(u_int16_t), 0);
    send(server_sck, (void*)host_user, lmsg, 0);

    // poi si riceve il responso se ci sono oppure no
    printf("[+]Receiving response from server.\n");
    recv(server_sck, (void*) &presence, RES_SIZE+1, 0);

    // se non ci sono stampare un semplice messaggio di avviso
    if (strcmp(presence,"E")==0){    // negative response
        
        recv(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
        len = ntohs(lmsg);
        recv(server_sck, (void*) message, BUFFER_SIZE, 0);
        fflush(stdout);
        printf("[+]Message from server:\n");
        printf(message);
        fflush(stdout);
        return;
    }
    else if(strcmp(presence,"S")==0){   // success response
        // altrimenti ricevere lunghezza del messaggio e il messaggio
        printf("\nMittente\tN° messaggi\tTimestamp\n");
        recv(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
        len = ntohs(lmsg);
        recv(server_sck, (void*) message, BUFFER_SIZE, 0);
        printf(message);
        fflush(stdout);
        // stampare il messaggio per 10 secondi
        return;
    }

}

void add_contact_list(char* name, int po){

    FILE* fptr;
    fptr = fopen("./contact_list.txt", "a");
    
    fprintf(fptr, "%s %d\n", name, po);
    fclose(fptr);
}


// si stabilisce una specie di cash per i messaggi con ogni utente
// ogni file avrà il nome user-chat.txt strutturato nella seguente maniera:
// esempio
// 2022-10-25 21:07:14 - sisa213:
// (*)Ciao il mio primo messaggio!
//
// 2022-10-25 23:01:32 - sisa213:
// (**)Considerami per favore :/
//
// 2022-10-25 23:01:32 sisa2130 piko99:
// -Considerami per favore :/

void show_history(char* user){

    FILE* fp, *fp1;
    char file_name[USER_LEN+20];
    char file_name1[USER_LEN+20];
    char buff_info[DIM_BUF];
    char buff_chat[MSG_BUFF];

    // getting file names
    strcpy(file_name, "./cache/");
    strcat(file_name, user);
    strcat(file_name, "_info.txt");

    strcpy(file_name1, "./cache/");
    strcat(file_name1, user);
    strcat(file_name1, "_texts.txt");
                                  
    if ( (fp = fopen(file_name,"r"))==NULL || (fp1 = fopen(file_name1, "r"))==NULL){
        perror("[-]Error opening users files");
        return;
    }
    printf("[+]Chat files correctly opened.\n ");

    while( fgets(buff_info, DIM_BUF, fp)!=NULL && fgets(buff_chat, MSG_BUFF, fp1)!=NULL ) {

        printf(buff_info);
        printf("\n");
        printf(buff_chat);
        printf("\n");
    }
  
    fclose(fp);
    fclose(fp1);
}


void store_message(struct message* msg){

    FILE* fp, *fp1;

    fp = fopen("./buffer_info.txt", "a");
    fp1 = fopen("./buffer_texts.txt", "a");

    fprintf(fp, "%s %s %s\n", msg->time_stamp, msg->recipient, msg->group);
    fprintf(fp1, "%s\n", msg->text);

    fclose(fp);
    fclose(fp1);
}


void show_online_users(){

    uint16_t lmsg;
    char buffer [BUFFER_SIZE];
    char command [CMD_SIZE+1] = "AOL";

    // prima si invia la richiesta del servizio
    printf("[+]Sending request to server.\n");
    send(server_sck, (void*) command, CMD_SIZE+1, 0);

    // receive response
    printf("[+]Receiving response from server.\n");
    recv(server_sck, (void*)&lmsg, sizeof(uint16_t), 0);
    lmsg = ntohs(lmsg);
    recv(server_sck, (void*)buffer, lmsg, 0);
    
    //stampo
    printf(buffer);
    printf("\n");
}




int ask_server_con_peer(char* user){

    int sck;
    uint16_t lmsg, rec_port;
    char response[RES_SIZE+1];
    char buffer[DIM_BUF];
    
    // invio il comando "GPC" (get_peer_connection)
        char cmd[CMD_SIZE+1] = "GPC";
        if (send(server_sck, (void*)cmd, CMD_SIZE+1, 0)<=0){
            printf("[-]Server is offline.\n");
            return 0; 
        }

        // invio user 
        lmsg = strlen(user)+1;
        lmsg = htons(lmsg);
        send(server_sck, (void*)&lmsg, sizeof(uint16_t), 0);
        lmsg = ntohs(lmsg);
        send(server_sck, (void*)user, lmsg, 0);

        // receive response from server
        recv(server_sck, (void*)response, RES_SIZE+1, 0);
           
        if (strcmp(response,"E")==0){    // negative response
        
            recv(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
            lmsg = ntohs(lmsg);
            recv(server_sck, (void*) buffer, BUFFER_SIZE, 0);
            printf("[+]Message from server:\n");
            printf(buffer);
            return -1;
        }
        else{// se ho una risposta positiva significa che lo user è online
            // ricevo porta 
            recv(server_sck, (void*)&rec_port, sizeof(uint16_t), 0);
            rec_port = ntohs(rec_port);
            // e stabilisco connessione
            add_contact_list(user, rec_port);
            sck = setup_new_con(rec_port, user);
        }

    return sck;
}


int add_member(char *user){

    uint16_t counter, len;
    char type[CMD_SIZE];
    char cmd[CMD_SIZE] = "ATG";     // add to group
    int sck_user, port;
    struct con_peer* new_member;
    struct con_peer* temp;

    // ottengo la connessione da quel server
    // controllo prima se è presente tra le con_peer
    sck_user = get_conn_peer(user);

    if (sck_user==-1){
        // controllo se si tratta di un contatto nuovo
        port = check_contact_list(user);
        if (port ==-1){
            sck_user = ask_server_con_peer(user);
            if (sck_user==-1)
                return -1;
            else{
                // invio il comando
                send(sck_user, (void*)cmd, CMD_SIZE+1, 0);
                // invia "new"
                strcpy(type, "new");
                send(sck_user, (void*)type, CMD_SIZE+1, 0);
                first_ack_peer(sck_user,false);
            }
        }
        else{
            // invio il comando
            send(sck_user, (void*)cmd, CMD_SIZE+1, 0);
            // invia "old"
            strcpy(type, "old");
            send(sck_user, (void*)type, CMD_SIZE+1, 0);
            sck_user = setup_new_con(port, user);
            if (sck_user==-1) return -1;
        }
    }else{
        // invio il comando
        send(sck_user, (void*)cmd, CMD_SIZE+1, 0);
        strcpy(type, "old");
        send(sck_user, (void*)type, CMD_SIZE+1, 0);
    }


    // se è il primo utente aggiunto al gruppo (i.e. terzo nella chat)
    if (strcmp(current_chat->group, "-")==0){

        char num[2];

        struct con_peer* m1 = malloc(sizeof(struct con_peer));  
        struct con_peer* m2 = malloc(sizeof(struct con_peer));
        strcpy(current_chat->group, host_user);
        sprintf(num, "%d", grp_id);
        strcat(current_chat->group, num);

        grp_id++;

        // aggiungo ai membri i primi due del gruppo
        m1->socket_fd = -1;
        strcpy(m1->username, host_user);

        m2->socket_fd = get_conn_peer(current_chat->recipient);
        strcpy(m2->username, current_chat->recipient);
        m2->next = NULL;

        m1->next = m2;
        current_chat->members = m1;
    }
    
    new_member = (struct con_peer*)malloc(sizeof(struct con_peer));
    strcpy(new_member->username, user);
    new_member->socket_fd = sck_user;
    new_member->next = current_chat->members;
    current_chat->members = new_member;

    printf("[+]New member %s added to group.\n", user);

    // invio nome del gruppo
    len = strlen(current_chat->group)+1;
    len = htons(len);
    send(sck_user, (void*)&len, sizeof(uint16_t), 0);
    len = ntohs(len);
    send(sck_user, (void*)&current_chat->group, len, 0);

    // invio info (user e porta) degli altri membri del gruppo
    counter = htons((current_chat->users_counter)-1);
    send(sck_user, (void*)&counter, sizeof(uint16_t), 0);

    temp = current_chat->members;

    printf("[+]Sending info on group members to new member.\n");
    for (; temp; temp = temp->next){

        // invio username
        len = strlen(temp->username)+1;
        len = htons(len);
        send(sck_user, (void*)&len, sizeof(uint16_t), 0);
        len = ntohs(len);
        send(sck_user, (void*)&temp->username, len, 0);

        // invio porta
        len = check_contact_list(temp->username);
        len = htons(len);
        send(sck_user, (void*)&len, sizeof(uint16_t), 0);
    }

    current_chat->users_counter++;

    printf("[+]New user %s added to group.\n", user);

    return 1;

}

void print_message(struct message* msg, bool sent, bool mine){

    char status[3];
    
    printf("\n");

    printf("%s %s %s\n", msg->time_stamp, msg->group, msg->sender);

    if (mine==true){
        if (sent==true){
            strcpy(status, "**");
        }
        else{
            strcpy(status, "*");
        }
    }
    
    printf("(%s) %s\n", status, msg->text);

    printf("\n");
    
}

// ordina la chat in base al timestamp
void sort_messages(char* id){

    FILE *fp, *fp1;
    char file_name[USER_LEN+20];
    char file_name1[USER_LEN+20];
    char buff_info[DIM_BUF];
    char buff_chat[MSG_BUFF];
    struct message* new_list = NULL;
    struct message* temp;

    // getting file names
    strcpy(file_name, "./cache/");
    strcat(file_name, id);
    strcat(file_name, "_info.txt");

    strcpy(file_name1, "./cache/");
    strcat(file_name1, id);
    strcat(file_name1, "_texts.txt");

    if ( (fp = fopen(file_name,"r"))==NULL || (fp1 = fopen(file_name1, "r"))==NULL){
        perror("[-]Error opening users files");
        return;
    }
    printf("[+]Chat files correctly opened.\n ");

    while( fgets(buff_info, DIM_BUF, fp)!=NULL && fgets(buff_chat, MSG_BUFF, fp1)!=NULL ) {

        struct message* new_msg = malloc(sizeof(struct message));

        sscanf (buff_info, "%s %s %s", new_msg->time_stamp, new_msg->group, new_msg->sender);
        sscanf (buff_chat, "%s %s", new_msg->status, new_msg->text);
        
        insert_sorted(new_list, new_msg);
    }

    fp = fopen(file_name, "w");
    fp1 = fopen(file_name1, "w");

    temp = new_list;
    while (new_list){

        new_list = new_list->next;

        fprintf(fp, "%s %s %s\n", temp->time_stamp, temp->group, temp->sender);
        fprintf(fp1, "%s %s\n", temp->status, temp->text);

        free(temp);
        temp = new_list;
    }

    fclose(fp);
    fclose(fp1);

}


void save_message(struct message* msg, bool sent, bool mine){

    FILE* fp, *fp1;
    char status[3];
    char file_name[USER_LEN+20];
    char file_name1[USER_LEN+20];

    // getting cache_info
    strcpy(file_name, "./cache/");
    if (strcmp(msg->group, "-")!=0){
        strcat(file_name, msg->group);
    }
    else{
        strcat(file_name, msg->recipient);
    }
    strcat(file_name, "_info.txt");

    // getting cache_texts
    strcpy(file_name1, "./cache/");
    if (strcmp(msg->group, "-")!=0){
        strcat(file_name, msg->group);
    }
    else{
        strcat(file_name, msg->recipient);
    }
    strcat(file_name, "_texts.txt");

    fp = fopen(file_name, "a");
    fp1 = fopen(file_name1, "a");

    fprintf(fp, "%s %s %s\n", msg->time_stamp, msg->group, msg->sender);

    if (mine==true){
        if (sent==true){
            strcpy(status, "**");
        }
        else{
            strcpy(status, "*");
        }
    }
    else{
        strcpy(status, "-");
    }
    
    fprintf(fp1, "%s %s\n", status, msg->text);

    fclose(fp);
    fclose(fp1);

    // sort messages by timestamp
    if (strcmp(msg->group, "-")==0)
        sort_messages(msg->recipient);
    else sort_messages(msg->group);
}

void chat_handler(){
    // controllare input se uno dei comandi agire di conseguenza
    // altrimenti inviare messaggio al server
    // e a seconda del responso salvarli come recapitati oppure no nel file di archivio della chat
    char buffer [MSG_LEN+1];
    char timestamp [TIME_LEN+1];
    int ret, sck;
    char *token;
    struct message* new_msg = malloc(sizeof(struct message));
    char* b = buffer;
    size_t dimbuf = MSG_LEN;


    getline(&b, &dimbuf, stdin);
        
    if (strcmp(b, "\\q\n")==0){     // richiesta di chiusura della chat
        printf("[+]Chat correctly closed.\n");
        current_chat->on = false;
        menu_client();
        return;
    }
    else if (strcmp(b, "\\u\n")==0){    // richiesta della lista degli utenti online
        
        show_online_users();
    }    
    else if ( strncmp(b, "\\a ", 3)==0 ){
        // richiesta di aggiungere un nuovo membro alla chat

        //ricavo il nome dello user
        buffer[strcspn(buffer, "\n")] = '\0';
        token = strtok(buffer, " ");
        token = strtok(NULL, " ");
        // aggiungo l'utente scelto alla conversazione 
        add_member(token);
    }
    else{
            // preparo il messaggio
            time_t now = time(NULL);
            strftime(timestamp, TIME_LEN, "%Y-%m-%d %H:%M:%S", localtime(&now));
            // create new message struct
            strcpy(new_msg->sender, host_user);
            strcpy(new_msg->recipient, current_chat->recipient);
            strcpy(new_msg->group, "");
            // associo il timestamp
            strcpy(new_msg->time_stamp, timestamp);
            // salvo il testo
            new_msg->m_len = strlen(b);
            strcpy(new_msg->text, b);

            // singola chat
            if (strcmp(current_chat->group, "-")==0){
                
                if( current_chat->sck == -1){   // primo messaggio della chat

                    int porta = check_contact_list(current_chat->recipient);

                    if (porta==-1){   // nuovo utente

                        sck = ask_server_con_peer(current_chat->recipient);
                        if (sck==0){    // server is offline
                            store_message(new_msg);
                            printf("\33[2K\r");
                            print_message(new_msg, false, true);
                            save_message(new_msg, false, true);
                        }
                        else if (sck==-1){  // lo user è offline ma il server non lo è
                            send_message_to_server(new_msg);
                            printf("\33[2K\r");
                            print_message(new_msg, false, true);
                            save_message(new_msg, false, true);
                        }
                        else{
                            current_chat->sck = sck;
                            first_ack_peer(sck, true);
                            // invio il messaggio al peer
                            send_message_to_peer(new_msg, current_chat->recipient);
                            save_message(new_msg, true, true);
                            printf("\33[2K\r");
                            print_message(new_msg, true, true);
                        }
                    }
                    else{   // utente già noto  
                        sck = setup_new_con(porta, current_chat->recipient);
                        if (sck==-1){   // utente offline
                                store_message(new_msg);
                                save_message(new_msg, false, true);
                                printf("\33[2K\r");
                                print_message(new_msg, false, true);
                        }
                        else{   // utente online
                            current_chat->sck = sck;
                            first_ack_peer(sck, true);
                            // invio il messaggio al peer
                            send_message_to_peer(new_msg, current_chat->recipient);
                            save_message(new_msg, true, true);
                            printf("\33[2K\r");
                            print_message(new_msg, true, true);
                        }
                    }
                }
                else{   // ennesimo messaggio della chat (istanza)
                    // invio il messaggio al peer
                    ret = send_message_to_peer(new_msg, current_chat->recipient);
                    if (ret==-1){
                        ret = send_message_to_server(new_msg);
                        if (ret==-1)    // server_offline
                            store_message(new_msg);
                        printf("\33[2K\r");
                        print_message(new_msg, false, true);
                        save_message(new_msg, false, true);
                    }
                    else{
                        printf("\33[2K\r");
                        print_message(new_msg, true, true);
                        save_message(new_msg, true, true);
                    }                    
                }   
            }
            else{   // messaggio di gruppo

                struct con_peer* temp = current_chat->members;
                strcpy(new_msg->group, current_chat->group);

                while(temp){
                    if (temp->socket_fd!=-1){   // evito il mittente
                        
                        strcpy(new_msg->recipient, temp->username);
                        // dichiaro prima che si tratta di un gruppo
                        ret = send_message_to_peer(new_msg, temp->username);
                        if (ret==-1){
                            ret = send_message_to_server(new_msg);
                            if (ret==-1)    // server offline
                                store_message(new_msg);
                        }
                    }
                    temp = temp->next;
                }
                save_message(new_msg, true, true);
            }
    }
} 


char* get_name_from_sck(int s){

    struct con_peer* temp = peers;
    while (temp){
        if (temp->socket_fd==s){
            return temp->username;
        }
        temp = temp->next;
    }
    return NULL;
}


void start_chat(char* user){

    if (strlen(user)>50){
        printf("[-]Username doesn't exist.\n");
        return;
    }

    // se l'utente è presente in rubrica stampiamo la cache(user potrebbe essere anche l'id di un gruppo)
    if (check_contact_list(user)!=-1)
        show_history(user);    

    current_chat = (struct chat*)malloc(sizeof(struct chat)); 
    
    strcpy(current_chat->group, "-");
    current_chat->sck = -1;
    strcpy(current_chat->recipient, user);
    current_chat->on = true;
    current_chat->users_counter = 2;

}


void logout(){

    FILE* fp;
    int ret;
    char timestamp[TIME_LEN+1];
    char cmd[CMD_SIZE+1] = "LGO";

    // salvo timestamp corrente
    time_t now = time(NULL);
    strftime(timestamp, TIME_LEN, "%Y-%m-%d %H:%M:%S", localtime(&now));

    // invio comando al server
    ret = send(server_sck, (void*)cmd, CMD_SIZE+1, 0);

    if(ret<=0){ // se offline
        printf("[-]Server is offline.\n");
        fp = fopen("./last_log.txt", "a");
        fp = fopen("./last_log.txt", "w");
        fprintf(fp, "%s", timestamp);
    }

    close(listener);
    FD_CLR(listener, &master);

    send(server_sck, (void*)timestamp, TIME_LEN+1, 0);
    close(server_sck);
}
        

void show_user_hanging(char* user){

    int len, mess_counter;
    uint16_t lmsg, many;
    char message [BUFFER_SIZE+1];
    char command [CMD_SIZE+1] = "SHW";

    // prima si invia la richiesta del servizio
    printf("[+]Sending request to server.\n");
    send(server_sck, (void*) command, CMD_SIZE+1, 0);

    // invia nome del destinatario  al server
    printf("[+]Sending host name to server.\n");
    len = strlen(host_user);
    lmsg = htons(len);
    send(server_sck, (void*)&lmsg, sizeof(u_int16_t), 0);
    send(server_sck, (void*)host_user, lmsg, 0);

    // invia nome del mittente al server
    printf("[+]Sending sender name to server.\n");
    len = strlen(user)+1;
    lmsg = htons(len);
    send(server_sck, (void*)&lmsg, sizeof(u_int16_t), 0);
    send(server_sck, (void*)user, lmsg, 0);

    // ricevi responso del numero di messaggi dal server
    printf("[+]Receiving number of messages from server.\n");
    recv(server_sck, (void*)&many, sizeof(u_int16_t), 0);
    mess_counter = ntohs(many);
    if (mess_counter==0){
        printf("\nNon ci sono messaggi pendenti da %s\n", user);
        return;
    }

    while (mess_counter>0){

        struct message* m = malloc(sizeof(struct message));

        recv(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
        len = ntohs(lmsg);
        recv(server_sck, (void*) message, BUFFER_SIZE, 0);

        sscanf (message, "%s %s %s", m->time_stamp, m->group, m->sender);
        strcpy(m->status, "-");

        recv(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
        len = ntohs(lmsg);
        recv(server_sck, (void*) message, BUFFER_SIZE, 0);
        strcpy(m->text, message);

        // aggiungere i messaggi nella cache
        save_message(m, true, false);
        print_message(m, true, false);
    }

}

void command_handler(){

    char buffer[DIM_BUF];
    char *b = buffer;
    char *token0, *token1;
    size_t dimbuf = DIM_BUF;

    getline(&b, &dimbuf, stdin);
    printf("\n");

    buffer[strcspn(buffer, "\n")] = '\0';

    //get tokens
    token0 = strtok(buffer, " ");
    token1 = strtok(NULL, " ");

    //check if first word is valid or not
    if (strcmp(token0, "hanging")==0){
        preview_hanging();
    }
    else if(strcmp(token0, "show")==0){
        show_user_hanging(token1);
    }
    else if(strcmp(token0, "chat")==0){
        start_chat(token1);
    }
/*     else if(strcmp(token0, "share")==0){
        file_handler(token1);
    } */
    else if(strcmp(token0, "out")==0){
        logout();       //deve terminare con exit(1)
    }
    else{
        printf("[-]Invalid command! Please try again.\n");
        fflush(stdout);
        sleep(2);
        menu_client();
        command_handler();
    }

}

void check_send_log_time(){

}



void encrypt(char password[],int key)
{
    unsigned int i;
    for(i=0;i<strlen(password);++i)
    {
        password[i] = password[i] - key;
    }
}


int signup(char* user, char* psw){
    
    int len, sv_status;
    uint16_t lmsg;
    char response [RES_SIZE+1];
    char message [BUFFER_SIZE];
    char command [CMD_SIZE+1] = "SGU";
    
    encrypt(psw, CRYPT_SALT);

    // check parameters length
    if (strlen(user)>=50 || strlen(psw)>=50)
        return -1;

    // connect to server
    sv_status = setup_conn_server();
    if(sv_status==-1){
            if (close(server_sck)==-1){
                perror("[-]ERROR CLOSING SERVER SOCKET");
            }
            return -1;
    }

    printf("[+]Submitted credentials: user: %s, encrypted psw: %s\n", user, psw);

    // send request to server
    printf("[+]Sending request to server.\n");
    send(server_sck, (void*) command, CMD_SIZE+1, 0);

    // send user
    len = strlen(user)+1;
    lmsg = htons(len);
    send(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
    send(server_sck, (void*) user, len, 0);

    // send psw
    len = strlen(psw)+1;
    lmsg = htons(len);
    send(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
    send(server_sck, (void*) psw, len, 0);

    // receive response from server
    recv(server_sck, (void*)response, RES_SIZE+1, 0);

    if (strcmp(response,"E")==0){    // negative response
        
        recv(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
        len = ntohs(lmsg);
        recv(server_sck, (void*) message, BUFFER_SIZE, 0);
        close(server_sck);
        fflush(stdout);
        printf("[+]Message from server:\n");
        printf(message);
        fflush(stdout);
        return -1;
    }
    else if(strcmp(response,"S")==0){   // success response
        printf("[+]Account succesfully created.\n");
        close(server_sck);
        return 1;
    }

    return 1;
}

void receive_first_ack(){
    //                                                                       da definire|||||||||||||||||
}

void add_group(int sck){

    FILE *fp, *fp1;
    char type[CMD_SIZE+1];
    char fn[USER_LEN+20];
    char fn1[USER_LEN+20];
    uint16_t members_counter, len;
    struct chat* group_chat = malloc(sizeof(struct chat));

    // ricevo il tipo del creatore del gruppo
    send(sck, (void*)type, CMD_SIZE+1, 0);

    if (strcmp(type, "new")==0){
        receive_first_ack(sck);
    }

    // ricevo il nome del gruppo
    recv(sck, (void*)&len, sizeof(uint16_t), 0);
    len = ntohs(len);
    recv(sck, (void*)group_chat->group, len, 0);

    // ricevo il numero dei membri del gruppo (nota il numero non include né il creatore né il nuovo membro)
    recv(sck, (void*)&members_counter, sizeof(uint16_t), 0);
    members_counter = ntohs(members_counter);

    while (members_counter>0){

        uint16_t port;
        
        struct con_peer* member = malloc(sizeof(struct con_peer));
        // ricevo username
        recv(sck, (void*)&len, sizeof(uint16_t), 0);
        len = ntohs(len);
        recv(sck, (void*)member->username, len, 0);

        // ricevo porta
        recv(sck, (void*)&port, sizeof(uint16_t), 0);

        member->socket_fd = get_conn_peer(member->username);
         
        member->next = group_chat->members;
        group_chat->members = member;
    }

    if (ongoing_chats==NULL){
        ongoing_chats = group_chat;
    }
    else{
        group_chat->next = ongoing_chats;
        ongoing_chats = group_chat;
    }

    // creo due nuovi file per i nuovi messaggi
    strcpy(fn, "./cache/");
    strcpy(fn, group_chat->group);
    strcpy(fn, "_info.txt");
    
    strcpy(fn1, "./cache/");
    strcpy(fn1, group_chat->group);
    strcpy(fn1, "_texts.txt");

    fp = fopen(fn, "a");
    fp1 = fopen(fn1, "a");

    fclose(fp);
    fclose(fp1);

}

int login(char* user, char* psw){

    int len, sv_status;
    uint16_t lmsg, port;
    char response [RES_SIZE+1];
    char message [BUFFER_SIZE];
    char command [CMD_SIZE+1] = "LGI";
    
    encrypt(psw, CRYPT_SALT);

    // connect to server
    sv_status = setup_conn_server();
    if(sv_status==-1){
        close(server_sck);
        return -1;
    }

    // send request to server
    send(server_sck, (void*) command, CMD_SIZE+1, 0);

    // send user
    len = strlen(user)+1;
    lmsg = htons(len);
    send(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
    send(server_sck, (void*) user, len, 0);

    // send psw
    len = strlen(psw)+1;
    lmsg = htons(len);
    send(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
    send(server_sck, (void*) psw, len, 0);

    // send client port
    port = htons(client_port);
    send(server_sck, (void*) &port, sizeof(uint16_t), 0);

    // receive response from server
    recv(server_sck, (void*) &response, RES_SIZE+1, 0);

    if (strcmp(response,"E")==0){    // negative response
        
        recv(server_sck, (void*) &lmsg, sizeof(uint16_t), 0);
        len = ntohs(lmsg);
        recv(server_sck, (void*) message, BUFFER_SIZE, 0);
        printf(message);
        return -1;
    }
    else if(strcmp(response,"S")==0){   // success response
        strcpy(host_user, user);
        return 0;
    }
    else {
        printf("!!!!!!!!!!!!!!!Errore nel response del server.\n");
        return -1;
    }

}


void receive_message_handler(int sck){

    uint16_t len;
    struct message* new_msg = malloc(sizeof(struct message));

    // ricevo il gruppo
    recv(sck, (void*)&len, sizeof(uint16_t), 0);
    len = ntohs(len);
    recv(sck, (void*)&new_msg->group, len, 0);

    // ricevo la data
    recv(sck, (void*)new_msg->time_stamp, TIME_LEN+1, 0);

    // ricevo il messaggio
    recv(sck, (void*)&len, sizeof(uint16_t), 0);
    len = ntohs(len);
    recv(sck, (void*)new_msg->text, len, 0);

    // ricavo il sender
    strcpy(new_msg->sender, get_name_from_sck(sck));

    printf("[+]New message received.\n");

    // save message
    save_message(new_msg, true, false);

    // mostro la notifica se non è la chat di interesse
    if( current_chat->on==false || 
        ( strcmp(current_chat->group, "-")==0 && strcmp(current_chat->recipient, new_msg->sender)!=0) ||
        ( strcmp(current_chat->group, "-")!=0 && strcmp(current_chat->group, new_msg->group)!=0 )
    ){
        printf("\n Hai ricevuto un nuovo messaggio da %s.\n", new_msg->sender);
        sleep(2);
        printf("\33[2K\r");
    }
    else{
        print_message(new_msg, true, false);
    }

}

void home_client(){

    system("clear");
    printf("Benvenuto! Digita uno dei seguenti comandi:\n");
    printf("\n1) signup {username} {password}     -->  per registrarsi al servizio.");
    printf("\n2) in 4242 {username} {password}    -->  per effettuare il login.\n\n");
}

void enter_handler(){

    char buffer [DIM_BUF];
    char* b = buffer;
    char* token0, *token1, *token2, *token3;
    size_t dimbuf = DIM_BUF;


    getline(&b, &dimbuf, stdin);
    printf("\n");

    buffer[strcspn(buffer, "\n")] = '\0';

    //get tokens
    token0 = strtok(buffer, " ");
    token1 = strtok(NULL, " ");
    token2 = strtok(NULL, " ");
    token3 = strtok(NULL, " ");

    //check if first word is valid or not
    if (strcmp(token0, "signup")==0){

        if ( token1 && token2 ){
            if (signup(token1, token2)==-1){
                sleep(2);
            }
            else{
                printf("[+]Signup succeeded");
                printf("\n\nFai login con le tue nuove credenziali.\n");
                fflush(stdout);
                sleep(6);
            }
        }
        else{
            printf("[-]Please insert username and password after 'signup'.\n");
            fflush(stdout);
            sleep(2);
        }
        home_client();
        enter_handler();
    }
    else if(strcmp(token0, "in")==0){

        if ( token1 && token2 && token3){
            if (login(token2, token3)==-1){
                printf("[-]Login failed.\n");
                fflush(stdout);
                sleep(2);
                home_client();
                enter_handler();
            }
            else{
                printf("[+]Login succeeded.\n\n");
                sleep(3);
            }
        }
        else{
            printf("[-]Please insert port, username and password after 'in'.\n");
            fflush(stdout);
            sleep(2);
            home_client();
            enter_handler();
        }
    }
    else{
        printf("[-]Invalid command! Please try again.\n");
        fflush(stdout);
        sleep(2);
        home_client();
        enter_handler();
    }
}


void server_peers(){

    int fdmax; // Numero max di descrittori
    struct sockaddr_in sv_addr; // Indirizzo server
    struct sockaddr_in cl_addr; // Indirizzo client
    int newfd; // Socket di comunicazione
    char cmd[CMD_SIZE+1]; // Buffer di applicazione
    int nbytes;
    socklen_t addrlen;
    int i;

    /* Azzero i set */
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    
    sv_addr.sin_family = AF_INET;
    // INADDR_ANY mette il server in ascolto su tutte le
    // interfacce (indirizzi IP) disponibili sul server
    sv_addr.sin_addr.s_addr = INADDR_ANY;
    sv_addr.sin_port = htons(20000);
    bind(listener, (struct sockaddr*)& sv_addr, sizeof(sv_addr));
    listen(listener, 10);
    
    FD_SET(listener, &master);      // aggiungo il listener al set dei socket monitorati
    FD_SET(0,&master);              // aggiungo anche stdin
    FD_SET(server_sck, &master);    // aggiungo anche il socket del server

    menu_client();
    
    // Tengo traccia del maggiore (ora è il listener)
    fdmax = listener;
    for(;;){
        read_fds = master; // read_fds sarà modificato dalla select

        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("[-]Error in select");
            exit(4);
        }
        printf("[+]Select worked.\n");


        for(i=0; i<=fdmax; i++) { // f1) Scorro il set



            if(FD_ISSET(i, &read_fds)) { // i1) Trovato desc. pronto

                if(i==0){
                    if (current_chat!=NULL && current_chat->on){
                        chat_handler();
                    }
                    else{
                        command_handler();
                    }
                }
                else if(i == listener) { // i2) È il listener

                    addrlen = sizeof(cl_addr);
                    newfd = accept(listener, (struct sockaddr *)&cl_addr, &addrlen);
                    FD_SET(newfd, &master); // Aggiungo il nuovo socket
                    if(newfd > fdmax){ fdmax = newfd; } // Aggiorno fdmax
                }
                else { // Il socket connesso è pronto

                /* i messaggi che posso ricevere vanno distinti a seconda che provenagano dal server
                oppure dal socket, se provengono da altri peers sono o primi messaggi oppure ennesimi
                messaggi oppure richieste di aggiunta ad una conversazione (nel caso di gruppo o di 
                chat regolare). */

                    nbytes = recv(i, (void*)cmd, CMD_SIZE+1, 0);
                    if (nbytes<=0){
                        remove_from_peers((struct con_peer**)peers, get_name_from_sck(i));
                    }
                    else{
                        if (strcmp(cmd, "SMP")==0){
                            receive_message_handler(i);
                        }
                        else if (strcmp(cmd, "ATG")==0){
                            add_group(i);
                        }
                        else if(strcmp(cmd, "FAK")==0){
                            receive_first_ack(i);
                        }
                        else{
                            printf("[-]Abnormal request %s received.\n", cmd);
                        }
                    }
                }
            } // Fine if i1
        } // Fine for f1
    } // Fine for(;;)
    return;
}


int main(int argc, char* argv[]){

    if (argc > 1) {
        client_port = atoi(argv[1]);
    }
    else {
        printf("[-]No port specified. Please insert a port number.\n\n");
        return -1;
    }

    home_client();
    enter_handler();

    /* da inviare dentro la setup del server
    send_last_log();
    send_stored_messages_to_server();
    receive_acks(); */
    server_peers();

    return 0;

}
