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
#include "utility.c"

/* 
Ho un file txt dove salvo ogni volta che termino il server tutte le connessioni ancora aperte.
Ogni volta che avvio il server inizializzo la lista con le sole connessioni ancora aperte.
Per soddisfare il requisito sul salvataggio delle disconessioni offline il file txt conterrà sia 
le connessioni aperte che quelle chiuse. (in questo contesto intendo connessione<=>login/out)

Si implementano 2 file per le connections, una conterrà le sessioni chiuse(con logout) e l'altra 
sessioni ancora aperte()

ho un altro file con solo le info degli utenti registrati
*/ 

#define DEFAULT_SV_PORT 4242   // porta su cui ascolta il server
#define CMD_SIZE 3
#define USER_LEN 50
#define MSG_BUFF 1024
#define DIM_BUF 1024
#define TIME_LEN 20

struct user_device* users;
struct connection_log* cur_con;         // contiene le informazioni riguardo al socket correntemente gestito
struct connection_log* connections;
struct message* messages;

void show_home(){

    system("clear");

    printf("***********************************");
    printf(" SERVER STARTED ");
    printf("***********************************\n");
    printf("Digita un comando:\n\n");
    printf("1)  help --> mostra i dettagli dei comandi\n");
    printf("2)  list --> mostra un elenco degli utenti connessi\n");
    printf("3)  esc  --> chiude il server\n\n");

}


void setup_lists(){

    FILE *fptr, *fptr1;
    char buffer[90];
    char buff_info[MSG_BUFF];
    char buff_chat[MSG_BUFF];
    char user[USER_LEN+1];
    char time_login[TIME_LEN+1];
    char time_logout[TIME_LEN+1];
    char sen[USER_LEN+1];
    char rec[USER_LEN+1];
    char time[TIME_LEN+1];
    char grp[USER_LEN+20];     
    char st[3];     
    int port;

    // prima inizializzo la lista delle connessioni
    if ((fptr = fopen("./active_logs.txt","r")) == NULL){
        perror("[-]Error opening file");
            return;
    }
    printf("[+]Log file correctly opened.\n");

    while(fgets(buffer, 90, fptr) != NULL) {
        struct connection_log* temp;
        sscanf (buffer, "%s %d %s %s",user,&port,time_login,time_logout);        
                    
        temp = (struct connection_log*)malloc(sizeof(struct connection_log));
        if (temp == NULL){
            perror("[-]Memory not allocated\n");
            return;
        }
        else
        {
            strcpy(temp->username,user);
            temp->port = port;
            temp->socket_fd = -1;
            strcpy(temp->timestamp_login, time_login);
            strcpy(temp->timestamp_logout, time_logout);
            temp->next = NULL;
            if(connections == NULL)
                connections = temp;
            else
            {
                struct connection_log* lastNode = connections;
                while(lastNode->next != NULL)
                    lastNode = lastNode->next;
                lastNode->next = temp;        
            }
        }           
    }
    printf("[+]Connections list correctly implemented.\n");
    fclose(fptr);  

    // ora inizializzo la lista dei messaggi
    if ( (fptr = fopen("./chat_info.txt","r"))!=NULL && (fptr1 = fopen("./chats.txt","r")) != NULL)  {
        printf("[+]Chat files correctly opened.\n");
    }
    else{
        printf("[-]Error! opening file\n");
        return;
    }

    while( fgets(buff_info, 200, fptr)!=NULL && fgets(buff_chat, MSG_BUFF, fptr1)!=NULL ) {

        struct message* temp = malloc(sizeof(struct message));
        sscanf(buff_info, "%s %s %s %s %s", sen, rec, time, grp, st);
        strcpy(temp->sender, sen);
        strcpy(temp->recipient, rec);
        strcpy(temp->time_stamp, time);
        strcpy(temp->text, buff_chat);
        strcpy(temp->group, grp);
        strcpy(temp->status, st);
        temp->next = NULL;
        if(messages == NULL)
            messages = temp;
        else
        {
            struct message* lastNode = messages;
            while(lastNode->next != NULL)
                lastNode = lastNode->next;
            lastNode->next = temp;        
        }

    }
    printf("[+]Messages list correctly implemented.\n");
    fclose(fptr);  /* close the file */
    fclose(fptr1);
    return;

}                                                             


int get_socket(char* user){

    struct connection_log* temp = connections;

    while (temp){

        if (strcmp(temp->username, user)==0 && strcmp(temp->timestamp_logout, "")==0 && temp->socket_fd!=-1){
            break;
        }
        temp = temp->next;
    }

    if (temp==NULL) return -1;
    else return temp->socket_fd;
}


void logout(int socket, bool now, bool old){  
// now: indica una richiesta di logout da parte del device oppure una disconessione irregolare fatta quando il server è ancora attivo
// now= false indica che viene richiesto di registrare un logout relativo ad una precedente sessione chiusa quando il server era offline
    char user[USER_LEN];
    char buff[TIME_LEN];
    struct connection_log* temp = connections;

    printf("[+]Logout working..\n");

    if (now){
        time_t now = time(NULL);
        strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
    }
    else{
        // ricevo il logout
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

    // cerco un'altra connessione nella lista che abbia questo username e socket_fd
    // uguale a -1. Aggiorno in questo nodo il timestamp di logout
    temp = connections;
    printf("[+]Looking for session log to update..\n");
    while (temp)
    {
        if((strcmp(temp->username, user)==0 && temp->socket_fd==-1) ||
            ( strcmp(temp->username, user)==0 && strcmp(temp->timestamp_logout, "")==0 )
        ){
            strcpy(temp->timestamp_logout, buff);
            printf("[+]Session updated.\n");
            break;
        }
        temp = temp->next;
    }
    
    // soltanto se now è true procedo a chiudere il socket.
    if (old == false){

        close(socket);
        socket = -1;
        printf("[+]Socket closed.\n");
    }    
    printf("[+]Logout successfully completed.\n");

}


void show_list(){
    /*
    Mostra l’elenco degli utenti connessi alla rete, indicando username, timestamp di connessione e numero di
    porta nel formato “username*timestamp*porta”
    */
    struct connection_log* temp = connections;
    while(temp){
        if ( temp->socket_fd!=-1 ){
            printf("%s*%s*%d\n", temp->username, temp->timestamp_login, temp->port);
        }
        temp = temp->next;
    }

    sleep(6);
    show_home();
}


void terminate_server(){

    FILE *fptr;
    FILE *fptr1;
    struct connection_log* temp = connections;
    struct message* node = messages;

    // salvo i log ancora attivi nel file logs.txt
    if ( (fptr = fopen("./active_logs.txt","w"))!=NULL && (fptr1 = fopen("./logs_ar.txt", "a"))!=NULL ){
        printf("[+]Log files correctly opened.\n");
    }
    else{
        perror("[-]Error opening files");
        return;
    }                                                                    
   
    while(connections){

        connections = connections->next;
        if ( strcmp(temp->timestamp_logout, "\0")==0 ){

            //salvo le sessioni ancora attive
            fprintf(fptr, "%s %d %s %s\n", 
                temp->username, temp->port, temp->timestamp_login, temp->timestamp_logout);
        }
        else{
            //salvo le sessioni chiuse
            fprintf(fptr1, "%s %d %s %s\n", 
                temp->username, temp->port, temp->timestamp_login, temp->timestamp_logout);
            // chiudo le connessioni aperte
            close(temp->socket_fd);
        }

        free(temp);
        temp = connections;
    }
    printf("[+]Log files correctly updated.\n");

    fclose(fptr);  
    fclose(fptr1);

    // salvare i messaggi in file     e eliminare la lista e gestire memoria       !!!!!!!!!!!!!!!!!!!!!!
    if ( (fptr = fopen("./chat_info.txt","w"))!=NULL && (fptr1 = fopen("./chats.txt","w")) != NULL)  {
        printf("[+]Chat files correctly opened.\n");
    }
    else{
        perror("[-]Error opening files");
        return;
    }

    while(messages){
        messages = messages->next;
        fprintf(fptr, "%s %s %s %s %s\n", 
                node->sender, node->recipient, node->time_stamp, node->group, node->status);
        fprintf(fptr1,"%s\n", node->text);
        free(node);
        node = messages;
    }
    printf("[+]Chat files correctly updated.\n");  

    fclose(fptr);  
    fclose(fptr1);
    printf("[+]Server terminated.\n");
    exit(1);
}

void is_online(){}

void sendMessage(int socket, char* message, bool error){
    char c[2];
    uint16_t message_len;

    if (error)
        strcpy(c,"E");
    else strcpy(c,"S");
    send(socket, (void*)c, 2, 0);

    if (message){
        //invio prima la dimensione
        message_len = strlen(message);
        message_len = htons(message_len);
        send(socket, (void*)&message_len, sizeof(u_int16_t), 0);
        //invio ora il messaggio
        send(socket, (void*)message, message_len, 0);
    }
}


void login(int dvcSocket)        //gestisce la login
{
    FILE *fptr;
    char buff[DIM_BUF];
    char cur_name[USER_LEN+1];
    char cur_pw[USER_LEN+1];
    char psw[USER_LEN+1];
    char user[USER_LEN+1];
    char t_buff[TIME_LEN+1];
    uint16_t message_len;
    uint16_t port;
    time_t now = time(NULL);
    bool found = false;
    struct connection_log* new_node = malloc(sizeof( struct connection_log));

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


    // confronto username e psw con users.txt
    fptr = fopen("./users.txt","r");
    printf("[+]Users file correctly opened for reading.\n");
    while ( fgets( buff, sizeof buff, fptr ) != NULL )
    {
        sscanf (buff, "%s %s", cur_name, cur_pw);            

        if ( strcmp(cur_name, user) == 0 && strcmp(cur_pw, psw) == 0 )
        {   printf("[+]Found a match for received credentials.\n");
            found = true;
            break;
        }
    }

    if (!found){
            sendMessage(dvcSocket, "Login fallito!", true);
            return;
    }

    // aggiungo in coda alla lista delle connessioni gestite in questa sessione
    new_node->next = NULL;
    new_node->port = port;
    new_node->socket_fd = dvcSocket;
    strftime(t_buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
    strcpy(new_node->timestamp_login, t_buff);
    strcpy(new_node->timestamp_logout, "");
    strcpy(new_node->username, user);

    printf("[+]Login:\n");
    printf("\t%s", new_node->username);
    printf(" %s", new_node->timestamp_login);
    printf(" %s\n", new_node->timestamp_logout);

    if(connections == NULL)
         connections = new_node;

    else
    {
        struct connection_log* lastNode = connections;
    
        while(lastNode->next != NULL)
        {
            lastNode = lastNode->next;
        }
        lastNode->next = new_node;
      
    }

    printf("[+]Login saved.\n");

    //invio un messaggio di conferma all'utente
    sendMessage(dvcSocket, NULL, false);
   
}


void signup(int dvcSocket){

    FILE *fptr;
    FILE *fpta;
    char buff[100];
    char cur_name[50];
    char new_psw[50];
    char new_user[50];
    uint16_t message_len;

    printf("[+]Signup handler in action.\n");

    recv(dvcSocket, (void*)&message_len, sizeof(uint16_t), 0);          // ricevo la dimesione dello username
    message_len = ntohs(message_len);
    recv(dvcSocket, (void*)new_user, message_len, 0);                   // ricevo lo username

    recv(dvcSocket, (void*)&message_len, sizeof(uint16_t), 0);          // ricevo dimensione psw
    message_len = ntohs(message_len);
    recv(dvcSocket, (void*)new_psw, message_len, 0);                    // ricevo password

    printf("[+]Received credentials: user: %s, crypted psw: %s\n", new_user, new_psw);

    // apro il file users.txt per controllare l'eventuale presenza di un omonimo, se presente scarto il dato in entrata
    fptr = fopen("./users.txt","a");                                    // se non esiste viene creato
    if ( (fptr = fopen("./users.txt","r"))==NULL ){
        perror("[-]Error opening users file\n");
        return;
    }
    else
        printf("[+]Users file correctly opened for reading.\n");

    while ( fgets( buff, DIM_BUF, fptr ) != NULL )
    {
        sscanf (buff, "%s %*s", cur_name);            

        if ( strcmp(cur_name, new_user) == 0 )
        {   printf("[-]Username already used.\n");
            //invio segnalazione al client
            sendMessage(dvcSocket, "Username già presente nel database. Scegliere un altro username!", true);
            return;
        }
    }  
    fclose(fptr);

    // apro il file users.txt per aggiungere il nuovo utente
    fpta = fopen("./users.txt","a");
    printf("[+]Users file correctly opened for appending.\n");
    fprintf(fpta, "%s %s\n", new_user, new_psw);
    fflush(fpta);
    fclose(fpta);

    printf("[+]New user registered.\n");

    sendMessage(dvcSocket, NULL, false);

} 


void offline_message_handler(const char* rec, int client){
    
    uint16_t message_len;
    uint16_t ack;
    char t_buff[20];
    char sender[50];
    char m_buff[MSG_BUFF];
    struct message* new_node = malloc(sizeof(struct message));
    struct connection_log* temp = connections;
    time_t now = time(NULL);
    strftime(t_buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));

    //ricevo la lungheza del messaggio
    printf("[+]Message incoming..");

    recv(client, (void*)&message_len, sizeof(uint16_t), 0);         
    message_len = ntohs(message_len);
    // ed il messaggio
    recv(client, (void*)m_buff, message_len, 0);
    printf("[+]Messaggio received.");

    // search for sender username
    while(temp){
        if (temp->socket_fd == client && strcmp(temp->timestamp_logout, "\0")==0){
            strcpy(sender, temp->username);
            break;
        }
        temp = temp->next;
    }

    strcpy(new_node->recipient, rec);
    strcpy(new_node->sender, sender);
    strcpy(new_node->time_stamp, t_buff);
    strcpy(new_node->status, "*");
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
    printf("[+]Messagge saved.");

    //invio al mittente l'ACK di avvenuta memorizzazione
    ack = 1;
    ack = htons(ack);
    send(client, (void*)&ack, sizeof(uint16_t), 0);

    printf("[+]Saved ack sent.");
    
}


void online_contact_handler(struct connection_log* rec, int client){

    //da trattare ancora il caso di nuovo contatto aggiunto al gruppo
    char ts[TIME_LEN];
    char m_buff[MSG_BUFF];
    char cmd[CMD_SIZE+1];
    char sen[USER_LEN];
    uint16_t mess_len;
    uint16_t ack;
    struct connection_log* temp = connections;

    printf("[+]Online_contact handler working..\n");

    // ricevo la lungheza del messaggio
    printf("[+]Message incoming..");

    recv(client, (void*)&mess_len, sizeof(uint16_t), 0);         
    mess_len = ntohs(mess_len);
    // ed il messaggio
    recv(client, (void*)m_buff, mess_len, 0);
    printf("[+]Messaggio received.");

    // ricevo il timestamp di invio
    recv(client, (void*)ts, TIME_LEN, 0);

    //invio il comando
    strcpy(cmd, "PNC");
    send(rec->socket_fd, (void*)cmd, CMD_SIZE+1, 0);

    //cerco il mittente
    while (temp){
       if( temp->socket_fd == client && strcmp(temp->timestamp_logout, "\0")==0 ){
            strcpy(sen, temp->username);
            break;
       }
       temp = temp->next;
    }

    // invio il mittente
    send(rec->socket_fd, (void*)sen, strlen(sen), 0);
    printf("[+]Sender sent.\n");

    // invio il timestamp
    send(rec->socket_fd, (void*)ts, TIME_LEN, 0);
    printf("[+]Timestamp sent.\n");

    // invio la lunghezza del messaggio
    mess_len = htons(mess_len);
    send(rec->socket_fd, (void*)&mess_len, sizeof(uint16_t), 0);
    // invio il messaggio
    send(rec->socket_fd, (void*)m_buff, strlen(m_buff), 0);
    printf("[+]Message sent.\n");

   //invio notifica di avvenuta ricezione (ack=2)
    ack = 2;
    ack = htons(ack);
    send(client, (void*)&ack, sizeof(uint16_t), 0);
}


void newcontact_handler(int dvcSocket){       // gestisco un nuovo contatto ed il primo messaggio offline

    FILE *fptr;
    char new_user[USER_LEN];
    uint16_t message_len;
    char buff[DIM_BUF];
    char cur_name[USER_LEN];
    struct connection_log* temp = connections;
    bool exists = false;
    bool online = false;
    
    printf("[+]newcontact_handler in action..\n");

    //ricevo lo username
    recv(dvcSocket, (void*)&message_len, sizeof(uint16_t), 0);          // ricevo la dimesione dello username
    message_len = ntohs(message_len);
    recv(dvcSocket, (void*)new_user, message_len, 0);                   // ricevo lo username

    // controllo se lo user esiste
    fptr = fopen("./users.txt","r");
    printf("[+]Users file correctly opened for reading.");

    while ( fgets( buff, DIM_BUF, fptr ) != NULL )
    {
        sscanf (buff, "%s %*s", cur_name);            

        if ( strcmp(cur_name, new_user) == 0 )
        {   
            exists = true;
            break;
        }
    }

    if (!exists){
        sendMessage(dvcSocket, "User non trovato", true);
        printf("[-]User search failed.");
        return;
    }
    printf("[+]User found.");
    fclose(fptr);

    // controllo se lo user è online
    while (temp){
       if( strcmp(temp->username, new_user)==0 && strcmp(temp->timestamp_logout, "\0")==0 
                    && temp->socket_fd!=-1){
            online = true;
            break;
       }
       temp = temp->next;
    }

    if (online){
        printf("[+]User online.\n");
        sendMessage(dvcSocket, "USER_ONLINE", false);
        online_contact_handler(temp, dvcSocket);
    }
    else{
        printf("[+]User offline.\n");
        sendMessage(dvcSocket, "USER_OFFLINE", false);
        offline_message_handler(new_user, dvcSocket);
    }

}


void sv_command_handler(){

    char input[MSG_BUFF];
	
	fgets(input,sizeof(input),stdin);
	input[strcspn(input, "\n")] = '\0';
	
	while(1){
		if(strcmp(input,"help")==0){
            system("clear");
			printf("\n-> help: mostra questa breve descrizione dei comandi;\n");
			printf("-> list: mostra l’elenco degli utenti connessi alla rete, indicando username, timestamp di connessione e numero di porta;\n");
			printf("-> esc:  termina il server. Ciò non impedisce alle chat in corso di proseguire.\n\n");
			break;
		}
		else if(strcmp(input,"list")==0){
			show_list();
			break;
        }
		else if(strcmp(input,"esc")==0){
			terminate_server();
            break;
		}
		else{
			printf("[-]Invalid command. Try Again.\n");
            sleep(2);
            show_home();
			break;
		}
		
	}//end of while
}


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

void hanging_handler(int fd){

    FILE *fptr, *fptr1;
    char user[USER_LEN+1];
    int len, grp, mlen;
    uint16_t message_len;
    char sen[USER_LEN+1];
    char rec[USER_LEN+1];
    char time[TIME_LEN+1];
    char buff_info[DIM_BUF];
    char buff_mess[DIM_BUF];
    char buffer[DIM_BUF];
    char counter[DIM_BUF];
    char info_file[USER_LEN+20] = "./storedMessages/";
    char mess_file[USER_LEN+20] = "./storedMessages/";
    struct preview_user* list = NULL;
    struct preview_user* prev = list;

    /*Permette all’utente di ricevere la lista degli utenti che gli hanno inviato messaggi mentre era offline. 
    Tali messaggi sono detti messaggi pendenti. Per ogni utente, il comando mostra username, il numero di messaggi
    pendenti in ingresso, e il timestamp del più recente.*/
    // ricevo il nome dello user
    recv(fd, (void*)&message_len, sizeof(uint16_t), 0);          // ricevo la dimesione dello username
    message_len = ntohs(message_len);
    recv(fd, (void*)user, message_len, 0);                       // ricevo lo username

    // ottengo il nome del file da cercare
    strcat(info_file, user);
    strcat(mess_file, user);
    strcat(info_file, "_info.txt");
    strcat(mess_file, "_messages.txt");

    // apro il file delle info
    printf("[+]Opening file %s", info_file);
    if ((fptr = fopen(info_file, "r")) == NULL){
        perror("[-]Error opening file");
        sendMessage(fd, "Nessun messaggio pendente\n", true);
        return;
    }

    printf("[+]File correctly opened.\n");
    sendMessage(fd, NULL, false);

    // apro il file delle chat
    fptr1 = fopen(info_file, "r");

    // leggo il contenuto dei file
    printf("[+]Fetching buffered messages..\n");
    while( fgets(buff_info, DIM_BUF, fptr)!=NULL && fgets(buff_mess, DIM_BUF, fptr1)!=NULL ) {

        struct preview_user* temp;
        sscanf(buff_info, "%s %s %s %d %d", sen, rec, time, &grp, &mlen);
        if (strcmp(rec, user)==0){
            // controllo che il mittente abbia già una preview dedicata
            temp = name_checked(list, user);
            if ( temp == NULL ){
                temp = (struct preview_user*)malloc(sizeof(struct preview_user));
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

void pending_messages(int fd){

    FILE* fp, *fp1, *fp2;
    int len, size, socket_ack;
    int texts_counter = 0;
    uint16_t lmsg;
    struct ack *ackp;
    struct message *all, *to_send;
    struct message* cur;
    char buffer[DIM_BUF];
    char buff_info[DIM_BUF];
    char buff_chat[MSG_BUFF+1];
    char sender[USER_LEN+1];
    char recipient[USER_LEN+1];
    char sen[USER_LEN+1];
    char rec[USER_LEN+1];
    char grp[USER_LEN+20];
    char t[TIME_LEN+1];
    char now_time[TIME_LEN+1];
    char oldest_time[TIME_LEN+1];
    char info_file[USER_LEN+20] = "./storedMessages/";
    char mess_file[USER_LEN+20] = "./storedMessages/";
    char ack[CMD_SIZE+1] = "ACK1";
    time_t now = time(NULL);

    // ricevi username del destinatario
    recv(fd, (void*)&lmsg, sizeof(uint16_t), 0);          // ricevo la dimesione dello username
    len = ntohs(lmsg);
    recv(fd, (void*)recipient, len, 0);                       // ricevo lo username

    // ricevi username del mittente
    recv(fd, (void*)&lmsg, sizeof(uint16_t), 0);          // ricevo la dimesione dello username
    len = ntohs(lmsg);
    recv(fd, (void*)sender, len, 0);                       // ricevo lo username

    // ottengo i nomi dei file
    strcat(info_file, recipient);
    strcat(mess_file, recipient);
    strcat(info_file, "_info.txt");
    strcat(mess_file, "_messages.txt");

    // apri i file (se esistono) dimessaggi destinati a quel user
    fp = fopen(info_file, "r");

    if (fp == NULL){
        printf("[-]File doesn't exist.\n");
        lmsg = htons(texts_counter);
        send(fd, (void*)&lmsg, sizeof(uint16_t), 0);
        return;
    }
    if (fp != NULL) {   
        fseek (fp, 0, SEEK_END);
        size = ftell(fp);

        if (size==0) {
            printf("[-]File is empty.\n");
            lmsg = htons(texts_counter);
            send(fd, (void*)&lmsg, sizeof(uint16_t), 0);
            return;
        }
    }

    fp1 = fopen(mess_file, "r");

    // si gestiscono due liste
    while( fgets(buff_info, DIM_BUF, fp)!=NULL && fgets(buff_chat, MSG_BUFF, fp1)!=NULL ) {

        struct message* temp = malloc(sizeof(struct message));
        sscanf(buff_info, "%s %s %s %s", sen, rec, t, grp);
        strcpy(temp->sender, sen);
        strcpy(temp->recipient, rec);
        strcpy(temp->time_stamp, t);
        strcpy(temp->text, buff_chat);
        strcpy(temp->group, grp);
        temp->next = NULL;

        if (strcmp(sen, sender)==0 ){
            //aggiungo alla lista degli elementi da inviare
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
    if (texts_counter==0) return;
    // altrimenti inviare tutti i messaggi

    cur = to_send;
    while(to_send){

        to_send = to_send->next;
        // fare quello che devi con curr
        strcat(buffer, cur->time_stamp);
        strcat(buffer, " ");
        strcat(buffer, cur->group);
        strcat(buffer, " ");
        strcat(buffer, cur->sender);
        // send to recipient
        len = strlen(buffer)+1;
        lmsg = htons(len);
        send(fd, (void*)&lmsg, sizeof(u_int16_t), 0);
        send(fd, (void*)buffer, lmsg, 0);
 
        strcat(buffer, cur->text);

        // send to recipient
        len = strlen(buffer)+1;
        lmsg = htons(len);
        send(fd, (void*)&lmsg, sizeof(u_int16_t), 0);
        send(fd, (void*)buffer, lmsg, 0);

        free(cur);
        cur = to_send;
    }

    // invia un singolo ack al mittente (nota che basta specificargli l'utente destinatario, la port, e il timestamp del messaggio meno recente)
    socket_ack = get_socket(sender);

    ackp = (struct ack*)malloc(sizeof(struct ack));
    strcpy(ackp->sender, sender);
    strcpy(ackp->recipient, recipient);
    strcpy(ackp->start_time, oldest_time);
    strftime(now_time, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
    if (socket_ack == -1){  // se il mittente è offline
        //salvo gli ack e glieli invio non appena si riconnette
        // nota bene in ack_register vengono salvati solo gli ack di conferma di lettura
        fp2 = fopen("./ack_register.txt", "a");
        fprintf(fp2, "%s %s %s %s\n", ackp->sender, ackp->recipient, ackp->start_time,
                ackp->end_time);

        fclose(fp2);
    }
    else{   // il mittente è online
        //invio il comando di ack di ricezione e le info
        send(socket_ack, ack, CMD_SIZE+1, 0);
        // seguo inviando destinatario e timestamp del meno recente
        len = strlen(recipient)+1;
        lmsg = htons(len);
        send(socket_ack, (void*)&lmsg, sizeof(u_int16_t), 0);
        send(socket_ack, (void*)recipient, lmsg, 0);
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
}

void save_offline_messages(){

}

void send_users_online(int fd){

    char buffer[DIM_BUF];
    uint16_t lmsg;
    int counter = 0;
    struct connection_log* temp = connections;

    while (temp){
        if (temp->socket_fd!=-1 && temp->socket_fd!=fd && strcmp(temp->timestamp_logout, "")==0 ){
            counter++;
            strcat(buffer, temp->username);
            strcat(buffer, "\n");
        }
        temp = temp->next;
    }

    // controllare lunghezza del file
    if (counter==0){
        strcpy(buffer, "Nessun utente online.\n");
    }
    // send buffer to user
    lmsg = strlen(buffer)+1;
    lmsg = htons(lmsg);
    send(fd, (void*)&lmsg, sizeof(uint16_t), 0);
    lmsg = ntohs(lmsg);
    send(fd, (void*)buffer, lmsg, 0);
    
}

void send_peer_connection(){

}


void client_handler(char* cmd, int s_fd){       // gestisce le interazioni client a server

    if (strcmp(cmd, "SGU")==0 ){
        signup(s_fd);
    }
    else if (strcmp(cmd, "LGI")==0){
        login(s_fd);
    }
    else if(strcmp(cmd,"LGO")==0){
        logout(s_fd, true, false);
    }
    else if(strcmp(cmd, "NWC")==0){
        newcontact_handler(s_fd);
    }
    else if(strcmp(cmd, "HNG")==0){
        hanging_handler(s_fd);
    }
    else if(strcmp(cmd, "SHW")==0){
        pending_messages(s_fd);
    }
    else if(strcmp(cmd, "LOT")==0){
        logout(s_fd, false, true);
    }
    else if(strcmp(cmd, "SOM")==0){
        save_offline_messages();
    }
    else if(strcmp(cmd, "AOL")==0){
        send_users_online(s_fd);
    }
    else if(strcmp(cmd, "GPC")==0){
        send_peer_connection(s_fd);
    }
    else{
        printf("[-]Error in server command reception");
    }
    
}

int main(int argc, char* argcv[])
{
    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number

    struct sockaddr_in dv_addr; // device address
    struct sockaddr_in sv_addr; // server address
    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    
    socklen_t addrlen;

    char buff[1024];    /// buffer for incoming data

    int i;
    int sv_port;
    int ret_b, ret_l, ret_r;

    signal(SIGPIPE, SIG_IGN);

    FD_ZERO(&master);    // clear the master and temp sets
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
    printf("[+]Server socket created successfully.\n");

    sv_addr.sin_family = AF_INET;
    sv_addr.sin_addr.s_addr = INADDR_ANY;
    sv_addr.sin_port = htons(sv_port);

    ret_b = bind(listener, (struct sockaddr*)& sv_addr, sizeof(sv_addr));
    if(ret_b < 0) {
        perror("[-]Error in bind");
        exit(1);
    }
    printf("[+]Binding successfull.\n");

    ret_l = listen(listener, 10);
    if(ret_l < 0){
        perror("[-]Error in listening");
        exit(1);
    }
    printf("[+]Listening....\n");

    // add stdin and listener to the master set
    FD_SET(0,&master);
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    setup_lists();
    sleep(3);
    show_home();

    // main loop
    for(;;) {
        read_fds = master; // copy it
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("[-]Error in select");
            exit(4);
        }
        printf("[+]Select worked.\n");

        /*if stdin becomes active*/
		if(FD_ISSET(0,&read_fds)){

			sv_command_handler();
		}

        /*if TCP becomes active*/
		if(FD_ISSET(listener,&read_fds)){
			
			/*calling accept to get new connections*/
			addrlen = sizeof(dv_addr);
            if((newfd = accept(listener,(struct sockaddr*)&dv_addr,&addrlen))<0){
				perror("[-]Error in accept");
			}
			else{
                printf("[+]New connection accepted.\n");
                /*set new fd to master set*/
                FD_SET(newfd,&master);
                
                /*update maxfd*/
                if(newfd>fdmax){
                    fdmax = newfd;
                }
            }
			
		}
    
        for(i = 1; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds) && i!=listener) { // we got one!!
            //perché sia arrivato a questo punto deve prima essersi connesso cn in
            // ricevo il tipo di comando e gestisco tramite handler
            // Ricevo i dati
                    // handle data from a client
                    if ((ret_r = recv(newfd, (void*)buff, CMD_SIZE+1, 0)) <= 0) {

                        if (ret_r == 0) {
                            // chiusura del device, chiusura non regolare si traduce in un logout
                            logout(i, true, false);
                            printf("[-]Selectserver: socket %d hung up\n", i);

                        } else {
                            // errore nel recv
                            perror("[-]Error in recv");
                        }

                        // rimuovere la connessione dalla lista delle connessioni attive !!!
                        close(i); // bye! chiudo il socket connesso
                        FD_CLR(i, &master); // remove from master set
                        
                    } 
                    else {
                        // we got some data from a client
                        printf("[+]Client request received: %s\n", buff);
                        client_handler(buff, i);
                        // uso i dati
                    }
                    
            } // END got new incoming connection
        } // END looping through file descriptors
    }
    
    return 0;
}