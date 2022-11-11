 /*
 * utility_s.c:     file contenente definizioni macro, strutture dati e funzioni di routine utilizzate dal server.
 *
 * Samayandys Sisa Ruiz Muenala, 10 novembre 2022
 * 
 */

/*--------------------------
*         MACRO
*---------------------------*/

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
    uint16_t port;      
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
    char group[USER_LEN+2];         // '-' se non fa parte di una conversazione di gruppo
    uint16_t m_len;
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
    char end_time[TIME_LEN+1];
    int port_recipient;             // porta del destinatario del messaggio
    int status;                     // 1: memorizzato dal server, 2: inviato al destinatario 
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
* Function: send_server_message
* -------------------------------
* invia messaggi sull'esito o sullo stato del servizio richiesto dal dispositivo-client
*/
void send_server_message(int socket, char* message, bool error){

    char c[RES_SIZE+1];
    uint16_t message_len;

    printf("[+]Sending outcome of request to client...\n");

    if (error)
        strcpy(c,"E");
    else strcpy(c,"S");
    // invio prima l'esito della richiesta
    send(socket, (void*)c, 2, 0);

    // invio poi un eventuale messaggio al dispositivo
    if (message){
        //invio prima la dimensione
        message_len = strlen(message);
        message_len = htons(message_len);
        send(socket, (void*)&message_len, sizeof(u_int16_t), 0);
        //invio ora il messaggio
        send(socket, (void*)message, message_len, 0);
    }

    printf("[+]Message sent to client.\n");
}


/*
* Function: remove_key
* -----------------------
* rimuove dalla lista head gli elementi che hanno sender uguale a key
*/
struct message* remove_key(char* key, struct message* head)
{
    while (head && strcmp(head->sender,key)==0)
    {
        struct message* tmp = head;
        head = head->next;
        free(tmp);
    }

    struct message* current = head;
    for (; current != NULL; current = current->next)
    {
        while (current->next != NULL && strcmp(current->next->sender,key)==0)
        {
            struct message* tmp = current->next;
            current->next = tmp->next;
            free(tmp);
        }
    }

    return head;
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
