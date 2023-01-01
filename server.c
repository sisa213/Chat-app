/*
* Chat-app: applicazione network di messaggistica istantanea. 
*
*server.c:   implementazione del server dell'applicazione di rete. 
*
* Samayandys Sisa Ruiz Muenala, 10 novembre 2022
*
*/
#include "utility_s.c" 

fd_set master;                              // set master
fd_set read_fds;                            // set di lettura per la select
int fdmax;                                  // numero max di descrittori
struct session_log* connections = NULL;     // lista delle sessioni attualmente attive
struct message* messages = NULL;            // lista dei messaggi da bufferizzare (destinati ad utenti attualmente offline)


/*
 * Function:  show_home
 * -----------------------
 * mostra il menù iniziale del server.
 */
void show_home(){

    system("clear");

    printf("***********************************");
    printf(" SERVER STARTED ");
    printf("***********************************\n");
    printf("Digita un comando:\n\n");
    printf("1)  help --> mostra i dettagli dei comandi\n");
    printf("2)  list --> mostra un elenco degli utenti connessi\n");
    printf("3)  esc  --> chiude il server\n");
}


/*
* Function: help_display
* ------------------------
* mostra in dettaglio le funzionalità dei comandi.
*/
void help_display(){

    printf("\n***************** COMMANDS *****************\n");
	printf("\n-> help:  mostra questa breve descrizione dei comandi;\n");
	printf("-> list:  mostra l’elenco degli utenti connessi alla rete indicando username, timestamp di connessione e numero di porta;\n");
	printf("-> esc:   termina il server. Le chat in corso possono proseguire.\n");
}


/*
* Function: setup_lists
* ------------------------
* si occupa di inizializzare le liste connections e messages all'avvio del server.
*/
void setup_lists(){

    FILE *fptr, *fptr1;
    char buffer[BUFF_SIZE];
    char buff_info[BUFF_SIZE];
    char buff_chat[MSG_LEN];
    char user[USER_LEN+1];
    char time_login[TIME_LEN+1];
    char time_logout[TIME_LEN+1];
    char sen[USER_LEN+1];
    char rec[USER_LEN+1];
    char time[TIME_LEN+1];
    char grp[USER_LEN+20];     
    char st[3];     
    int port, size;

    printf("\n[+]Setting up starting lists..\n");

    // prima inizializzo la lista delle sessioni ancora aperte (per cui non ho ricevuto il logout)
    if ((fptr = fopen("./active_logs.txt","r")) == NULL){
        perror("[-]Error opening file");
        return;
    }
    printf("[+]Log file correctly opened.\n");

    // controllo che il file non risulti vuoto
    fseek (fptr, 0, SEEK_END);
    size = ftell(fptr);

    if (size==0) {  // se non lo è
        printf("[-]File is empty.\n");
    }
    else{
            // scompongo ogni riga del file nei singoli campi dati 
        while(fgets(buffer, BUFF_SIZE, fptr) != NULL) {
            struct session_log* temp;
            sscanf (buffer, "%s %d %s %s",user,&port,time_login,time_logout);        
                        
            temp = (struct session_log*)malloc(sizeof(struct session_log));
            if (temp == NULL){
                perror("[-]Memory not allocated\n");
                exit(-1);
            }
            else
            {   // aggiungo un nuovo elemento alla lista delle connessioni
                strcpy(temp->username,user);
                temp->port = port;
                temp->socket_fd = -1;
                strcpy(temp->timestamp_login, time_login);
                strcpy(temp->timestamp_logout, time_logout);
                temp->next = NULL;
                if(connections == NULL){
                    connections = temp;
                }
                else
                {
                    struct session_log* lastNode = connections;
                    while(lastNode->next != NULL)
                        lastNode = lastNode->next;
                    lastNode->next = temp;        
                }
            }  
            free(temp);         
        }
        fclose(fptr);  
    }
    printf("[+]Connections list correctly initialized.\n");

    // ora inizializzo la lista dei messaggi
    if ( (fptr = fopen("./chat_info.txt","r")) != NULL && (fptr1 = fopen("./chats.txt","r")) != NULL)  {
        printf("[+]Chat files correctly opened.\n");
    }
    else{
        perror("[-]Error opening file");
        return;
    }

    // recupero i dati dei messaggi bufferizzati
    while( fgets(buff_info, BUFF_SIZE, fptr)!=NULL && fgets(buff_chat, MSG_LEN, fptr1)!=NULL ) {

        struct message* temp = (struct message*)malloc(sizeof(struct message));
        if (temp == NULL){
            perror("[-]Memory not allocated\n");
            exit(-1);
        }
        sscanf(buff_info, "%s %s %s %s %s", sen, rec, time, grp, st);
        strcpy(temp->sender, sen);
        strcpy(temp->recipient, rec);
        strcpy(temp->time_stamp, time);
        strcpy(temp->text, buff_chat);
        strcpy(temp->group, grp);
        strcpy(temp->status, st);
        temp->next = NULL;
        // aggiungo alla lista dei messaggi
        if(messages == NULL)
            messages = temp;
        else
        {
            struct message* lastNode = messages;
            while(lastNode->next != NULL)
                lastNode = lastNode->next;
            lastNode->next = temp;        
        }
        free(temp);
    }
    printf("[+]Messages list correctly initialized.\n");
    fclose(fptr);  
    fclose(fptr1);

    return;

}                                                             


/*
* Function: get_socket
* -----------------------
* dato un nome utente restituisce il numero del socket associato alla connessione del suo device se online,
* altrimenti -1.
*/
int get_socket(char* user){

    int ret = -1;
    struct session_log* temp = connections;

    printf("[+]Getting socket of %s..\n", user);

    while (temp){

        if (strcmp(temp->username, user)==0 && strcmp(temp->timestamp_logout, NA_LOGOUT)==0 && temp->socket_fd!=-1){
            ret = temp->socket_fd;
            printf("[+]Socket obtained.\n");
            break;
        }
        temp = temp->next;
    }

    free(temp);
    return ret;
}


/*
* Function: logout
* ------------------
* si occupa di aggiornare una determinata sessione nella lista delle connessioni modificando timestamp di logout ed eventualmente n° di socket.
* sono distinti i casi in cui il logout è regolare, irregolare, oppure si tratta di un logout relativo ad una sessione precedente.
*/
void logout(int socket, bool regular){  

    char user[USER_LEN+1];
    char buff[TIME_LEN+1];
    struct session_log* temp = connections;
    struct session_log* temp1 = connections;

    printf("[+]Logout working..\n");

    if (!regular){      // nel caso di disconnessione irregolare
        time_t now = time(NULL);
        strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
    }
    else{
        // ricevo il timestamp dilogout
        recv(socket, (void*)buff, TIME_LEN+1, 0);
    }
    printf("[+]Logout timestamp obtained.\n");

    //ricavo il suo username tramite il socket
    printf("[+]Looking for username..\n");
    while(temp){
        if(temp->socket_fd==socket){
            strcpy(user, temp->username);
            printf("[+]Username found.\n");
            break;
        }
        temp = temp->next;
    }

    // cerco un'altra connessione nella lista che abbia questo username e timestamp di logout NULLo
    // aggiorno quindi il timestamp di logout

    printf("[+]Looking for session log to update..\n");

    while (temp1)
    {
        if( strcmp(temp1->username, user)==0 && strcmp(temp1->timestamp_logout, NA_LOGOUT)==0 )
        {
            strcpy(temp1->timestamp_logout, buff);
            printf("[+]Session updated.\n");
            break;
        }
        temp1 = temp1->next;
    }

    // se è stato aggiornata una sessione corrente chiudo il socket
    if (temp1->socket_fd!=-1){

        close(socket);
        FD_CLR(socket, &master);
        printf("[+]Socket closed.\n");
    }  

    printf("[+]Logout successfully completed.\n");

}


/*
* Function: show_list
* ---------------------
* mostra l’elenco degli utenti connessi alla rete, indicando username, timestamp di connessione e numero di
* porta nel formato “username*timestamp*porta”
*/
void show_list(){

    struct session_log* temp = connections;

    // scorro la lista delle sessioni e stampo dettagli di quelle attive
    printf("\n************* USERS ONLINE *************\n");
    printf("\n\tUSER*TIME_LOGIN*PORT\n\n");
    while(temp){
        if ( strcmp(temp->timestamp_logout, NA_LOGOUT)==0 && temp->socket_fd!=-1 ){
            printf("\t");
            printf("%s*%s*%d\n", temp->username, temp->timestamp_login, temp->port);
        }
        temp = temp->next;
    }

    free(temp);
}


/*
* Function: terminate_server
* ----------------------------
* Termina il server. Salva le sessioni attive e i messaggi bufferizzati in file e si occupa di chiudere tutti i socket attivi.
*/
void terminate_server(){

    FILE *fptr;
    FILE *fptr1;
    struct session_log* temp = connections;
    struct message* node = messages;

    printf("[+]Terminating server..\n");

    // apro i file relativi ai log delle sessioni
    if ( (fptr = fopen("./active_logs.txt","w"))!=NULL && (fptr1 = fopen("./logs_ar.txt", "a"))!=NULL ){
        printf("[+]Log files correctly opened.\n");
    }
    else{
        perror("[-]Error opening files");
        return;
    }                                                                    
   
    // scorro le sessioni
    while(connections){

        connections = connections->next;
        if ( strcmp(temp->timestamp_logout, NA_LOGOUT)==0 ){

            // salvo le sessioni ancora attive
            fprintf(fptr, "%s %d %s %s\n", 
                temp->username, temp->port, temp->timestamp_login, temp->timestamp_logout);
            if (temp->socket_fd!=-1){
                close(temp->socket_fd);
                FD_CLR(temp->socket_fd, &master);
            }
        }
        else{
            // salvo le sessioni chiuse
            fprintf(fptr1, "%s %d %s %s\n", 
                temp->username, temp->port, temp->timestamp_login, temp->timestamp_logout);
        }

        free(temp);
        temp = connections;
    }
    printf("[+]Log files correctly updated.\n");

    fclose(fptr);  
    fclose(fptr1);

    // salvo i messaggi pendenti
    if ( (fptr = fopen("./chat_info.txt","w"))!=NULL && (fptr1 = fopen("./chats.txt","w")) != NULL)  {
        printf("[+]Chat files correctly opened.\n");
    }
    else{
        perror("[-]Error opening files");
        return;
    }

    while(messages){
        messages = messages->next;
        fprintf(fptr, "%s %s %s %s\n", 
                node->sender, node->recipient, node->time_stamp, node->group);
        fprintf(fptr1,"%s\n", node->text);
        free(node);
        node = messages;
    }
    printf("[+]Chat files correctly updated.\n");  

    fclose(fptr);  
    fclose(fptr1);
    printf("[+]Server terminated.\n\n");
    exit(1);
}


/*
 * Function:  login
 * ----------------
 * registra il login di un dispositivo client.
 */
void login(int dvcSocket)     
{
    FILE *fptr;
    char buff[BUFF_SIZE];
    char cur_name[USER_LEN+1];
    char cur_pw[USER_LEN+1];
    char psw[USER_LEN+1];
    char user[USER_LEN+1];
    char t_buff[TIME_LEN+1];
    uint16_t message_len;
    uint16_t port;
    time_t now = time(NULL);
    bool found = false;
    struct session_log* new_node = (struct session_log*)malloc(sizeof( struct session_log));
    if (new_node == NULL){
        perror("[-]Memory not allocated\n");
        exit(-1);
    }

    //ricevo usernmae e psw
    recv(dvcSocket, (void*)&message_len, sizeof(uint16_t), 0);          // ricevo la dimesione dello username
    message_len = ntohs(message_len);
    recv(dvcSocket, (void*)user, message_len, 0);                       // ricevo lo username

    recv(dvcSocket, (void*)&message_len, sizeof(uint16_t), 0);          // ricevo dimensione psw
    message_len = ntohs(message_len);
    recv(dvcSocket, (void*)psw, message_len, 0);                        // ricevo password

    recv(dvcSocket, (void*)&port, sizeof(uint16_t), 0);                 // ricevo il numero della porta
    port = ntohs(port);

    printf("[+]Received credentials for login: %s, %s.\n", user, psw);


    // confronto username e psw con i dati di users.txt
    fptr = fopen("./users.txt","r");
    printf("[+]Users file correctly opened for reading.\n");
    while ( fgets( buff, sizeof buff, fptr ) != NULL )
    {
        sscanf (buff, "%s %s %*d", cur_name, cur_pw);            

        if ( strcmp(cur_name, user) == 0 && strcmp(cur_pw, psw) == 0 )
        {   printf("[+]Found a match for received credentials.\n");
            found = true;
            break;
        }
    }

    if (!found){
        printf("[-]Could not find matching credentials in database.\n");
        send_server_message(dvcSocket, "Credenziali non trovate nel database!\n", true);
        return;
    }

    // aggiungo in coda alla lista delle connessioni gestite in questa sessione
    new_node->next = NULL;
    new_node->port = port;
    new_node->socket_fd = dvcSocket;
    strftime(t_buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
    strcpy(new_node->username, user);
    strcpy(new_node->timestamp_login, t_buff);
    strcpy(new_node->timestamp_logout, NA_LOGOUT);

    printf("[+]Login:\n");
    printf("\t%s", new_node->username);
    printf(" %s", new_node->timestamp_login);
    printf(" %s\n", new_node->timestamp_logout);

    if(connections == NULL){
        connections = new_node;
        connections->next = NULL;
    }
    else
    {   
        struct session_log* lastNode = connections;
    
        while(lastNode->next != NULL)
        {
            lastNode = lastNode->next;
        }
        lastNode->next = new_node;
    }

    printf("[+]Login saved.\n");

    //invio un messaggio di conferma all'utente
    send_server_message(dvcSocket, NULL, false);

    FD_SET(dvcSocket, &master);

    // aggiorno fdmax
    if(dvcSocket>fdmax){
        fdmax = dvcSocket;
    } 
}


/*
 * Function:  signup
 * -----------------
 * registra un nuovo dispositivo client
 */
void signup(int dvcSocket){

    FILE *fpta;
    char new_psw[USER_LEN+1];
    char new_user[USER_LEN+1];
    uint16_t message_len, new_port;

    printf("[+]Signup handler in action.\n");

    // ricevo lo user
    recv(dvcSocket, (void*)&message_len, sizeof(uint16_t), 0);  
    message_len = ntohs(message_len);
    recv(dvcSocket, (void*)new_user, message_len, 0);               

    // ricevo la password
    recv(dvcSocket, (void*)&message_len, sizeof(uint16_t), 0);        
    message_len = ntohs(message_len);
    recv(dvcSocket, (void*)new_psw, message_len, 0);            

    // ricevo la porta
    recv(dvcSocket, (void*)&new_port, sizeof(uint16_t), 0);
    new_port = ntohs(new_port);

    printf("[+]Received credentials: user: %s, crypted psw: %s\n", new_user, new_psw);

    // controllo l'eventuale presenza di un omonimo, se presente scarto il dato in entrata            
    if ( check_username(new_user) == 1 )
    {
        //invio segnalazione al client
        send_server_message(dvcSocket, "Username già presente nel database. Scegliere un altro username!", true);
        return;
    }

    // apro il file users.txt per aggiungere il nuovo utente
    fpta = fopen("./users.txt","a");
    printf("[+]Users file correctly opened for appending.\n");
    fprintf(fpta, "%s %s %d\n", new_user, new_psw, new_port);
    fflush(fpta);
    fclose(fpta);

    printf("[+]New user registered.\n");

    send_server_message(dvcSocket, NULL, false);

    close(dvcSocket);
} 


/*
* Function: offline_message_handler
* ------------------------------------
* salva i messaggi pendenti ( destinati ad utenti attualmente offline )
*/
void offline_message_handler(int s_client){

    char t_buff[TIME_LEN+1];
    char sender[USER_LEN+1];
    char rec[USER_LEN+1];
    char m_buff[MSG_LEN];
    char grp[USER_LEN+2];
    struct session_log* temp = connections;

    printf("[+]Handling offline message..\n");

    struct message* new_node = (struct message*)malloc(sizeof(struct message));
    if (new_node == NULL){
        perror("[-]Memory not allocated");
        exit(-1);
    }

    // ricevo il mittente
    basic_receive(s_client, sender);

    // ricevo il destinatario
    basic_receive(s_client, rec);

    // ricevo il timestamp
    recv(s_client, (void*)t_buff, TIME_LEN+1, 0);

    // ricevo il gruppo
    basic_receive(s_client, grp);

    // ricevo il testo del messaggio
    basic_receive(s_client, m_buff);
    printf("[+]Messaggio received.\n");

    strcpy(new_node->recipient, rec);
    strcpy(new_node->sender, sender);
    strcpy(new_node->time_stamp, t_buff);
    strcpy(new_node->status, "*");
    strcpy(new_node->group, grp);
    strcpy(new_node->text, m_buff);

    new_node->next = NULL;

    //aggiungo alla lista dei messaggi
    if(messages == NULL)
         messages = new_node;
    else
    {
        struct message* lastNode = messages;
        while(lastNode->next != NULL)
        {
            lastNode = lastNode->next;
        }
        lastNode->next = new_node;
    }
    free(new_node);
    free(temp);

    printf("[+]Messagge saved.\n");

}


/*
* Function: send_message_to peer
* -------------------------------
* invia un messaggio ad un device.
* Restituisce -1 se l'invio non va a buon fine, altrimenti restituisce 1.
*/ 
int send_message_to_peer(int sck, struct message* m){

    char cmd[CMD_SIZE+1] = "RMP";

    // invio il comando di messaggio
    if ( send(sck, (void*)cmd, CMD_SIZE+1, 0)<=0 )
    {   printf("[-]Can't connect with %s is offline.\n", m->recipient);
        return -1;
    }

    // invio il gruppo
    basic_send(sck, m->group);

    // invio il mittente
    basic_send(sck, m->sender);

    // invio il timestamp
    send(sck, (void*)m->time_stamp, TIME_LEN+1, 0);

    // invio il messaggio
    basic_send(sck, m->text);

    printf("[+]Message succesfully sent to new contact %s.\n", m->recipient);

    return 1;
}


/*
 * Function:  new_contact_handler
 * --------------------------------
 * gestisce il primo messaggio ad un nuovo contatto. Se questo è online invia il 
 * messaggio al destinatario e il numero di porta del nuovo contatto al mittente,
 * altrimenti si lmita a salvare il messaggio.
 */
void new_contact_handler(int dvcSocket){       

    FILE *fptr;
    char new_user[USER_LEN+1];
    int port;
    char buff[BUFF_SIZE];
    char cur_name[USER_LEN+1];
    uint8_t ack = 1;
    bool exists = false;
    struct session_log* temp = connections;
    struct message* new_msg = (struct message*)malloc(sizeof(struct message));

    if (new_msg==NULL){
        perror("[-]Memory not allocated");
        exit(-1);
    }
    
    printf("[+]new_contact_handler in action..\n");

    //ricevo lo username
    basic_receive(dvcSocket, new_user);

    // controllo se lo user esiste
    fptr = fopen("./users.txt","r");
    printf("[+]Users file correctly opened for reading.\n");

    while ( fgets( buff, BUFF_SIZE, fptr ) != NULL )
    {
        sscanf (buff, "%s %*s %d", cur_name, &port);            

        if ( strcmp(cur_name, new_user) == 0 )
        {  
            exists = true;
            break;
        }
    }

    fclose(fptr);

    if (exists){
        printf("[+]User found.\n");
        send_server_message(dvcSocket, NULL, false);
        port = htons(port);
        send(dvcSocket, (void*)&port, sizeof(uint16_t), 0);
    }
    else{
        printf("[-]User search failed.\n");
        send_server_message(dvcSocket, "User non esistente", true);
        return;
    }

    // ricevo il messaggio
    basic_receive(dvcSocket, new_msg->time_stamp);
    basic_receive(dvcSocket, new_msg->group);
    basic_receive(dvcSocket, new_msg->recipient);
    basic_receive(dvcSocket, new_msg->sender);
    basic_receive(dvcSocket, new_msg->status);
    basic_receive(dvcSocket, new_msg->text);

    // controllo se l'utente è online
    while(temp!=NULL){
        if (strcmp(temp->username, new_user)==0 && temp->socket_fd!=-1){
            break;
        }
        temp = temp->next; 
    }

    if (temp!=NULL){    // se l'utente risulta online
        // provo a inviare il messaggio
        int attempts = 1;
        while (attempts<4){

            printf("[+]Attempt to send message to new contact: n° %d\n", attempts);
            if (send_message_to_peer(temp->socket_fd, new_msg)==1){
                ack = 2;    // ack di avvenuta ricezione
                break;
            }
            attempts++;
        }

        if (attempts<4){    // invio andato a buon fine
            // ricavo la porta
            uint16_t port = temp->port;

            // invio l'ack
            send(dvcSocket, (void*)&ack, sizeof(uint8_t), 0);
            // invio la porta del destinatario
            port = htons(port);
            send(dvcSocket, (void*)&port, sizeof(uint16_t), 0);
            
            return;
        }
        else{
            // l'utente risulta offline
            // aggiorno la sua sessione
            logout(temp->socket_fd, false);
        }
    }

    // utente risulta offline
    //aggiungo alla lista dei messaggi
    if(messages == NULL)
         messages = new_msg;
    else
    {
        struct message* lastNode = messages;
        while(lastNode->next != NULL)
        {
            lastNode = lastNode->next;
        }
        lastNode->next = new_msg;
    }
    free(new_msg);
    free(temp);

    // invio ack di avvenuta memorizzazione
    send(dvcSocket, (void*)&ack, sizeof(uint8_t), 0);

}


/*
* Function: input_handler
* --------------------------------
* gestisce le richeste fatte tramite stdin del server. Legge l'input e avvia il corrispondente handler.
*/
void input_handler(){

    char input[BUFF_SIZE];
	
	fgets(input,sizeof(input),stdin);
    input[strlen(input)-1] = '\0';      // rimuovo l'ultimo char ('\n')
	
    printf("\n");

	if(strcmp(input,"help")==0){
        help_display();
	}
	else if(strcmp(input,"list")==0){
		show_list();
    }
	else if(strcmp(input,"esc")==0){
		terminate_server();
	}
	else{
        show_home();
		printf("\n[-]Invalid command. Try Again.\n");
	}
    prompt_user();
}


/*
* Function: hanging_handler
* ----------------------------
* fornisce una anteprima dei messaggi pendenti destinati al cliente che ha effettuato la richiesta.
* per ogni utente viene mostrato username, il numero di messaggi pendenti, e il timestamp del più recente.
*/
void hanging_handler(int fd){

    FILE *fptr, *fptr1;
    char user[USER_LEN+1];
    int len;
    uint16_t message_len;
    char sen[USER_LEN+1];
    char rec[USER_LEN+1];
    char time[TIME_LEN+1];
    char grp[USER_LEN+2];
    char buff_info[BUFF_SIZE];
    char buff_mess[BUFF_SIZE];
    char buffer[BUFF_SIZE];
    char counter[BUFF_SIZE];
    char info_file[USER_LEN+20] = "./storedMessages/";
    char mess_file[USER_LEN+20] = "./storedMessages/";
    struct preview_user* list = NULL;
    struct preview_user* prev = list;

    // ricevo il nome dello user
    basic_receive(fd, user);

    // ottengo il nome dei file da cercare
    strcat(info_file, user);
    strcat(mess_file, user);
    strcat(info_file, "_info.txt");
    strcat(mess_file, "_messages.txt");

    // apro il file delle info
    printf("[+]Opening file %s\n", info_file);
    if ((fptr = fopen(info_file, "r")) == NULL){
        perror("[-]Error opening file");
        send_server_message(fd, "Nessun messaggio pendente\n", true);
        return;
    }

    printf("[+]File correctly opened.\n");
    send_server_message(fd, NULL, false);

    // apro il file delle chat
    fptr1 = fopen(info_file, "r");

    // leggo il contenuto dei file
    printf("[+]Fetching buffered messages..\n");
    while( fgets(buff_info, BUFF_SIZE, fptr)!=NULL && fgets(buff_mess, BUFF_SIZE, fptr1)!=NULL ) {

        struct preview_user* temp;
        sscanf(buff_info, "%s %s %s %s", sen, rec, time, grp);
        if (strcmp(rec, user)==0){
            // controllo che il mittente abbia già una preview dedicata
            temp = name_checked(list, user);
            if ( temp == NULL ){
                temp = (struct preview_user*)malloc(sizeof(struct preview_user));
                if (temp == NULL){
                    perror("[-]Memory not allocated\n");
                    exit(-1);
                }
                strcpy(temp->user, sen);
                temp->messages_counter = 1;
                strcpy(temp->timestamp, time);
                temp->next = list;
                list = temp;
            }
            else{
                temp->messages_counter++;
                strcpy(temp->timestamp, time);
            }
        }
        free(temp);
    }

    fclose(fptr);
    fclose(fptr1);
    printf("[+]Files closed.\n");

    // preparo la stringa da inviare
    printf("[+]Setting up buffer to send to device..\n");

    while (list){
        
        list = list->next;
        strcat(buffer, prev->user);
        strcat(buffer, "\t");
        sprintf(counter, "%d", prev->messages_counter);
        strcat(buffer, counter);
        strcat(buffer, "\t");
        strcat(buffer, prev->timestamp);
        strcat(buffer, "\n");
        free(prev);
        prev = list;
    }

    // invio la stringa, prima la lunghezza poi il testo
    len = strlen(buffer)+1;
    message_len = htons(len);
    send(fd, (void*)&message_len, sizeof(u_int16_t), 0);
    send(fd, (void*)buffer, message_len, 0);
    printf("[+]Buffer successfully sent to device.\n");
        
}


/*
* Function: pending_messages
* ----------------------------
* invia al client tutti i messaggi pendenti da un determinato user.
* invia un ack di ricezione al mittente dei messaggi
*/
void pending_messages(int fd){

    FILE* fp, *fp1, *fp2;
    int len, size, socket_ack;
    int texts_counter = 0;
    uint16_t lmsg;
    struct ack *ackp;
    struct message *all, *to_send;
    struct message* cur;
    char buffer[BUFF_SIZE];
    char buff_info[BUFF_SIZE];
    char buff_chat[MSG_LEN];
    char sender[USER_LEN+1];
    char recipient[USER_LEN+1];
    char sen[USER_LEN+1];
    char rec[USER_LEN+1];
    char grp[USER_LEN+20];
    char t[TIME_LEN+1];
    char oldest_time[TIME_LEN+1];
    char info_file[USER_LEN+20] = "./storedMessages/";
    char mess_file[USER_LEN+20] = "./storedMessages/";
    char ack[CMD_SIZE+1] = "AK1";

    // ricevi username del destinatario
    recv(fd, (void*)&lmsg, sizeof(uint16_t), 0);          // ricevo la dimesione dello username
    len = ntohs(lmsg);
    recv(fd, (void*)recipient, len, 0);                   // ricevo lo username

    // ricevi username del mittente
    recv(fd, (void*)&lmsg, sizeof(uint16_t), 0);          // ricevo la dimesione dello username
    len = ntohs(lmsg);
    recv(fd, (void*)sender, len, 0);                      // ricevo lo username

    // ottengo i nomi dei file
    strcat(info_file, recipient);
    strcat(mess_file, recipient);
    strcat(info_file, "_info.txt");
    strcat(mess_file, "_messages.txt");

    // apro i file (se esistono) dei messaggi destinati a quel user
    fp = fopen(info_file, "r");

    if (fp == NULL){    // il file non esiste
        printf("[-]File %s doesn't exist.\n", info_file);
        lmsg = htons(texts_counter);
        send(fd, (void*)&lmsg, sizeof(uint16_t), 0);
        return;
    }
    else{   // il file esiste
        fseek (fp, 0, SEEK_END);
        size = ftell(fp);

        if (size==0) {  // ed è vuoto
            printf("[-]File is empty.\n");
            send(fd, (void*)&lmsg, sizeof(uint16_t), 0);
            return;
        }
    }

    fp1 = fopen(mess_file, "r");

    // si gestiscono due liste
    while( fgets(buff_info, BUFF_SIZE, fp)!=NULL && fgets(buff_chat, MSG_LEN, fp1)!=NULL ) {

        struct message* temp = (struct message*)malloc(sizeof(struct message));
        if (temp == NULL){
            perror("[-]Memory not allocated\n");
            exit(-1);
        }
        sscanf(buff_info, "%s %s %s %s", sen, rec, t, grp);
        strcpy(temp->sender, sen);
        strcpy(temp->recipient, rec);
        strcpy(temp->time_stamp, t);
        strcpy(temp->text, buff_chat);
        strcpy(temp->group, grp);
        temp->next = NULL;

        if (strcmp(sen, sender)==0 ){
            // aggiungo alla lista degli elementi da inviare
            // aggiungo in coda

            texts_counter++;
            if(to_send == NULL){
                to_send = temp;
            }
            else{
                struct message* lastNode = to_send;
                while(lastNode->next != NULL)
                    lastNode = lastNode->next;
                lastNode->next = temp;     
            }
        }
        // in goni caso si aggiunge nella lista generale
        if(all == NULL)
            all = temp;
        else
        {
            struct message* lastNode = all;
            while(lastNode->next != NULL)
                lastNode = lastNode->next;
            lastNode->next = temp;        
        }
    }

    fclose(fp);
    fclose(fp1);

    // salvo il timestamp del messaggio meno recente
    strcpy(oldest_time, to_send->time_stamp);

    // invia il numero di messaggi della lista
    lmsg = htons(texts_counter);
    send(fd, (void*)&lmsg, sizeof(uint16_t), 0);
    // se zero ci si ferma
    if (texts_counter==0) {
        return;
    }

    // altrimenti inviare tutti i messaggi

    cur = to_send;
    while(to_send){

        to_send = to_send->next;
        // si bufferizzano i dati del messaggio
        strcat(buffer, cur->time_stamp);
        strcat(buffer, " ");
        strcat(buffer, cur->group);
        strcat(buffer, " ");
        strcat(buffer, cur->sender);

        // invio il buffer contenente info sul messaggio al client
        len = strlen(buffer)+1;
        lmsg = htons(len);
        send(fd, (void*)&lmsg, sizeof(u_int16_t), 0);
        lmsg = ntohs(lmsg);
        send(fd, (void*)buffer, lmsg, 0);
 
        // invio il buffer contenente il messaggio al client
        strcpy(buffer, cur->text);
        
        len = strlen(buffer)+1;
        lmsg = htons(len);
        send(fd, (void*)&lmsg, sizeof(uint16_t), 0);
        lmsg = ntohs(lmsg);
        send(fd, (void*)buffer, lmsg, 0);

        free(cur);
        cur = to_send;
    }

    // invia un singolo ack al mittente (nota che basta specificargli l'utente destinatario, la port, e il timestamp del messaggio meno recente) 
    socket_ack = get_socket(sender);

    ackp = (struct ack*)malloc(sizeof(struct ack));
    if (ackp == NULL){
            perror("[-]Memory not allocated\n");
            exit(-1);
    }
    strcpy(ackp->sender, sender);
    strcpy(ackp->recipient, recipient);
    strcpy(ackp->start_time, oldest_time);

    if (socket_ack == -1){  // se il mittente è offline

        //salvo gli ack e glieli invio non appena si riconnette
        // nota bene in buffered_acks.txt vengono salvati solo gli ack di conferma di lettura
        fp2 = fopen("./buffered_acks.txt", "a");
        fprintf(fp2, "%s %s %s\n", ackp->sender, ackp->recipient, ackp->start_time);

        fclose(fp2);
    }
    else{// il mittente è online
         // invio il comando di ack di ricezione e le info
        send(socket_ack, ack, CMD_SIZE+1, 0);
        // seguo inviando destinatario e timestamp del meno recente
        basic_send(socket_ack, ackp->recipient);
        send(socket_ack, (void*)ackp->start_time, TIME_LEN+1, 0);
    }

    // elimina dalla precedente lista quella generale i nodi che hanno come dest quello considerato
    all = remove_key(sender, all);

    // scrivi "w" nel file associato gli elementi della lista generale ora modificata
    fp = fopen(info_file, "w");
    fp1 = fopen(mess_file, "w");

    cur = all;

    while(all){
        all = all->next;        
        fprintf(fp, "%s %s %s %s\n", cur->sender, cur->recipient, cur->time_stamp, cur->group);
        fprintf(fp1, "%s\n", cur->text);
        free(cur);
        cur = all;
    }

    fclose(fp);
    fclose(fp1);

    printf("[+]File aggiornati correttamente.\n");
}


/*
* Function: send_buffered_acks
* --------------------------------
* invia all'utente tutti gli ack di ricezione salvati mentre questo era offline.
*/
void send_buffered_acks( int sck ){

    FILE* fp;
    uint8_t counter;
    char buffer[BUFF_SIZE];
    struct ack* list = NULL;
    struct ack* to_send = NULL;

    fp = fopen("./buffered_acks.txt", "r+");

    if (fp == NULL){
        counter = 0;
    }
    else {
        // apro il file per cercare acks destinati allo user
        while( fgets(buffer, BUFF_SIZE, fp)!=NULL){

            struct ack* cur_ack =  (struct ack*)malloc(sizeof(struct ack));
            if (cur_ack==NULL){
                perror("[-]");
                exit(-1);
            }
            sscanf(buffer, "%s %s %s", cur_ack->sender, cur_ack->recipient, cur_ack->start_time);

            // se trovo un ack destinato a quell'utente
            if(strcmp(cur_ack->sender, get_name_from_sck(connections, sck))==0){
                
                // aggiungo alla lista degli ack da inviare
                cur_ack->next = to_send;
                to_send = cur_ack;
                counter++;

            }
            else{
                // aggiungo alla lista degli ack da salvare in file
                cur_ack->next = list;
                list = cur_ack;
            }
        }    
    }

    // invio il numero di ack
    send(sck, (void*)&counter, sizeof(uint8_t), 0);

    if (counter==0){
        printf("[-]No acks for %s to send.\n", get_name_from_sck(connections, sck));
        return;
    }
    else{
        // invio tutti gl ack
        struct ack* next;
        while(to_send!=NULL){
            
            basic_send(sck, to_send->recipient);
            send(sck, (void*)to_send->start_time, TIME_LEN+1, 0);
            next = to_send->next;
            free(to_send);
            to_send = next;
        }
    }

    // salvo i restanti ack
    if (list==NULL){
        fclose(fp);
        remove("./buffered_acks.txt");
    }
    else{
        rewind(fp);
        struct ack* next;
        while(list!=NULL){
            fprintf(fp, "%s %s %s\n", list->sender, list->recipient, list->start_time);
            next = list->next;
            free(list);
            list = next;
        }
        fclose(fp);
    }

    return;

}


/*
* Function: send_port
* ------------------------
* invia a sck, il numero di porta del user richiesto.
*/
void send_port(int sck){

    FILE* fp;
    char user[USER_LEN+1];
    char cur_us[USER_LEN+1];
    char buff[BUFF_SIZE];
    int cur_port;
    uint16_t port;

    // ricevo il nome
    basic_receive(sck, user);

    fp = fopen("./users.txt", "r");
    while( fgets(buff, BUFF_SIZE, fp)!=NULL ){

        sscanf(buff, "%s %*s %d", cur_us, &cur_port);
        if (strcmp(cur_us, user)==0){
            port = cur_port;
            break;
        }
    }

    // invio la porta
    port = htons(port);
    send(sck, (void*)&port, sizeof(uint16_t), 0);

    printf("[+]Port sent.\n");

}


/*
* Function: client_handler
* -----------------------
* gestisce le interazioni client server. per ogni richiesta ricevuta avvia l'apposito gestore
*/
void client_handler(char* cmd, int s_fd){       

    if(strcmp(cmd,"LGO")==0){
        logout(s_fd, true);
    }
    else if(strcmp(cmd, "NCH")==0){
        new_contact_handler(s_fd);
    }
    else if(strcmp(cmd, "HNG")==0){
        hanging_handler(s_fd);
    }
    else if(strcmp(cmd, "SHW")==0){
        pending_messages(s_fd);
    }
    else if(strcmp(cmd, "SOM")==0){    
        offline_message_handler(s_fd);
    }
    else if(strcmp(cmd, "RCA")==0){
        send_buffered_acks(s_fd);
    }
    else if(strcmp(cmd, "RCP")==0){
        send_port(s_fd);
    }
    else{
        printf("[-]Error in server command reception\n");
    }
    
}


int main(int argc, char* argcv[])
{
    struct sockaddr_in dv_addr;     // indirizzo del device
    struct sockaddr_in sv_addr;     // indirizzo del server
    int listener;                   // descrittore del listening socket
    int newfd;                      // descrittore di un nuovo socket
    socklen_t addrlen;

    char buff[BUFF_SIZE];           // buffer per i dati in ricezione

    int i;
    int sv_port;
    int ret_b, ret_l, ret_r;        // valori di ritorno delle funzioni


    FD_ZERO(&master);               // azzero i set
    FD_ZERO(&read_fds);

    if (argc > 1) {
        sv_port = atoi(argcv[1]);
    }
    else {
        sv_port = DEFAULT_SV_PORT;
    }

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("[-]Error in socket");
        exit(1);
    }
    printf("\n[+]Server socket created successfully.\n");

    sv_addr.sin_family = AF_INET;
    sv_addr.sin_addr.s_addr = INADDR_ANY;
    sv_addr.sin_port = htons(sv_port);

    ret_l = setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    if (ret_l == -1){
        perror("[-]Error in setsockopt");
        exit(-1);
    }

    ret_b = bind(listener, (struct sockaddr*)& sv_addr, sizeof(sv_addr));
    if(ret_b < 0){
        perror("[-]Error in bind");
        exit(1);
    }
    printf("[+]Binding successfull.\n");

    ret_l = listen(listener, 10);
    if(ret_l < 0){
        perror("[-]Error in listening");
        exit(1);
    }
    printf("[+]Listening..\n");

    // aggiungo stdin e listener al master set
    FD_SET(0,&master);
    FD_SET(listener, &master);

    fdmax = listener; 

    setup_lists();
    sleep(2);
    show_home();
    prompt_user();

    for(;;) {
        read_fds = master; 
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("[-]Error in select");
            exit(4);
        }
        printf("\n[+]Select worked.\n");

        // se stdin diviene attivo
		if(FD_ISSET(0,&read_fds)){

			input_handler();
		}

        /// se TCP diviene attivo
		else if(FD_ISSET(listener,&read_fds)){

			// chiamo accept per ottenere una nuova connessione
			addrlen = sizeof(dv_addr);
            if((newfd = accept(listener,(struct sockaddr*)&dv_addr,&addrlen))<0){
				perror("[-]Error in accept");
			}
			else{
                printf("[+]New connection accepted.\n");

                // gestisco qui la signup/login
                recv(newfd, (void*)buff, CMD_SIZE+1, 0);
                if (strcmp(buff, "SGU")==0){
                    signup(newfd);
                }
                else if(strcmp(buff, "LGI")==0){
                    login(newfd);
                }
                else{
                    printf("[-]Error in cmd received from device: %s\n", buff);
                }
		    }
            prompt_user();
        }
    
        for(i = 1; i <= fdmax; i++) {

            if (FD_ISSET(i, &read_fds) && i!=listener) { 

                // nel caso di errore nella ricezione dei dati
                if ((ret_r = recv(newfd, (void*)buff, CMD_SIZE+1, 0)) <= 0) {

                    if (ret_r == 0) {
                            // chiusura del device, chiusura non regolare si traduce in un logout
                            logout(i, false);
                            printf("[-]Selectserver: socket %d hung up\n", i);

                    } else {
                            // errore nel recv
                            perror("[-]Error in recv");
                            // rimuovere la connessione dalla lista delle connessioni attive !!!
                            //logout(i, false);
                    } 
                } 
                else {
                        // ricevo il tipo di comando e gestisco tramite handler
                        printf("[+]Client request received: %s\n", buff);
                        client_handler(buff, i);
                }
                prompt_user();                    
            }
        } 
    }
    
    return 0;
}
