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
int grp_id = 0;                     // contatore dei gruppi creati
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
* Function: send_offline_message
* ---------------------------------
* se il server è online invia un messaggio destinato ad un utente offline perché venga salvato.
*/
int send_offline_message(struct message* msg){
    
    int ret; 
    char cmd[CMD_SIZE+1] = "SOM";

    ret = send(server_sck, (void*)cmd, CMD_SIZE+1, 0);
    if (ret<=0){    // se ci sono problemi nell'invio dei dati suppongo che il server sia offline
        printf("[-]Server may be offline.\n");
        close(server_sck);
        FD_CLR(server_sck, &master);
        SERVER_ON = false;
        return -1;
    }

    // invio prima il mittente
    basic_send(server_sck, host_user);

    // invio poi il destinatario
    basic_send(server_sck, msg->recipient);

    // invio il timestamp
    send(server_sck, (void*)msg->time_stamp, TIME_LEN+1, 0);
    
    // invio l'id del gruppo
    basic_send(server_sck, msg->group);

    // invio il messaggio
    basic_send(server_sck, msg->text);

    return 1;
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
    send_last_log();
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
    char cmd[CMD_SIZE+1] = "SMP";

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

    // invio la data
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
    printf("[+]Chat files correctly opened.\n");

    while( fgets(buff_info, BUFF_SIZE, fp)!=NULL && fgets(buff_chat, MSG_LEN, fp1)!=NULL ) {

        printf(buff_info);
        printf("\n");
        printf(buff_chat);
        printf("\n");
    }
  
    fclose(fp);
    fclose(fp1);
}


/*
* Function: store_messages
* ----------------------------
* funzione invocata ogni qualvolta devo inviare un messaggio ad un destinatario offline
* e anche il server risulta offline. Tale messaggio è salvato in file locali.
*/
void store_message(struct message* msg){

    FILE* fp, *fp1;

    // memorizzo in file il messaggio da inviare
    fp = fopen("./buffer_info.txt", "a");
    fp1 = fopen("./buffer_texts.txt", "a");

    fprintf(fp, "%s %s %s %s\n", host_user, msg->recipient, msg->time_stamp, msg->group);
    fprintf(fp1, "%s", msg->text);

    fclose(fp);
    fclose(fp1);
}


/*
* Function: show_online_users
* -----------------------------
* se il server è online richiede al server una lista degli utenti attualmente online,
* altrimenti ottiene una lista dei suoi contatti in rubrica online. Stampa la lista a video.
*/
void show_online_users(){

    int ret;
    char buffer [BUFF_SIZE];
    char command [CMD_SIZE+1] = "AOL";

    // nel caso in cui il server risultava offline, provo a ristabilire la connessione
    if (SERVER_ON==false){

        ret = setup_conn_server();
        // se il server risulta ancora offline rinuncio ad inviare la richiesta
        if(ret==-1){    
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

    // ricevo responso
    printf("[+]Receiving response from server.\n");
    basic_receive(server_sck, buffer);
    
    //stampo
    printf(buffer);
    printf("\n");
}


/*
* Function: ask_server_con_peer
* -------------------------------
* chiede al server la porta di un determinato user;
* ottenuta la porta stabilisce una connessione con lo user.
*/
int ask_server_con_peer(char* user){

    int sck, ret;
    uint16_t rec_port;
    char response[RES_SIZE+1];
    char buffer[BUFF_SIZE];
    char cmd[CMD_SIZE+1] = "NWC";
    
    // nel caso in cui il server risultava offline, provo a ristabilire la connessione
    if (SERVER_ON==false){

        ret = setup_conn_server();
        if(ret==-1){    // se il server risulta ancora offline rinuncio ad inviare il messaggio
            return -1;
        }
    }

    ret = send(server_sck, (void*)cmd, CMD_SIZE+1, 0);
    if (ret<=0){    // se ci sono problemi nell'invio dei dati suppongo che il server sia offline
        printf("[-]Server may be offline.\n");
        close(server_sck);
        FD_CLR(server_sck, &master);
        SERVER_ON = false;
        return 0;
    }

    // invio user 
    basic_send(server_sck, user);

    // ricevo responso dal server
    recv(server_sck, (void*)response, RES_SIZE+1, 0);
           
    if (strcmp(response,"E")==0){    // negative response

        basic_receive(server_sck, buffer);
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
        sck = setup_new_con(peers, rec_port, user);
    }

    return sck;
}

/*
* Function: add_member
* ------------------------
* aggiungi un nuovo membro alla chat corrente
*/
int add_member(char *user){

    uint16_t counter, len;
    char type[CMD_SIZE+1];
    char cmd[CMD_SIZE+1] = "ATG";     // add to group
    int sck_user, port;
    struct con_peer* new_member;
    struct con_peer* temp;

    // creo se non ancora attiva una connessione con user
    // controllo prima se è presente tra le con_peer
    sck_user = get_conn_peer(peers, user);

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
                first_ack_peer(host_user, client_port, sck_user,false);
            }
        }
        else{
            // invio il comando
            send(sck_user, (void*)cmd, CMD_SIZE+1, 0);
            // invia "old"
            strcpy(type, "old");
            send(sck_user, (void*)type, CMD_SIZE+1, 0);
            sck_user = setup_new_con(peers, port, user);
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
        if (m1 == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }
  
        struct con_peer* m2 = malloc(sizeof(struct con_peer));
        if (m2 == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }

        strcpy(current_chat->group, host_user);
        sprintf(num, "%d", grp_id);
        strcat(current_chat->group, num);

        grp_id++;

        // aggiungo ai membri i primi due del gruppo
        m1->socket_fd = -1;
        strcpy(m1->username, host_user);

        m2->socket_fd = get_conn_peer(peers, current_chat->recipient);
        strcpy(m2->username, current_chat->recipient);
        m2->next = NULL;

        m1->next = m2;
        current_chat->members = m1;
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

    printf("[+]New member %s added to group.\n", user);

    // invio nome del gruppo
    basic_send(sck_user, current_chat->group);

    // invio info (user e porta) degli altri membri del gruppo
    counter = htons((current_chat->users_counter));
    send(sck_user, (void*)&counter, sizeof(uint16_t), 0);

    printf("[+]Sending info on group members to new member.\n");
    temp = current_chat->members;
    for (; temp; temp = temp->next){

        // invio username
        basic_send(sck_user, temp->username);

        // invio porta
        len = check_contact_list(temp->username);
        len = htons(len);
        send(sck_user, (void*)&len, sizeof(uint16_t), 0);
    }

    current_chat->users_counter++;

    printf("[+]New user %s added to group.\n", user);

    return 1;

}


/*
* Function: print_message
* -------------------------
* formatta un messaggio aggiungendovi altre informazioni e lo stampa
*/
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
    
    printf("%s %s\n", status, msg->text);
    printf("\n");
    
}


/*
* Function: save_message
* --------------------------
* salva il messaggio nella cache apposita
*/
void save_message(struct message* msg, bool sent, bool mine){

    FILE* fp, *fp1;
    char status[3];
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
    printf(msg->time_stamp);
    fflush(fp);
    fclose(fp);
    sleep(5);

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
    
    // salvo il messaggio in file
    fp1 = fopen(file_name1, "a");   // text
    fprintf(fp1, "%s %s", status, msg->text);
    fflush(fp1);
    fclose(fp1);
    sleep(5);

    // ordino in base al timestamp i messaggi nella cache
    printf("DEBUG0");
    sleep(4);

    if (strcmp(msg->group, "-")==0)
        sort_messages(msg->recipient);
    else sort_messages(msg->group);

    printf("DEBUG1");
    sleep(4);


    printf("[+]Message cached.\n");

    printf("DEBUG2");

}


/*
* Function: chat_handler
* --------------------------
* gestisce messaggi e comandi nel contesto di una chat
*/
void chat_handler(){
    // controllare input se uno dei comandi agire di conseguenza
    // altrimenti inviare messaggio al server
    // e a seconda del responso salvarli come recapitati oppure no nel file di archivio della chat
    char buffer [MSG_LEN];
    char timestamp [TIME_LEN+1];
    int ret, sck;
    char *token;
    struct message* new_msg = malloc(sizeof(struct message));
    char* b = buffer;
    size_t dimbuf = MSG_LEN;

    // salvo l'input dell'utente
    getline(&b, &dimbuf, stdin);

    // controllo se il contatto è già presente in rubrica
    ret = check_contact_list(current_chat->recipient);

    system("clear");
    printf("****************** CHAT ******************\n");

    // in caso positivo stampo la cronologia dei messaggi
    if(ret==1){
        show_history(current_chat->recipient);
    }  

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
            // preparo il messaggio
            if (new_msg == NULL){
                perror("[-]Memory not allocated");
                exit(-1);
            }

            time_t now = time(NULL);
            strftime(timestamp, TIME_LEN, "%Y-%m-%d %H:%M:%S", localtime(&now));

            strcpy(new_msg->sender, host_user);
            strcpy(new_msg->recipient, current_chat->recipient);
            strcpy(new_msg->group, "-");
            strcpy(new_msg->time_stamp, timestamp);
            // salvo il testo
            strcpy(new_msg->text, b);

            // singola chat (conversazione a due)
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

                            // nel caso in cui il server risultava offline, provo a ristabilire la connessione
                            if (SERVER_ON==false){

                                ret = setup_conn_server();
                                if(ret==-1){    // se il server risulta ancora offline rinuncio ad inviare il messaggio
                                    return;
                                }
                            }
                            send_offline_message(new_msg);
                            printf("\33[2K\r");
                            print_message(new_msg, false, true);
                            save_message(new_msg, false, true);
                        }
                        else{
                            current_chat->sck = sck;
                            first_ack_peer(host_user, client_port, sck, true);
                            // invio il messaggio al peer
                            send_message_to_peer(new_msg, current_chat->recipient);
                            save_message(new_msg, true, true);
                            printf("\33[2K\r");
                            print_message(new_msg, true, true);
                        }
                    }
                    else{   // utente già noto  
                        sck = setup_new_con(peers, porta, current_chat->recipient);
                        if (sck==-1){   // utente offline
                                store_message(new_msg);
                                save_message(new_msg, false, true);
                                printf("\33[2K\r");
                                print_message(new_msg, false, true);
                        }
                        else{   // utente online
                            current_chat->sck = sck;
                            first_ack_peer(host_user, client_port, sck, true);
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

                        // nel caso in cui il server risultava offline, provo a ristabilire la connessione
                        if (SERVER_ON==false){

                            ret = setup_conn_server();
                            if(ret==-1){    // se il server risulta ancora offline rinuncio ad inviare il messaggio
                                return;
                            }
                        }
                        ret = send_offline_message(new_msg);
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

                            // nel caso in cui il server risultava offline, provo a ristabilire la connessione
                            if (SERVER_ON==false){

                                ret = setup_conn_server();
                                if(ret==-1){    // se il server risulta ancora offline rinuncio ad inviare il messaggio
                                    return;
                                }
                            }
                            ret = send_offline_message(new_msg);
                            if (ret==-1)    // server offline
                                store_message(new_msg);
                        }
                    }
                    temp = temp->next;
                }
                save_message(new_msg, true, true);
            }
    }
    prompt_user();
} 


/*
* Function: start_chat
* --------------------------
* avvia una chat e stampa una eventuale cronologia
*/
void start_chat(char* user){

    printf("[+]Starting chat...\n");
    if (strlen(user)>50){
        printf("[-]Username doesn't exist.\n");
        return;
    }

    // se l'utente è presente in rubrica stampiamo la cache(user potrebbe essere anche l'id di un gruppo)
    system("clear");
    printf("\n****************** CHAT ******************\n");

    if (check_contact_list(user)==1)
        show_history(user);    

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

        struct message* m = malloc(sizeof(struct message));
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
        save_message(m, true, false);
        print_message(m, true, false);
        mess_counter--;
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

    prompt_user();
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
    uint16_t members_counter, len;
    struct chat* group_chat = malloc(sizeof(struct chat));
    if (group_chat == NULL){
        perror("[-]Memory not allocated");
        exit(-1);
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
        if (member == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }

        basic_receive(sck, member->username);
        recv(sck, (void*)&port, sizeof(uint16_t), 0);
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

    struct message* new_msg = malloc(sizeof(struct message));
    if (new_msg == NULL){
        perror("[-]Memory not allocated");
        exit(-1);
    }

    // ricevo il gruppo
    basic_receive(sck, new_msg->group);

    // ricevo la data
    recv(sck, (void*)new_msg->time_stamp, TIME_LEN+1, 0);

    // ricevo il messaggio
    basic_receive(sck, new_msg->text);

    // ricavo il sender
    strcpy(new_msg->sender, get_name_from_sck(peers, sck));

    printf("[+]New message received.\n");

    // save message
    save_message(new_msg, true, false);

    // mostro unaa notifica se al momento non è aperta la chat a cui il messaggio appartiene
    if( current_chat->on==false || 
        ( strcmp(current_chat->group, "-")==0 && strcmp(current_chat->recipient, new_msg->sender)!=0) ||
        ( strcmp(current_chat->group, "-")!=0 && strcmp(current_chat->group, new_msg->group)!=0 )
    ){
        printf("\n Hai ricevuto un nuovo messaggio da %s.\n", new_msg->sender);
        sleep(2);
        printf("\33[2K\r");
    }
    else{   // altrimenti stampo il messaggio
        print_message(new_msg, true, false);
    }
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
                        if (strcmp(cmd, "SMP")==0){
                            receive_message_handler(i);
                        }
                        else if (strcmp(cmd, "ATG")==0){
                            add_group(i);
                        }
                        else{
                            printf("[-]Abnormal request %s received.\n", cmd);
                        }
                    }
                }
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
    send_last_log();
    send_stored_messages_to_server();
    receive_acks(); */
    server_peers();

    return 0;
}

