/*
* Chat-app: applicazione network di messaggistica istantanea. 
*
* client.c:   implementazione del client (device) della rete. 
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
uint16_t client_port;               // porta del client
char host_user[USER_LEN+1];         // nome utente del client
struct chat* ongoing_chats;         // lista delle chat attive nell'attuale sessione
struct chat* current_chat = NULL;   // ultima chat visualizzata
struct con_peer* peers = NULL;      // lista delle connessioni degli utenti con cui si è avviata una chat
bool SERVER_ON = false;             // tiene traccia dello stato del server
bool session_on = false;            // indica se è già stato effettuato il login


/*
 * Function:  home__client
 * -------------------------
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
* Function: send_last_log
* --------------------------
* invia se è presente l'ultimo logout salvato al server
*/
void send_last_log(){

    FILE *fp;
    char timestamp[TIME_LEN+1];
    char fn[BUFF_SIZE];
    char cmd[CMD_SIZE+1] = "LGO";

    sprintf(fn, "./%s/last_logout.txt", host_user);

    fp = fopen(fn, "r");
    if (fp == NULL){
        printf("[-]No cached logout to send.\n");
        return;
    }

    // ottengo il timestamp
    fgets(timestamp, TIME_LEN+1, fp);
    fclose(fp);

    // invio comando al server
    send(server_sck, (void*)cmd, CMD_SIZE+1, 0);

    // invio il timestamp al server
    send(server_sck, (void*)timestamp, TIME_LEN+1, 0);

    remove(fn);

    printf("[+]Cached logout timestamp sent to server.\n");
}


/*
* Function: setup_new_con
* ---------------------------
* instaura una connessione TCP con il peer 'user' sulla porta 'peer_port'. Se la connessione viene correttamente 
* stabilita restituisce il socket creato, altrimenti restituisce -1.
*/
int setup_new_con(int peer_port, char* user){

    int peer_sck, ret;
    uint16_t port;
    struct sockaddr_in peer_addr;

    /* Creazione socket */
    peer_sck = socket(AF_INET, SOCK_STREAM, 0);

    // setsockopt(peer_sck, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    /* Creazione indirizzo del server */
    port = peer_port;
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET ;
    peer_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &peer_addr.sin_addr);

    ret = connect(peer_sck, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
    
    if(ret==-1){
        perror("[-]Can't connect to new contact");
        close(peer_sck);
        return -1;
    }
    else{
        // invio il nome
        basic_send(peer_sck, host_user);
        FD_SET(peer_sck, &master);    // aggiungo il socket al master set
        
        if(peer_sck > fdmax){ fdmax = peer_sck; } // aggiorno fdmax
    
        if (check_contact_list(user)==-1)   // se si tratta di un nuovo contatto
        {
                add_contact_list(user, peer_port);
        }

        peers = add_to_con(peers, peer_sck, user);

        return peer_sck;
    }
}


/*
* Function: send_offline_message
* ---------------------------------
* se il server è online invia il messaggio 'msg' destinato ad un utente offline perché venga salvato,
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

    // invio il testo del messaggio
    basic_send(server_sck, msg->text);

    printf("[+]Message sent to server.\n");

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
    char fn[BUFF_SIZE], fn1[BUFF_SIZE];
    char buff_info[BUFF_SIZE];
    char buff_chat[BUFF_SIZE];

    // apro i file se esistono
    sprintf(fn, "./%s/buffered_info.txt", host_user);
    sprintf(fn1, "./%s/buffered_texts.txt", host_user);

    if ( (fp = fopen(fn, "r"))==NULL || (fp1=fopen(fn1, "r"))==NULL){
        printf("[-]No stored messages to send.\n");
        return;
    }

    // recupero i dati dei messaggi bufferizzati
    while( fgets(buff_info, BUFF_SIZE, fp)!=NULL && fgets(buff_chat, MSG_LEN, fp1)!=NULL ) {
        char day[TIME_LEN];
        char hour[TIME_LEN];
    
        struct message* mess = (struct message*)malloc(sizeof(struct message));
        if (mess == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }
        sscanf(buff_info, "%s %s %s %s", mess->sender, mess->recipient, day, hour);
        sprintf(mess->time_stamp, "%s %s", day, hour);
        strcpy(mess->text, buff_chat);
        send_offline_message(mess);
        free(mess);
    }
    fclose(fp);
    fclose(fp1);

    remove(fn);
    remove(fn1);
    
    printf("[+]Stored messages sent to server.\n");

}


/*
* Function: setup_conn_server
* ------------------------------
* instaura una connessione TCP col server. Ritorna 1 se ha successo, -1 altrimenti.
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
    if (session_on==true){
        send_stored_messages_to_server();       
    }
    session_on = true;
    return 1;
}



/*
* Function:  ask_port
* ----------------------
* chiede e riceve dal server la porta di un determinato device
*/
int ask_port(char* user){

    uint16_t port;
    char cmd[CMD_SIZE+1] = "RCP"; //(ReCeive Port)


    // nel caso in cui il server risultava offline, provo a ristabilire la connessione
    if (SERVER_ON==false){
        int ret = setup_conn_server();
        if(ret==-1){    // se il server risulta ancora offline rinuncio ad inviare il messaggio
            return -1;
        }
    }
    send(server_sck, (void*)cmd, CMD_SIZE+1, 0);

    // invio il nome
    basic_send(server_sck, user);

    // ricevo la porta
    recv(server_sck, (void*)&port, sizeof(uint16_t), 0);
    port = ntohs(port);

    return port;
}


/*
* Function: send_message_to_peer
* ----------------------------------
* invia il messaggio 'm' al peer 'user'. Se ha successo ritorna 1, altrimenti -1.
*/
int send_message_to_peer(struct message* m, char* user){

    int ret, sck, port;
    char cmd[CMD_SIZE+1] = "RMP";

    sck = get_conn_peer(peers, user);     
    if (sck==-1){   // se non è ancora stata stabilita una connessione                              
        port = check_contact_list(user);        // ottengo la porta
        if(port == -1){
            ask_port(user);
        }
        sck = setup_new_con(port, user); // e provo a stabilirne una
    }
    if (sck==-1) return -1;     // se il peer risulta offline

    ret = send(sck, (void*)cmd, CMD_SIZE+1, 0);
    if (ret<=0){    // se ci sono problema nell'invio dei dati
        // suppongo che il peer sia offline
        remove_from_peers(&peers, user);      
        FD_CLR(sck, &master);

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

    // aggiorno lo stato del messaggio
    strcpy(m->status, "**");

    printf("[+]Message successfully sent to %s.\n", user);

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

    // nel caso in cui il server risultava offline, provo a ristabilire la connessione
    if (SERVER_ON==false){
        ret = setup_conn_server();
        if(ret==-1){    // se il server risulta ancora offline rinuncio ad inviare il messaggio
            return;
        }
    }

    // prima invio la richiesta del servizio
    printf("[+]Sending request to server.\n");
    ret = send(server_sck, (void*) command, CMD_SIZE+1, 0);

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
        printf("[+]Message from server:\n%s", message);
        fflush(stdout);
        return;
    }
    else if(strcmp(presence,"S")==0){   // success response
        // altrimenti ricevo la stringa da stampare
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
    char buff_info[BUFF_SIZE];
    char buff_chat[MSG_LEN];

    system("clear");
    printf("****************** CHAT WITH %s ******************\n", 
        (strcmp(current_chat->group, "-")==0)?current_chat->recipient:current_chat->group );

    if ( (fp = fopen(get_cache_name1(user),"r"))==NULL || (fp1 = fopen(get_cache_name2(user), "r"))==NULL){
        return;
    }

    // stampo tutti i messaggi nella cache
    while( fgets(buff_info, BUFF_SIZE, fp)!=NULL && fgets(buff_chat, MSG_LEN, fp1)!=NULL ) 
        sprintf("%s%s\n", buff_info, buff_chat);
  
    fclose(fp);
    fclose(fp1);
}


/*
* Function: show_online_users
* -----------------------------
* mostra una lista dei contatti attualmente online (eccetto la persona con cui si è in contatto al momento)
*/
void show_online_users(){

    int cur_port;
    char fn[BUFF_SIZE];
    char buffer [BUFF_SIZE];
    char list [BUFF_SIZE];
    FILE * fp;

    memset(list, 0, sizeof(list));

    sprintf(fn, "./%s/contact_list.txt", host_user);
    fp = fopen(fn, "r");

    if (fp==NULL){
        printf("[-]No contacts yet.\n");
        return;
    }

    while( fgets(buffer, sizeof buffer, fp)!= NULL){
        char cur_name[USER_LEN+1];

        sscanf(buffer, "%s %d", cur_name, &cur_port);
        printf("[+]Fetching contacts..\n");

        // controllo se ho già una conversazione aperta con questo contatto
        if (strcmp(cur_name, current_chat->recipient)){
            if (get_conn_peer(peers, cur_name)!=-1){
                // aggiungo il nome alla lista
                strcat(list, "\t");
                strcat(list, cur_name);
                strcat(list, "\n");
            }
            // altrimenti provo a instaurare una connessione tcp
            else if (setup_new_con(cur_port, cur_name)!=-1){
                // se ho successo, aggiungo il nome alla lista
                strcat(list, "\t");
                strcat(list, cur_name);
                strcat(list, "\n");

                // rimuovo la connessione
                FD_CLR(get_conn_peer(peers, cur_name), &master);
                remove_from_peers(&peers, cur_name);
            }
        }
        
    }

    fclose(fp);

    printf("*************** USERS ONLINE ***************\n");
    printf(list);  
}


/*
* Function: new_contact_handler
* -------------------------------
* gestisce l'invio del primo messaggio ad un nuovo contatto tramite il server. Riceve poi l'ack dal server,
* ed eventualmente la porta del nuovo contatto. Ritorna -1 se l'invio non è andato a buon fine, 0 se è stato 
* salvato dal server e 1 se il messaggio è stato inviato al nuovo contatto, -2 se è stato salvato localmente
*/
int new_contact_handler(char* user, struct message* m){

    uint8_t ack;
    uint16_t port;
    char buffer[BUFF_SIZE];
    char cmd[CMD_SIZE+1] = "NCH";

    // nel caso in cui il server risultasse offline provo a ricontattarlo
    if (SERVER_ON == false && setup_conn_server()==-1){
        // se risulta ancora offline mi limito a salvare il messaggio localmente
        store_message(m);
        printf("[-]Couldn't send message to server.\n");
        return 0;
    }

    // invio il comando al server
    if ( send(server_sck, (void*)cmd, CMD_SIZE+1, 0)<=0 ){  
        // se non va a buon fine si deduce che il server è offline
        printf("[-]Server may be offline.\n");
        close(server_sck);
        FD_CLR(server_sck, &master);
        SERVER_ON = false;
        return -2;
    }
    
    // invio il nome utente al server
    basic_send(server_sck, user);

    memset(buffer, 0, sizeof(buffer));
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
    basic_send(server_sck, m->recipient);
    basic_send(server_sck, m->sender);
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
    current_chat->sck = setup_new_con(port, user);

    strcpy(m->status, "**");

    return 1;
}

/*
* Function: send_group_info
* -------------------------------
* invia tramite il socket sck, informazioni riguardo un nuovo membro del gruppo.
* se update è true, invio informazioni soltanto sul nuovo membro aggiunto,
* altrimenti se falso invio informazioni su tutti i membri del gruppo eccetto l'ultimo,
*/
void send_group_info(int sck, bool update){

    uint16_t port;
    char cmd[CMD_SIZE+1] = "RGI";

    // invio il comando
    send(sck, (void*)cmd, CMD_SIZE+1, 0);

    // invio il nome del gruppo
    basic_send(sck, current_chat->group);

    if (update){    // notifica dell'aggiunta di un nuovo membro
        basic_send(sck, current_chat->members->username);
        port = check_contact_list(current_chat->members->username);
        port = htons(port);
        send(sck, (void*)&port, sizeof(uint16_t), 0);
    }
    else{   // notifica ad un nuovo membro

        uint8_t count = current_chat->users_counter-1;

        struct con_peer* cur = current_chat->members->next; 
        // invio il numero di membri
        send(sck, (void*)&count, sizeof(uint8_t), 0);

        while(cur){
            // invio il nome del membro
            basic_send(sck, cur->username);

            // invio la porta del membro
            port = check_contact_list(cur->username);
            port = htons(port);
            send(sck, (void*)&port, sizeof(uint16_t), 0);

            cur = cur->next;
        }
    }
}


/*
* Function: create_group
* ------------------------------
* inizializza una struttura chat di gruppo a partire dalla chat corrente
*/
void create_group(){

    FILE *fp, *fp1;

    struct con_peer* m1 = (struct con_peer*)malloc(sizeof(struct con_peer));
    struct con_peer* m2 = (struct con_peer*)malloc(sizeof(struct con_peer));

    if (m1 == NULL || m2 == NULL){
        perror("[-]Memory not allocated");
        exit(-1);
    }

    // assegno un nome al gruppo
    strcpy(current_chat->group, host_user);
    strcat(current_chat->group, "group");

    // aggiungo il creatore del gruppo
    m1->socket_fd = -1;
    strcpy(m1->username, host_user);

    // aggiungo il secondo membro
    m2->socket_fd = get_conn_peer(peers, current_chat->recipient);
    strcpy(m2->username, current_chat->recipient);
    m2->next = m1;

    current_chat->members = m2;

    // invio la notifica di nuovo gruppo al secondo membro
    send_group_info(m2->socket_fd, false);

    // creo le cache apposite
    fp = fopen(get_cache_name1(current_chat->group), "a");
    fp1 = fopen(get_cache_name2(current_chat->group), "a");
    if (fp== NULL || fp1==NULL){
        perror("[-]Error allocating memory");
        exit(-1);
    }

    printf("[+]Group created.\n");
}


/*
* Function: add_member
* ------------------------
* aggiungi un nuovo 'user' alla chat corrente
*/
void add_member(char *user){

    int sck_user, port;
    struct con_peer* new_member, *temp;

    // controllo se è ancora attiva una conversazione con user
    sck_user = get_conn_peer(peers, user);

    if (sck_user==-1){     // se non è presente tra le con_peer
        // instauro una nuova connessione tcp con user
        port = check_contact_list(user);
        sck_user = setup_new_con(port, user);
    }

    // se è il primo utente aggiunto al gruppo (i.e. terzo nella chat)
    if (strcmp(current_chat->group, "-")==0)
        create_group();

    // aggiungo il nuovo membro    
    new_member = (struct con_peer*)malloc(sizeof(struct con_peer));
    if (new_member == NULL){
        perror("[-]Memory not allocated");
        exit(-1);
    }

    strcpy(new_member->username, user);
    new_member->socket_fd = sck_user;
    new_member->next = current_chat->members;
    current_chat->members = new_member;
    current_chat->users_counter++;

    printf("[+]Group updated.\n");

    // invio la notifica di gruppo al nuovo membro
    send_group_info(new_member->socket_fd, false);

    // invio la notifica dell'aggiunta di un membro agli altri membri del gruppo
    temp = current_chat->members->next;
    while(temp){
        send_group_info(temp->socket_fd, true);
        temp = temp->next;
    }

    printf("[+]New user %s added to group.\n", user);

    return;
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

    char buffer[MSG_LEN];
    char timestamp [TIME_LEN+1];
    char *token;
    char* b = buffer;
    size_t dimbuf = MSG_LEN;

    memset(buffer, 0, sizeof(buffer));

    // salvo l'input dell'utente
    getline(&b, &dimbuf, stdin);

    // elimino newline dalla stringa
    b[strlen(b)-1] = '\0';

    // controllo prima se l'input è un comando
    if (strcmp(b, "\\q")==0){             // richiesta di chiusura della chat

        printf("[+]Chat correctly closed.\n");
        current_chat->on = false;
        menu_client();
        return;
    }

    else if (strcmp(b, "\\u")==0){        // richiesta della lista degli utenti online
        
        show_online_users();
    } 

    else if ( strncmp(b, "\\a ", 3)==0 ){   // richiesta di aggiungere un nuovo membro alla chat

        //ricavo il nome dello user
        // buffer[strcspn(buffer, "\n")] = '\0';
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
                
            // se c'è già stato un precedente contatto con questo user
            if (access(get_cache_name1(current_chat->recipient), F_OK) == 0) {    // il file esiste
                // invio il messaggio al peer
                if (send_message_to_peer(new_msg, current_chat->recipient)==-1){
                    // se il peer risulta offline, invio il messaggio al server
                    send_offline_message(new_msg);
                }               
            } else {    // cache non esistente => nuovo contatto
                int ret = new_contact_handler(current_chat->recipient, new_msg);
                if(ret == -1){     // nel caso in cui il nome non fosse valido
                    current_chat->on = false;
                    return;
                }
            }
        }
        // messaggio di gruppo
        else{   

            // invio il messaggio ad ogni membro del gruppo
            struct con_peer* temp = current_chat->members;
            strcpy(new_msg->group, current_chat->group);

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
        show_history(strcmp(current_chat->group, "-")==0?current_chat->recipient:current_chat->group);   
    }
} 


/*
* Function: start_chat
* --------------------------
* avvia una chat e stampa una eventuale cronologia
*/
void start_chat(char* user){

    struct chat* cur = ongoing_chats;

    // controllo parametro di ingresso
    if (strcmp(user, host_user)==0){
        printf("[-]Attempt to chat with yourself.\n");
        return;
    }

    printf("[+]Starting chat...\n");

    // cerco se tra le ongoing chat c'è quella con user
    while(cur!=NULL){
        
        if ( strcmp(cur->group, user)==0 || strcmp(cur->recipient, user)==0){
            current_chat = cur;
            break;
        }
        cur = cur->next;
    }
    
    if (cur == NULL){   // se non presente creo una nuova istanza di chat
        cur = (struct chat*)malloc(sizeof(struct chat)); 
        if (cur == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }
        
        strcpy(cur->group, "-");
        cur->sck = -1;
        strcpy(cur->recipient, user);
        cur->on = true;
        cur->users_counter = 2;

        current_chat = cur;
    }

    // stampo la crnologia dei messaggi
    show_history(strcmp(cur->group, "-")==0?cur->recipient:cur->group);   
}


/*
* Function: leave_group
* ----------------------------
* termina una sessione di gruppo creata da altri
*/
void leave_group(int sck){

    char grp_name[BUFF_SIZE];
    struct chat* temp = ongoing_chats;
    struct chat* prev;

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

    // rimuovo la chat di gruppo dalla lista delle ongoing chats
	if (temp != NULL && strcmp(temp->group, grp_name)==0) {
		ongoing_chats = temp->next; 
		free(temp); 
	}
    else{
        while (temp != NULL && strcmp(temp->group, grp_name)!=0) {
            prev = temp;
            temp = temp->next;
        }
        prev->next = temp->next;

        free(temp);
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
        if (strcmp(temp_chat->group, "-")!=0){   
            
            //nel caso elimino le cache corrispondenti
            remove(get_cache_name1(temp_chat->group));
            remove(get_cache_name2(temp_chat->group));

            sprintf(buff, "%sgroup", host_user);
            if (strcmp(temp_chat->group, buff)==0){
                my_group = temp_chat;
            }
        }
        temp_chat = temp_chat->next;
    }

    if (temp_chat==NULL){
        printf("[+]No owned group found.\n");
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

    printf("[+]Logging out..\n");

    // chiudo un eventuale gruppo
    terminate_group();

    // salvo timestamp corrente
    time_t now = time(NULL);
    strftime(timestamp, TIME_LEN, "%Y-%m-%d %H:%M:%S", localtime(&now));

    // invio comando al server
    ret = send(server_sck, (void*)cmd, CMD_SIZE+1, 0);

    if(ret<=0){ // se offline
        char fn[FNAME_LEN+1];
        
        printf("[-]Server is offline.\n");

        sprintf(fn, "./%s/last_logout.txt", host_user);
        fp = fopen(fn, "a");
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
    uint8_t many;
    char message [BUFF_SIZE];
    struct message* list = NULL;
    struct message* next;
    char command [CMD_SIZE+1] = "SHW";


    // controllo il dato in ingresso
    if (strcmp(user, host_user)==0){
        printf("[-]Error: invalid user.\n");
        return;
    }

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
    mess_counter = many;
    if (mess_counter==0){
        printf("\n\tNo hanging messages from %s\n", user);
        return;
    }

    while (mess_counter>0){
         
        char day[TIME_LEN];
        char hour[TIME_LEN];

        memset(day, 0, sizeof(day));
        memset(hour, 0, sizeof(hour));

        struct message* m = (struct message*)malloc(sizeof(struct message));
        if (m == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }

        memset(message, 0, sizeof(message));
        // ricevo info sul messaggio
        basic_receive(server_sck, message);

        sscanf (message, "%s %s %s", day, hour, m->sender);
        sprintf(m->time_stamp, "%s %s", day, hour);
        strcpy(m->group, "-");
        strcpy(m->status, "-");
        strcpy(m->recipient, host_user);

        // ricevo il contenuto del messaggio
        basic_receive(server_sck, m->text);

        // aggiungere i messaggi nella cache
        save_message(m);

        // aggiungo il messaggio in fondo alla lista
        if(list==NULL){
            list = m;
        }
        else{
            struct message* last = list;
            while(last->next!=NULL)
                last = last->next;
            last->next = m;
        }

        mess_counter--;
    }

    // nel caso fossero messaggi di un nuovo contatto chiedo la porta al server
    if (check_contact_list(user)==-1){  
        
        uint16_t new_port;
       
        new_port = ask_port(user);
        if (new_port != -1){
            // aggiungo il contatto
            add_contact_list(user, new_port);

        }
    }

    // stampo i messaggi
    while(list!=NULL){
        print_message(list);
        next = list->next;
        free(list);
        list = next;
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

    memset(buffer, 0, sizeof(buffer));

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
* se il valore ritornato è positivo l'utente è stato correttamente registrato.
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
    memset(response, 0, sizeof(response));
    recv(server_sck, (void*)response, RES_SIZE+1, 0);

    if (strcmp(response,"E")==0){    // negative response
        
        memset(message, 0, sizeof(message));
        basic_receive(server_sck, message);

        close(server_sck);
        fflush(stdout);
        printf("[+]Message from server:\n");
        printf(message);
        fflush(stdout);
        return -1;
    }
    else if(strcmp(response,"S")==0){   // success response

        char buff[BUFF_SIZE];

        memset(buff, 0, sizeof(buff));

        printf("[+]Account succesfully created.\n");
        sprintf(buff, "./%s/", host_user);
        
        // creo una directory per l'utente
        if (!mkdir(user, 0777)){
            printf("[+]Directory created.\n");
        }
        else{
            perror("[-]Error while creating subdirectory");
            exit(-1);
        }
        close(server_sck);
        return 1;
    }

    return 1;
}


/*
* Function: group_handler
* ----------------------
* aggiungo un gruppo alla lista delle conversazioni attive
*/
void group_handler(int sck){

    struct chat* cur = ongoing_chats;
    char name[USER_LEN+1];
    uint16_t port;
    int n_sck;

    // ricevo nome del gruppo
    basic_receive(sck, name);

    // controllo se il gruppo esiste
    while(cur){
        
        if ( strcmp(cur->group, name)==0){
            current_chat = cur;
            break;
        }
        cur = cur->next;
    }

    if (cur){   // se esite la notifica riguarda l'aggiunta di un nuovo membro

        struct con_peer* nm = malloc(sizeof(struct con_peer));
        if (nm == NULL){
            perror("[-]Error allocating memory");
            exit(-1);
        }
        // ricevo il nome del nuovo membro
        basic_receive(sck, name);
        recv(sck, (void*)&port, sizeof(uint16_t), 0);
        port = ntohs(port);
        // aggiungo al gruppo
        n_sck = get_conn_peer(peers, name);
        if (n_sck == -1){
            setup_new_con(port, name);
        }
        strcpy(nm->username, name);
        nm->socket_fd = n_sck;

        nm->next = cur->members;
        cur->members = nm;        
    }
    else{   // altrimenti creo un nuovo gruppo col nuovo nome

        FILE *fp, *fp1;
        uint8_t counter;
        struct chat* group_chat = malloc(sizeof(struct chat));
        if (group_chat == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }

        // ricevo il numero dei membri
        strcpy(group_chat->group, name);
        recv(sck, (void*)&counter, sizeof(uint8_t), 0);
        counter = ntohs(counter);

        while(counter){

            struct con_peer* member = (struct con_peer*)malloc(sizeof(struct con_peer));
            if (member == NULL){
                perror("[-]Memory not allocated");
                exit(-1);
            }

            basic_receive(sck, member->username);           // ricevo lo username
            recv(sck, (void*)&port, sizeof(uint16_t), 0);   // ricevo la porta
            port = ntohs(port);
            n_sck = get_conn_peer(peers, member->username);
            if (n_sck==-1){
                n_sck = setup_new_con(port, member->username);
            }
            member->socket_fd = n_sck;

            member->next = group_chat->members;
            group_chat->members = member;

            counter--;
        }

        // aggiungo il nuovo gruppo alla lista delle conversazioni
        if (ongoing_chats==NULL){
            ongoing_chats = group_chat;
        }
        else{
            group_chat->next = ongoing_chats;
            ongoing_chats = group_chat;
        }

        // creo due nuovi file cache per i nuovi messaggi
        fp = fopen(get_cache_name1(group_chat->group), "a");
        fp1 = fopen(get_cache_name2(group_chat->group), "a");

        printf("[+]New group added.\n");
        fclose(fp);
        fclose(fp1);
    }     
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

    printf("[+]Receiving new message.\n");

    // ricevo il gruppo
    basic_receive(sck, new_msg->group);

    // ricevo il sender
    basic_receive(sck, (void*)new_msg->sender);

    // ricevo il timestamp
    recv(sck, (void*)new_msg->time_stamp, TIME_LEN+1, 0);

    // ricevo il messaggio
    basic_receive(sck, new_msg->text);

    // se è il server ad inviare il messaggio allora è un nuovo contatto
    if (sck==server_sck){
        uint16_t port;

        recv(sck, (void*)&port, sizeof(uint16_t), 0);
        port = ntohs(port);
        add_contact_list(new_msg->sender, port);
    }

    strcpy(new_msg->status, "-");

    // salvo il messaggio nella cache apposita
    save_message(new_msg);

    // mostro unaa notifica se al momento non è aperta la chat a cui il messaggio appartiene
    if( current_chat == NULL || current_chat->on==false || 
        ( strcmp(current_chat->group, "-")==0 && strcmp(current_chat->recipient, new_msg->sender)!=0) ||
        ( strcmp(current_chat->group, "-")!=0 && strcmp(current_chat->group, new_msg->group)!=0 )
    ){
        if (strcmp(new_msg->group, "-")==0)
            printf("\t[New message from %s]\n", new_msg->sender);
        else
            printf("\t[New message from %s]\n", new_msg->group);
    }
    else{   // altrimenti stampo il messaggio
        print_message(new_msg);
    }

    if (current_chat!=NULL && current_chat->on){
        sleep(2);
        system("clear");
        if (strcmp(current_chat->group, "-")==0){
            show_history(current_chat->recipient);
        }
        else{
            show_history(current_chat->group);
        }
    }
}


/*
* Function: receive_single_ack
* ------------------------------
* riceve un ack e aggiorna la relativa cache
*/
void receive_single_ack(){

    char rec[USER_LEN+1];
    char temp[TIME_LEN+1];

    basic_receive(server_sck, rec);     // ricevo il destinatario
    recv(server_sck, (void*)temp, TIME_LEN+1, 0);   // ricevo il timestamp del messaggio meno recente
    
    // aggiorna la corrispondente cache dei messaggi
    update_ack(rec);

    if(current_chat!=NULL && current_chat->on && !strcmp(current_chat->group, "-") && !strcmp(current_chat->recipient, rec)){
        show_history(rec);
    }
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
        
        receive_single_ack();
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

    memset(input, 0, sizeof(input));
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

    memset(&sv_addr, 0, sizeof(sv_addr));
    sv_addr.sin_family = AF_INET;
    sv_addr.sin_addr.s_addr = INADDR_ANY;
    sv_addr.sin_port = htons(client_port);
    inet_pton(AF_INET, "127.0.0.1", &sv_addr.sin_addr);

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
                    if (newfd<0){
                        perror("[-]Error in accept");
                    }
                    else{
                        char new_con[USER_LEN+1];
                        FD_SET(newfd, &master);             // aggiungo il nuovo socket
                        if(newfd > fdmax){ fdmax = newfd; } // aggiorno fdmax

                        // ricevo il nome
                        basic_receive(newfd, new_con);

                        // aggiungo alla lista peers
                        peers = add_to_con(peers, newfd, new_con);
                    }
                }
                else { // il socket connesso è pronto

                    memset(cmd, 0, sizeof(cmd));
                    nbytes = recv(i, (void*)cmd, CMD_SIZE+1, 0);
                    if (nbytes<=0){
                        if (SERVER_ON && i == server_sck){
                            printf("[-]Server is now offline.");
                            close(i);
                        }
                        else
                            remove_from_peers(&peers, get_name_from_sck(peers, i));
                        FD_CLR(i, &master);
                    }
                    else{
                        if (strcmp(cmd, "RMP")==0){
                            receive_message_handler(i);
                        }
                        else if (strcmp(cmd, "EOG")==0){
                            leave_group(i);
                        }
                        else if (strcmp(cmd, "AK1")==0){
                            receive_single_ack();
                        }
                        else if (strcmp(cmd, "RGI")==0){
                            group_handler(i);
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

    send_last_log();
    send_stored_messages_to_server();
    receive_acks();
    server_peers();

    return 0;
}
