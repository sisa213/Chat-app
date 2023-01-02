/*
* Chat-app: applicazione network di messaggistica istantanea. 
*
* client.c:   implementazione del client(device) della rete. 
*
* Samayandys Sisa Ruiz Muenala, 10 novembre 2022
*
*/


#include "utility_d.c"


fd_set master;                      // set master
fd_set read_fds;                    // set di lettura per la select
int fdmax;                          // numero max di descrittori
int listener;                       // socket per l'ascolto
int server_sck;                     // socket del server
int client_port;                    // porta del client
char host_user[USER_LEN+1];         // nome utente del client
struct chat* ongoing_chats;         // lista delle chat attive nell'attuale sessione
struct chat* current_chat = NULL;   // ultima chat visualizzata
struct con_peer* peers = NULL;      // lista delle connessioni degli utenti con cui si è avviata una chat
bool SERVER_ON = false;             // tiene traccia dello stato del server


/*
 * Function:  home__client
 * -----------------------
 * mostra il menù iniziale del device
 */
void home_client(){

    system("clear");
    printf("***********************************");
    printf(" APP STARTED ");
    printf("***********************************\n");
    printf("Benvenuto! Digita uno dei seguenti comandi:\n");
    printf("\n1)  signup {username} {password}         -->  per registrarsi al servizio");
    printf("\n2)  in {srv_port} {username} {password}  -->  per effettuare il login\n");
}


/*
 * Function:  menu_client
 * -----------------------
 * mostra il menù iniziale dell'utente che ha eseguito il login
 */
void menu_client(){

    system("clear");

    printf("***********************************");
    printf(" Bentornato! ");
    printf("***********************************\n");
    printf("Digita uno dei seguenti comandi:\n\n");
    printf("1)  hanging          -->  per visualizzare gli utenti che ti hanno inviato un messaggio mentre eri offline\n");
    printf("2)  show {username}  -->  per visualizzare i messaggi pendenti da {username}\n");
    printf("3)  chat {username}  -->  per avviare una chat con l'utente {username}\n");
    printf("4)  out              -->  per disconnetersi\n");
}


/*
 * Function:  remove_from_peers
 * ------------------------------
 * elimina la connessione del peer avente username 'key' dalla lista peers e dal master set
 */
void remove_from_peers(char* key)
{
    struct con_peer ** head_ref = (struct con_peer**)peers;
    struct con_peer *temp = *head_ref, *prev;
 
    if (temp != NULL && strcmp(temp->username, key)==0) {
        *head_ref = temp->next; 
        free(temp); 
        return;
    }
 
    while (temp != NULL && strcmp(temp->username, key)!=0) {
        prev = temp;
        temp = temp->next;
    }

    if (temp == NULL)
        return;
 
    prev->next = temp->next;

    // chiudo il socket associato
    close(temp->socket_fd);
    // rimuovo il descrittore del socket connesso dal set dei monitorati
    FD_CLR(temp->socket_fd, &master);
 
    free(temp); 
}


/*
* Function: send_last_log
* --------------------------
* invia se esiste l'ultimo logout salvato al server
*/
void send_last_log(){

    FILE *fp;
    char timestamp[TIME_LEN+1];
    char cmd[CMD_SIZE+1] = "LGO";

    fp = fopen("./last_logout.txt", "r");
    if (fp == NULL){
        printf("[-]No cached logout to send.\n");
        return;
    }
    // ottengo il timestamp
    fgets(timestamp, TIME_LEN+1, fp);
    fclose(fp);

    remove("./last_logout.txt");

    // invio comando al server
    send(server_sck, (void*)cmd, CMD_SIZE+1, 0);

    // invio il timestamp al server
    send(server_sck, (void*)timestamp, TIME_LEN+1, 0);

    printf("[+]Cached logout timestamp sent to server.\n");
}


/*
* Function: setup_new_con
* ---------------------------
* instaura una connessione TCP con user sulla porta 'peer_port'
*/
int setup_new_con(struct con_peer* p, int peer_port, char* user){

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
        FD_SET(peer_sck, &master);    // aggiungo il socket al master set
        add_to_con( (struct con_peer**)p, peer_sck, user);
        return peer_sck;
    }
}


/*
* Function: store_message
* ----------------------------
* funzione invocata ogni qualvolta devo inviare un messaggio ad un destinatario offline
* e anche il server risulta offline. Tale messaggio è salvato in file locali.
*/
void store_message(struct message* msg){

    FILE* fp, *fp1;

    // memorizzo in file il messaggio da inviare
    fp = fopen("./buffer_info.txt", "a");
    fp1 = fopen("./buffer_texts.txt", "a");

    fprintf(fp, "%s %s %s\n",  msg->time_stamp, host_user, msg->recipient);
    fprintf(fp1, "%s", msg->text);

    fclose(fp);
    fclose(fp1);
}


/*
* Function: send_offline_message
* ---------------------------------
* se il server è online invia un messaggio destinato ad un utente offline perché venga salvato,
* altrimenti lo salva localmente.
*/
void send_offline_message(struct message* msg){
    
    int ret; 
    char cmd[CMD_SIZE+1] = "SOM";

    ret = send(server_sck, (void*)cmd, CMD_SIZE+1, 0);
    if (ret<=0){    // se ci sono problemi nell'invio dei dati
        // suppongo che il server sia offline
        printf("[-]Server may be offline.\n");
        close(server_sck);
        FD_CLR(server_sck, &master);
        SERVER_ON = false;
        store_message(msg);
        return;
    }

    // invio prima il mittente
    basic_send(server_sck, host_user);

    // invio poi il destinatario
    basic_send(server_sck, msg->recipient);

    // invio il timestamp
    send(server_sck, (void*)msg->time_stamp, TIME_LEN+1, 0);
    
    // invio l'id del gruppo
    basic_send(server_sck, msg->group);

    // invio il testo del messaggio
    basic_send(server_sck, msg->text);

    strcpy(msg->status, "*");

    return;
}


/*
* Function: send_stored_messages_to_server
* ------------------------------------------
* invia al server i messaggi pendenti bufferizzati
*/
void send_stored_messages_to_server(){

    FILE *fp, *fp1;
    char buff_info[BUFF_SIZE];
    char buff_chat[BUFF_SIZE];

    // apro i file se esistono
    fp = fopen("./buffer_info.txt", "r");
    if (fp==NULL){
        printf("[-]No stored messages to send.\n");
        return;
    }
    fp1 = fopen("./buffer_texts.txt", "r");

    // recupero i dati dei messaggi bufferizzati
    while( fgets(buff_info, BUFF_SIZE, fp)!=NULL && fgets(buff_chat, MSG_LEN, fp1)!=NULL ) {
    
        struct message* mess = (struct message*)malloc(sizeof(struct message));
        if (mess == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }
        sscanf(buff_info, "%s %s %s %s", mess->sender, mess->recipient, mess->time_stamp, mess->group);
        strcpy(mess->text, buff_chat);
        send_offline_message(mess);
        free(mess);
    }
    fclose(fp);
    fclose(fp1);

    remove("./buffer_info.txt");
    remove("./buffer_texts.txt");
    
    printf("[+]Stored messages sent to server.\n");

}


/*
* Function: setup_conn_server
* ------------------------------
* instaura una connessione TCP col server
*/
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
        perror("[-]Server may be offline");
        SERVER_ON = false;
        return -1;
    }

    FD_SET(server_sck, &master);    // aggiungo il socket al master set
    SERVER_ON = true;
    printf("[+]Server connection set up.\n");

    // invio i messaggi salvati e un eventuale logout
    send_stored_messages_to_server();
    return 1;
}


/*
* Function: send_message_to_peer
* ----------------------------------
* invia un messaggio al peer 'user'
*/
int send_message_to_peer(struct message* m, char* user){

    int ret, sck, port;
    char cmd[CMD_SIZE+1] = "RMP";

    sck = get_conn_peer(peers, user);           // ottengo il socket di user
    if (sck==-1){                               // se non è ancora stata stabilita una connessione
        port = check_contact_list(user);        // ottengo la porta
        sck = setup_new_con(peers, port, user); // e provo a stabilirne una
    }
    if (sck==-1) return -1;                     // se il peer risulta offline

    ret = send(sck, (void*)cmd,CMD_SIZE+1, 0);
    if (ret<=0){                                // se ci sono problema nell'invio dei dati
        remove_from_peers(user);                // suppongo che il peer sia offline
        printf("[-]Peer may be offline.\n");
        return -1;
    }

    // invio il gruppo
    basic_send(sck, m->group);

    // invio il sender
    basic_send(sck, m->sender);

    // invio il timestamp
    send(sck, (void*)m->time_stamp, TIME_LEN+1, 0);

    // invio il messaggio
    basic_send(sck, m->text);
    printf("[+]Message sent.\n");

    return 1;
}


/*
* Function: preview_hanging
* -----------------------------
* chiede al server e mostra al client la lista degli utenti che gli hanno inviato messaggi mentre era offline. 
* Per ogni utente, il comando mostra username, il numero di messaggi pendenti in ingresso, e il timestamp del più recente.
*/
void preview_hanging(){

    int ret;
    char presence [RES_SIZE+1];
    char message [BUFF_SIZE];
    char command [CMD_SIZE+1] = "HNG";

    // prima si invia la richiesta del servizio
    printf("[+]Sending request to server.\n");
    ret = send(server_sck, (void*) command, CMD_SIZE+1, 0);

    // se ci sono problemi nell'invio dei dati suppongo che il server sia offline
    if (ret<=0){    
        printf("[-]Server may be offline.\n");
        close(server_sck);
        FD_CLR(server_sck, &master);
        SERVER_ON = false;
        return;
    }
    
    // poi invio il nome dello user
    printf("[+]Sending host name to server.\n");
    basic_send(server_sck, host_user);

    // poi si riceve il responso se ci sono oppure no
    printf("[+]Receiving response from server.\n");
    recv(server_sck, (void*)&presence, RES_SIZE+1, 0);

    // se non ci sono stampare un semplice messaggio di avviso
    if (strcmp(presence,"E")==0){    // negative response
        
        basic_receive(server_sck, message);
        printf("[+]Message from server:\n");
        printf(message);
        fflush(stdout);
        return;
    }
    else if(strcmp(presence,"S")==0){   // success response
        // altrimenti ricevere lunghezza del messaggio e il messaggio
        printf("\nMittente\tN° messaggi\tTimestamp\n");
        basic_receive(server_sck, message);
        printf(message);
        fflush(stdout);
        return;
    }

}


/*
* Function: show_history
* -----------------------------
* mostra la cronologia della chat
*/
void show_history(char* user){

    FILE *fp, *fp1;
    char file_name[USER_LEN+20];
    char file_name1[USER_LEN+20];
    char buff_info[BUFF_SIZE];
    char buff_chat[MSG_LEN];

    // ottengo i nomi dei file
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

    while( fgets(buff_info, BUFF_SIZE, fp)!=NULL && fgets(buff_chat, MSG_LEN, fp1)!=NULL ) {

        printf(buff_info);
        printf(buff_chat);
        printf("\n");
    }
  
    fclose(fp);
    fclose(fp1);
}


/*
* Function: show_online_users
* -----------------------------
* mostra una lista dei contatti attualmente online.
*/
void show_online_users(){

    int cur_port;
    char buffer [BUFF_SIZE];
    char list [BUFF_SIZE];
    char cur_name[USER_LEN+1];
    FILE * fp;

    fp = fopen("./contact_list.txt", "r");

    if (fp==NULL){
        printf("[-]No contacts yet.\n");
        return;
    }

    while( fgets(buffer, sizeof buffer, fp)!= NULL){
        sscanf(buffer, "%s %d", cur_name, &cur_port);

        // controllo se ho già una conversazione aperta con questo contatto
        if (get_conn_peer(peers, cur_name)!=-1){
            // aggiungo il nome alla lista
            strcat(list, "\t");
            strcat(list, cur_name);
            strcat(list, "\n");
        }
        // altrimenti provo a instaurare una connessione tcp
        else if (setup_new_con(peers, cur_port, cur_name)!=-1){
            // aggiungo il nome alla lista
            strcat(list, "\t");
            strcat(list, cur_name);
            strcat(list, "\n");

            // rimuovo la connessione
            remove_from_peers(cur_name);
        }
    }

    fclose(fp);

    printf("*************** USERS ONLINE ***************\n");
    printf(buffer);    

}


/*
* Function: new_contact_handler
* -------------------------------
* gestisce l'invio del primo messaggio ad un nuovo contatto tramite il server.
* Oltre a ricevere l'ack di ricezione o memorizzazione dal server, riceve anche
* la porta del nuovo contatto.
*/
int new_contact_handler(char* user, struct message* m){

    uint8_t ack;
    uint16_t port;
    char buffer[BUFF_SIZE];
    char cmd[CMD_SIZE+1] = "NCH";

    // nel caso in cui il server risulatava offline provo a ricontattarlo
    if (SERVER_ON == false && setup_conn_server()==-1){
        // se risulta ancora offline mi limito a salvare il messaggio localmente
        store_message(m);
        printf("[-]Couldn't send message to server.\n");
        return -1;
    }

    // invio il comando al server
    if ( send(server_sck, (void*)cmd, CMD_SIZE+1, 0)<=0 ){  
        // se non va a buon fine si deduce che il server è offline
        printf("[-]Server may be offline.\n");
        close(server_sck);
        FD_CLR(server_sck, &master);
        SERVER_ON = false;
        return -1;
    }
    
    // invio il nome utente al server
    basic_send(server_sck, user);

    // attendo una risposta riguardo la validità del nome inviato
    recv(server_sck, buffer, RES_SIZE+1, 0);

    if (strcmp(buffer, "E")==0){    // se il nome non risulta valido
        basic_receive(server_sck, buffer);
        printf(buffer);
        fflush(stdout);
        return -1;
    }

    // invio il messaggio al server
    basic_send(server_sck, m->time_stamp);
    basic_send(server_sck, m->group);
    basic_send(server_sck, m->recipient);
    basic_send(server_sck, m->sender);
    basic_send(server_sck, m->status);
    basic_send(server_sck, m->text);

    // ricevo ack
    recv(server_sck, (void*)&ack, sizeof(uint8_t), 0);
    
    if ( ack==1 ) // in caso di ack di memorizzazione
    {
        printf("[-]User %s is offline.\n", user);
        printf("[+]Message saved by server.\n");
        strcpy(m->status, "*");
        return 0;
    }

    // altrimenti il messaggio è stato recapitato al destinatario
    // ricevo la porta
    printf("[+]Message sent to new contact.\n");
    recv(server_sck, (void*)&port, sizeof(uint16_t), 0);
    port = ntohs(port);

    // instauro una connessione tcp col nuovo contatto
    current_chat->sck = setup_new_con(peers, port, user);
    strcpy(m->status, "**");
    return 1;

}
    

/*
* Function: add_member
* ------------------------
* aggiungi un nuovo membro alla chat corrente
*/
int add_member(char *user){

    uint16_t counter, cur_port;
    int sck_user, port;
    struct con_peer* new_member;
    struct con_peer* temp;
    char cmd[CMD_SIZE+1] = "ANG"; // (Added to New Group)

    // controllo se è già attiva una conversazione con user
    sck_user = get_conn_peer(peers, user);

    if (sck_user==-1){     // se non è presente tra le con_peer
        // instauro una nuova connessione tcp con user
        port = check_contact_list(user);
        sck_user = setup_new_con(peers, port, user);
    }

    // se è il primo utente aggiunto al gruppo (i.e. terzo nella chat)
    if (strcmp(current_chat->group, "-")==0){

        struct con_peer* m1 = (struct con_peer*)malloc(sizeof(struct con_peer));
        struct con_peer* m2 = (struct con_peer*)malloc(sizeof(struct con_peer));

        if (m1 == NULL || m2 == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }

        // assegno un nome al gruppo
        strcpy(current_chat->group, host_user);
        strcat(current_chat->group, "group");

        // aggiungo al gruppo i primi due membri
        m1->socket_fd = -1;
        strcpy(m1->username, host_user);

        m2->socket_fd = get_conn_peer(peers, current_chat->recipient);
        strcpy(m2->username, current_chat->recipient);
        m2->next = NULL;

        m1->next = m2;
        current_chat->members = m1;

        printf("[+]Group created.\n");
    }
    
    new_member = (struct con_peer*)malloc(sizeof(struct con_peer));
    if (new_member == NULL){
        perror("[-]Memory not allocated");
        exit(-1);
    }

    strcpy(new_member->username, user);
    new_member->socket_fd = sck_user;
    new_member->next = current_chat->members;
    current_chat->members = new_member;

    printf("[+]Group updated.\n");

    // invio il comando al peer
    send(sck_user, (void*)cmd, CMD_SIZE+1, 0);

    // invio nome del gruppo al nuovo membro
    basic_send(sck_user, current_chat->group);

    // invio prima il numero dei membri facenti parte del gruppo
    counter = htons((current_chat->users_counter));
    send(sck_user, (void*)&counter, sizeof(uint16_t), 0);

    // invio info (user e porta) degli altri membri del gruppo
    printf("[+]Sending info on group members to new member.\n");
    temp = current_chat->members;
    for (; temp; temp = temp->next){

        if (strcmp(temp->username, new_member->username)!=0)    // evito di inviare info su sé stesso
        {   // invio username
            basic_send(sck_user, temp->username);

            // invio porta
            if(temp->socket_fd==-1)
                cur_port = client_port;
            else
                cur_port = check_contact_list(temp->username);
            cur_port = htons(cur_port);
            send(sck_user, (void*)&cur_port, sizeof(uint16_t), 0);
        }
    }

    current_chat->users_counter++;

    free(temp);
    printf("[+]New user %s added to group.\n", user);

    return 1;
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

    printf("[+]Caching message...\n");

    // ottengo i nomi dei file
    strcpy(file_name0, "./cache/");
    strcpy(file_name1, "./cache/");

    if (strcmp(msg->group, "-")==0){
        strcat(file_name0, msg->recipient);
        strcat(file_name1, msg->recipient);
    }
    else{
        strcat(file_name0, msg->recipient);
        strcat(file_name1, msg->recipient);
    }

    strcat(file_name0, "_info.txt");
    strcat(file_name1, "_texts.txt");

    // apro i file in append
    fp = fopen(file_name0, "a");    // info
    fp1 = fopen(file_name1, "a");   // text

    if (fp1==NULL){
        printf("[-]Cache files couldn't be found.\n");
        printf("[+]New cache files created.\n");
    }
    fclose(fp);
    fclose(fp1);

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
    fprintf(fp1, "%s %s", msg->status, msg->text);
    fflush(fp1);
    fclose(fp1);

    // ordino in base al timestamp i messaggi nella cache
    if (strcmp(msg->group, "-")==0)
        sort_messages(msg->recipient);
    else sort_messages(msg->group);

    printf("[+]Message cached.\n");

}


/*
* Function: chat_handler
* --------------------------
* gestisce messaggi e comandi nel contesto di una chat. In particolare controlla l'input del user
* e se corrisponde ad uno dei comandi chiama il handler. A seconda del mittente chiama il server 
* perché gestisca l'invio del messaggio oppure invia il messaggio direttamente al destinatario.
* In ogni caso salva il messaggio in una cache locale.
*/
void chat_handler(){

    char buffer [MSG_LEN];
    char timestamp [TIME_LEN+1];
    char *token;
    char* b = buffer;
    size_t dimbuf = MSG_LEN;

    // salvo l'input dell'utente
    getline(&b, &dimbuf, stdin);

    // controllo prima se l'input è un comando
    if (strcmp(b, "\\q\n")==0){             // richiesta di chiusura della chat

        printf("[+]Chat correctly closed.\n");
        current_chat->on = false;
        menu_client();
        return;
    }
    else if (strcmp(b, "\\u\n")==0){        // richiesta della lista degli utenti online
        
        show_online_users();
    }    
    else if ( strncmp(b, "\\a ", 3)==0 ){   // richiesta di aggiungere un nuovo membro alla chat

        //ricavo il nome dello user
        buffer[strcspn(buffer, "\n")] = '\0';
        token = strtok(buffer, " ");
        token = strtok(NULL, " ");

        // aggiungo l'utente scelto alla conversazione 
        add_member(token);
    }
    else{
        // trattasi di un messaggio
        struct message* new_msg = (struct message*)malloc(sizeof(struct message));

        if (new_msg == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }

        time_t now = time(NULL);
        strftime(timestamp, TIME_LEN, "%Y-%m-%d %H:%M:%S", localtime(&now));

        strcpy(new_msg->sender, host_user);
        strcpy(new_msg->recipient, current_chat->recipient);
        strcpy(new_msg->group, current_chat->group);
        strcpy(new_msg->time_stamp, timestamp);
        strcpy(new_msg->text, b);

        // singola chat (conversazione a due)
        if (strcmp(current_chat->group, "-")==0){
                
            // se è il primo messaggio ad un nuovo contatto
            if( current_chat->sck == -1){

                if ( new_contact_handler(current_chat->recipient, new_msg)<=0 ){
                    strcpy(new_msg->status, "*");
                    current_chat->sck = -2;
                }
                else{
                    strcpy(new_msg->status, "**");
                }
            }
            else{  // utente noto
                // invio il messaggio al peer
                if (send_message_to_peer(new_msg, current_chat->recipient)==-1){
                    // se il peer risulta offline, invio il messaggio al server
                    send_offline_message(new_msg);
                    strcpy(new_msg->status, "*");
                }
                else{
                    strcpy(new_msg->status, "**");
                }                    
            }
        }
        // messaggio di gruppo
        else{   

            // invio il messaggio ad ogni membro del gruppo
            struct con_peer* temp = current_chat->members;
            strcpy(new_msg->group, current_chat->group);
            strcpy(new_msg->status, "-");

            while(temp){
                if (temp->socket_fd!=-1){   // evito il mittente
                        
                    strcpy(new_msg->recipient, temp->username);

                    if (send_message_to_peer(new_msg, temp->username)==-1)
                        send_offline_message(new_msg);
                }
                temp = temp->next;
            }
            free(temp);
        }
        
        save_message(new_msg);
        system("clear");
        printf("****************** CHAT WITH %s ******************\n", current_chat->recipient);
        show_history(current_chat->recipient); 
    }
} 


/*
* Function: start_chat
* --------------------------
* avvia una chat e stampa una eventuale cronologia
*/
void start_chat(char* user){

    printf("[+]Starting chat...\n");

    // controllo parametro di ingresso
    if (strcmp(user, host_user)==0){
        printf("[-]Attempt to chat with yourself.\n");
        return;
    }

    if (strlen(user)>50){
        printf("[-]Username doesn't exist.\n");
        return;
    }

    // se l'utente è presente in rubrica stampiamo la cache(user potrebbe essere anche l'id di un gruppo) 

    current_chat = (struct chat*)malloc(sizeof(struct chat)); 
    if (current_chat == NULL){
        perror("[-]Memory not allocated");
        exit(-1);
    }
    
    strcpy(current_chat->group, "-");
    current_chat->sck = -1;
    strcpy(current_chat->recipient, user);
    current_chat->on = true;
    current_chat->users_counter = 2;

    system("clear");
    printf("****************** CHAT WITH %s ******************\n", current_chat->recipient);

    if (check_contact_list(user)==1)
        show_history(user);   
}

/*
* Function: leave_group
* ----------------------------
* termina una sessione di gruppo creata da altri
*/
void leave_group(int sck){

    char grp_name[BUFF_SIZE];

    // ricevo il nome del gruppo
    basic_receive(sck, grp_name);

    // mostro notifica di terminazione del gruppo
    printf("[+]Group %s terminated: creator logged out.\n", grp_name);

    // se mi trovavo all'interno della conversazione di gruppo
    // mi riporto nel menu principale
    if (current_chat!=NULL && strcmp(current_chat->group, grp_name)==0){
        current_chat->on = false;
        menu_client();
    }
}

/*
* Function: terminate_group
* ----------------------------
* funzione chiamata in fase di chiusura dell'applicazione.
* termina una eventuale sessione di gruppo creata dal device host.
*/
void terminate_group(){

    char buff[BUFF_SIZE];
    char cmd[CMD_SIZE+1] = "EOG";   // (End Of Group)
    struct chat* temp_chat = ongoing_chats;
    struct chat* my_group = NULL;

    strcpy(buff, host_user);
    strcat(buff, "group");


    // controllo se esistono dei gruppi    
    while( temp_chat!=NULL){
        if (strcmp(temp_chat->group, "-")!=0){   //nel caso elimino le cache corrispondenti
            
            strcpy(buff, "./cache/");
            strcat(buff, temp_chat->group);
            strcat(buff, "_info.txt");
            remove(buff);

            strcpy(buff, "./cache/");
            strcat(buff, temp_chat->group);
            strcat(buff, "_texts.txt");
            remove(buff);

            strcpy(buff, host_user);
            strcat(buff, "group");
            if (strcmp(temp_chat->group, buff)==0){
                my_group = temp_chat;
            }
        }
        temp_chat = temp_chat->next;
    }

    if (temp_chat==NULL){
        printf("[+]No group found.\n");
        return;
    }
    if (my_group!=NULL){

    // invio ad ogni membro del gruppo una notifica di terminazione del gruppo
        struct con_peer* temp_member = my_group->members;
        
        while(temp_member!=NULL){
            if (temp_member->socket_fd!=-1){
                // invio il comando
                send(temp_member->socket_fd, (void*)cmd, CMD_SIZE+1, 0);
                basic_send(temp_member->socket_fd, my_group->group);    // invio il nome del gruppo
            }
            temp_member = temp_member->next;
        }
        free(temp_member);
    }
    free(temp_chat);
    free(my_group);

    printf("[+]Group terminated.\n");

}

/*
* Function: logout
* -------------------
* invia una richiesta di logout al server
*/
void logout(){

    FILE* fp;
    int ret;
    char timestamp[TIME_LEN+1];
    char cmd[CMD_SIZE+1] = "LGO";
    int i = 2;

    printf("[+]Logging out...\n");

    // chiudo un eventuale gruppo
    terminate_group();

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
    else{
        // invio il timestamp al server
        send(server_sck, (void*)timestamp, TIME_LEN+1, 0);
    }

    // chiudo tutti i socket
    for(; i<=fdmax; i++) {
        close(i);
        FD_CLR(i, &master);
    }
    printf("[+]Logged out succesfully.\n");

    exit(1);
}
        
/*
* Function: show_user_hanging
* ---------------------------
* riceve dal server (se online) tutti i messaggi pendenti da user e li stampa
*/
void show_user_hanging(char* user){

    int mess_counter, ret;
    uint16_t many;
    char message [BUFF_SIZE];
    char command [CMD_SIZE+1] = "SHW";

    // nel caso in cui il server risultava offline, provo a ristabilire la connessione
    if (SERVER_ON==false){

        ret = setup_conn_server();
        if(ret==-1){    // se il server risulta ancora offline rinuncio ad inviare il messaggio
            return;
        }
    }

    // prima si invia la richiesta del servizio
    printf("[+]Sending request to server.\n");
    ret = send(server_sck, (void*) command, CMD_SIZE+1, 0);
    // se ci sono problemi nell'invio dei dati suppongo che il server sia offline
    if (ret<=0){    
        printf("[-]Server may be offline.\n");
        close(server_sck);
        FD_CLR(server_sck, &master);
        SERVER_ON = false;
        return;
    }

    // invio nome del destinatario al server
    printf("[+]Sending host name to server.\n");
    basic_send(server_sck, host_user);

    // invia nome del mittente al server
    printf("[+]Sending sender name to server.\n");
    basic_send(server_sck, user);

    // ricevi responso del numero di messaggi dal server
    printf("[+]Receiving number of messages from server.\n");
    recv(server_sck, (void*)&many, sizeof(u_int16_t), 0);
    mess_counter = ntohs(many);
    if (mess_counter==0){
        printf("\nNon ci sono messaggi pendenti da %s\n", user);
        return;
    }

    while (mess_counter>0){

        struct message* m = (struct message*)malloc(sizeof(struct message));
        if (m == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }
        // ricevo info sul messaggio
        basic_receive(server_sck, message);

        sscanf (message, "%s %s %s", m->time_stamp, m->group, m->sender);
        strcpy(m->status, "-");

        // ricevo il contenuto del messaggio
        basic_receive(server_sck, message);

        strcpy(m->text, message);

        // aggiungere i messaggi nella cache
        save_message(m);
        print_message(m);
        mess_counter--;
    }

    // nel caso fossero messaggi di un nuovo contatto chiedo la porta al server
    if (check_contact_list(user)==-1){  
        
        uint16_t new_port;
        char cmd[CMD_SIZE+1] = "RCP"; //(ReCeive Port)

        // invio il comando al server
        send(server_sck, (void*)cmd, CMD_SIZE+1, 0);
        // invio il nome
        basic_send(server_sck, user);
        // ricevo la porta
        recv(server_sck, (void*)&new_port, sizeof(uint16_t), 0);
        new_port = ntohs(new_port);

        // aggiungo il contatto
        add_contact_list(user, new_port);
    }
}


/*
* Function: command_handler
* -----------------------------
* considera l'input in stdin e a seconda della stringa viene avviato l'apposito handler.
*/
void command_handler(){

    char buffer[BUFF_SIZE];
    char *b = buffer;
    char *token0, *token1;
    size_t dimbuf = BUFF_SIZE;

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
        if (token1!=NULL){
            show_user_hanging(token1);
        }
        else{
            printf("Please insert a username.\n");
        }
    }
    else if(strcmp(token0, "chat")==0){
        if (token1!=NULL){
            start_chat(token1);
        }
        else{
            printf("Please insert a username.\n");
        }
    }
/*     else if(strcmp(token0, "share")==0){
        file_handler(token1);
    } */
    else if(strcmp(token0, "out")==0){
        logout();       
    }
    else{
        printf("[-]Invalid command! Please try again.\n");
    }

}


/*
* Function: signup
* -----------------------------
* invia le nuove credenziali al server che le registra.
* se il responso è positivo l'utente è stato correttamente registrato.
*/
int signup(char* user, char* psw){
    
    int sv_status;
    uint16_t port;
    char response [RES_SIZE+1];
    char message [BUFF_SIZE];
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

    printf("[+]Submitting credentials: user: %s, encrypted psw: %s\n", user, psw);

    // send request to server
    printf("[+]Sending request to server.\n");
    send(server_sck, (void*) command, CMD_SIZE+1, 0);

    // send user
    basic_send(server_sck, user);

    // send psw
    basic_send(server_sck, psw);

    // send port
    port = htons(client_port);
    send(server_sck, (void*) &port, sizeof(uint16_t), 0);

    // receive response from server
    recv(server_sck, (void*)response, RES_SIZE+1, 0);

    if (strcmp(response,"E")==0){    // negative response
        
        basic_receive(server_sck, message);

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


/*
* Function: add_group
* ----------------------
* aggiungo un gruppo alla lista delle conversazioni attive
*/
void add_group(int sck){

    FILE *fp, *fp1;
    char fn[USER_LEN+20];
    char fn1[USER_LEN+20];
    int temp_sck;
    uint16_t members_counter;
    struct chat* group_chat = malloc(sizeof(struct chat));
    if (group_chat == NULL){
        perror("[-]Memory not allocated");
        exit(-1);
    }

    // ricevo il nome del gruppo
    basic_receive(sck, group_chat->group);

    // ricevo il numero dei membri del gruppo (escluso questo device)
    recv(sck, (void*)&members_counter, sizeof(uint16_t), 0);
    members_counter = ntohs(members_counter);

    while (members_counter>0){

        uint16_t port;
        
        struct con_peer* member = (struct con_peer*)malloc(sizeof(struct con_peer));
        if (member == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }

        basic_receive(sck, member->username);           // ricevo lo username
        recv(sck, (void*)&port, sizeof(uint16_t), 0);   // ricevo la porta
        temp_sck = get_conn_peer(peers, member->username);
        if (temp_sck==-1){
            temp_sck = setup_new_con(peers, port, member->username);
        }
        member->socket_fd = temp_sck;

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


/*
* Function: login
* ----------------
* permette di effettuare il login e notificarlo al server
*/
int login(char* user, char* psw){

    int sv_status;
    uint16_t port;
    char response [RES_SIZE+1];
    char message [BUFF_SIZE];
    char command [CMD_SIZE+1] = "LGI";

    encrypt(psw, CRYPT_SALT);

    // instauro una connessione al server
    sv_status = setup_conn_server();
    if(sv_status==-1){
        close(server_sck);
        return -1;
    }

    // invio la richiesta al server
    send(server_sck, (void*) command, CMD_SIZE+1, 0);

    // invio lo user
    basic_send(server_sck, user);

    // invio la password criptata
    basic_send(server_sck, psw);

    // invio la porta del client
    port = htons(client_port);
    send(server_sck, (void*) &port, sizeof(uint16_t), 0);

    // ricevo il responso dal server
    recv(server_sck, (void*) &response, RES_SIZE+1, 0);

    if (strcmp(response,"E")==0){    // esito negativo

        basic_receive(server_sck, message);

        printf(message);
        fflush(stdout);
        return -1;
    }
    else if(strcmp(response,"S")==0){   // esito positivo
        strcpy(host_user, user);
        printf("[+]Login succeeded.\n");
        return 0;
    }
    else {
        printf("[-]Errore nel response del server!\n");
        return -1;
    }
}


/*
* Function: receive_message_handler
* -------------------------------------
* gestisce la ricezione di un messaggio da parte di un altro device
*/
void receive_message_handler(int sck){

    struct message* new_msg = (struct message*)malloc(sizeof(struct message));
    if (new_msg == NULL){
        perror("[-]Memory not allocated");
        exit(-1);
    }

    // ricevo il gruppo
    basic_receive(sck, new_msg->group);

    // ricevo il sender
    basic_receive(sck, (void*)new_msg->sender);

    // ricevo il timestamp
    recv(sck, (void*)new_msg->time_stamp, TIME_LEN+1, 0);

    // ricevo il messaggio
    basic_receive(sck, new_msg->text);

    strcpy(new_msg->status, "-");

    // save message
    save_message(new_msg);

    // mostro unaa notifica se al momento non è aperta la chat a cui il messaggio appartiene
    if( current_chat->on==false || 
        ( strcmp(current_chat->group, "-")==0 && strcmp(current_chat->recipient, new_msg->sender)!=0) ||
        ( strcmp(current_chat->group, "-")!=0 && strcmp(current_chat->group, new_msg->group)!=0 )
    ){
        if (strcmp(new_msg->group, "-")==0)
            printf("[+]New message from %s.\n", new_msg->sender);
        else
            printf("[+]New message from %s.\n", new_msg->group);
        sleep(2);
    }
    else{   // altrimenti stampo il messaggio
        print_message(new_msg);
    }
}


/*
* Function: update_ack
* aggiorna lo stato dei messaggi ora visualizzati dal destinatario
*/
void update_ack(char* dest){

    FILE* fp;
    char buff[BUFF_SIZE];
    char fn[USER_LEN+20];
    struct message* list = malloc(sizeof(struct message));
    struct message* next;
    struct message* cur = list;

    if (list==NULL){
        perror("[-]Memory not allocated");
        exit(-1);
    }
    
    // ricavo il nome del file
    strcpy(fn, "./cache/");
    strcat(fn, dest);
    strcat(fn, "_texts.txt");

    fp = fopen(fn, "r+");

    while( fgets(buff, BUFF_SIZE, fp)!=NULL ){

        printf("[+]Fetching cached messages.\n");

        char status[3];
        char text[MSG_LEN];
        struct message* cur_msg = (struct message*)malloc(sizeof(struct message));

        if (cur_msg==NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }
        
        // aggiorno lo stato dei messaggi
        if (strcmp(status, "*")==0){
            printf("[+]Updating message status.\n");
            strcpy(status, "**");
        }
        sscanf(buff, "%s %s", status, text);
        strcpy(cur_msg->status, status);
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

    while (cur!=NULL){

        fprintf(fp, "%s %s\n", cur->status, cur->text);
        next = cur->next;
        free(cur);
        cur = next;
    }

    printf("[+]Cache updated.\n");
    free(list);
    cur = NULL;
    next = NULL;

}

/*
* Function: receive_acks
* -------------------------
* riceve ack di ricezione di messaggi visualizzati mentre era offline
*/
void receive_acks(){

    uint8_t counter;
    char cmd[CMD_SIZE+1] = "RCA";    // (ReCeive Acks)

    // invia il comando
    send(server_sck, (void*)cmd, CMD_SIZE+1, 0);

    // riceve il numero di acks
    recv(server_sck, (void*)&counter, sizeof(uint8_t), 0);

    if (counter==0){
        printf("[-]There are no buffered acks.\n");
        return;
    }

    // riceve ogni struttura ack
    while (counter>0){
        
        char cur_rec[USER_LEN+1];
        char cur_temp[TIME_LEN+1];

        basic_receive(server_sck, cur_rec);     // ricevo il destinatario
        recv(server_sck, (void*)cur_temp, TIME_LEN+1, 0);   // ricevo il timestamp del messaggio meno recente
    
        // per ogni ack aggiorna la corrispondente cache dei messaggi
        update_ack(cur_rec);

        counter--;
    }
    printf("[+]Acks received.\n");

}


/*
* Function: input_handler
* -------------------------------------
* gestisce l'input iniziale dell'utente. Se valido avvia il corrispondente handler.
*/
void input_handler(){

    char input[BUFF_SIZE];
    char *token0, *token1, *token2, *token3;

    fgets(input,sizeof(input),stdin);
    input[strlen(input)-1] = '\0';
    printf("\n");

    // ottengo i tokens
    token0 = strtok(input, " ");

    token1 = strtok(NULL, " ");

    token2 = strtok(NULL, " ");

    token3 = strtok(NULL, " ");

    // controllo che la prima parola sia valida e nel caso avvio handler
    if (token0==NULL){
        printf("Please type one of the above-listed commands.\n");
        prompt_user();
        input_handler();
    }
    else if (strcmp(token0, "signup")==0){   // signup

        if ( token1 && token2 ){
            if (signup(token1, token2)==-1){
                printf("Try again.\n");
            }
            else{   
                printf("Please login with your new credentials.\n");
            }
        }
        else{
            printf("Please insert username and password after 'signup'.\n");
        }
        prompt_user();
        input_handler();

    }
    else if(strcmp(token0, "in")==0){       // login

        if ( token1 && token2 && token3 ){

            if (login(token2, token3)==-1){
                printf("Try again.\n");
                prompt_user();
                input_handler();
            }
            else sleep(1);
        }
        else{
            printf("Please insert port, username and password after 'in'.\n");
            prompt_user();
            input_handler();
        }
        
    }
    else{
        printf("[-]Invalid command! Please try again.\n");
        prompt_user();
        input_handler();
    }
}


/*
* Function: server_peers
* ------------------------
* gestisce le connessioni TCP peer to peer e server-client.
*/
void server_peers(){

    struct sockaddr_in sv_addr;         // indirizzo del server
    struct sockaddr_in cl_addr;         // indirizzo del client
    int newfd;                          // socket di comunicazione
    char cmd[CMD_SIZE+1];               // buffer per il comando
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
    setsockopt(server_sck, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    listen(listener, 10);
    
    FD_SET(listener, &master);      // aggiungo il listener al set dei socket monitorati
    FD_SET(0,&master);              // aggiungo anche stdin
    FD_SET(server_sck, &master);    // aggiungo anche il socket del server

    menu_client();
    prompt_user();
    
    // tengo traccia del maggiore (ora è il listener)
    fdmax = listener;
    for(;;){
        read_fds = master; // read_fds sarà modificato dalla select

        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("[-]Error in select");
            exit(4);
        }
        printf("[+]Select worked.\n");


        for(i=0; i<=fdmax; i++) { 

            if(FD_ISSET(i, &read_fds)) {

                if(i==0){
                    if (current_chat!=NULL && current_chat->on){
                        chat_handler();
                    }
                    else{
                        command_handler();
                    }
                }
                else if(i == listener) { 

                    addrlen = sizeof(cl_addr);
                    newfd = accept(listener, (struct sockaddr *)&cl_addr, &addrlen);
                    FD_SET(newfd, &master);             // aggiungo il nuovo socket
                    if(newfd > fdmax){ fdmax = newfd; } // aggiorno fdmax
                }
                else { // il socket connesso è pronto

                    nbytes = recv(i, (void*)cmd, CMD_SIZE+1, 0);
                    if (nbytes<=0){
                        if (SERVER_ON && i == server_sck){
                            close(i);
                            FD_CLR(i, &master);
                        }
                        else{
                            remove_from_peers(get_name_from_sck(peers, i));
                        }
                    }
                    else{
                        if (strcmp(cmd, "RMP")==0){
                            receive_message_handler(i);
                        }
                        else if (strcmp(cmd, "EOG")==0){
                            leave_group(i);
                        }
                        else if (strcmp(cmd, "ANG")==0){
                            add_group(i);
                        }
                        else{
                            printf("[-]Abnormal request %s received.\n", cmd);
                        }
                    }
                }
                prompt_user();
            } 
        } 
    } 
    return;
}


/*
* Function: main
* ------------------------
* inizia l'esecuzione del client. avvia l'interfaccia per il login o la signup.
*/
int main(int argc, char* argv[]){

    if (argc > 1) {
        client_port = atoi(argv[1]);
    }
    else {
        printf("[-]No port specified. Please insert a port number.\n\n");
        return -1;
    }

    home_client();
    prompt_user();
    input_handler();

    /* da inviare dentro la setup del server
    send_stored_messages_to_server(); */
    send_last_log();
    receive_acks();
    server_peers();

    return 0;
}
