 /*
 * utility_s.c:     file contenente definizioni macro, strutture dati e funzioni di routine utilizzate dal server.
 *
 * Samayandys Sisa Ruiz Muenala, 10 novembre 2022
 * 
 */

/*--------------------------
*       MACRO
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

struct session_log {
    char username[USER_LEN+1];
    uint16_t port;      
    char timestamp_login[TIME_LEN+1];
    char timestamp_logout[TIME_LEN+1];
    int socket_fd;          //non presente nel registro dei  e -1 se si tratta din una connesione vecchia da aggiornare
    struct connection_log* next;
};

struct user_device {
    char* username;
    int port;
    char* password;
};  

struct message {

    char sender[USER_LEN+1];
    char recipient[USER_LEN+1];   // \0 se di gruppo
    char time_stamp[TIME_LEN+1];
    char group[USER_LEN+2];     // positivo se fa parte di una chat di gruppo, -1 altrimenti
    uint16_t m_len;
    char text[MSG_LEN];
    char status[3];        // *: memorizzato dal server, **: inviato al destinatario
    struct message* next;
};

struct con_peer {
    char username[USER_LEN+1];
    int socket_fd;          //non presente nel registro dei  e -1 se si tratta din una connesione vecchia da aggiornare
    struct con_peer* next;
};

struct chat {
    char recipient[USER_LEN+1];
    char group[USER_LEN+2];
    int sck;
    bool on;
    int users_counter;
    struct con_peer* members;
    struct message* messages;
    struct chat* next;
};

struct dv_request {
    char command[3];
    char* data;
    int socket;
};


struct ack{
    char sender[USER_LEN+1];        // il destinatario del messaggio
    char recipient[USER_LEN+1];
    char start_time[TIME_LEN+1];
    char end_time[TIME_LEN];
    int port_recipient;     // il mittente del messaggio
    int status;             // 1: memorizzato dal server, 2: inviato al destinatario 
}; // soltanto quelli di stato 1 vengono memorizzati

struct group{
    int id;
    time_t timestamp_creation;
    struct user_device creator;
    struct user_device* members;
    struct message* grp_chat;
};

struct preview_user{
    char user[USER_LEN+1];
    int messages_counter;
    char timestamp[TIME_LEN+1];
    struct preview_user* next;
};

void send_server_message(int socket, char* message, bool error){
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

struct message* remove_key(char* key, struct message* head)
{
    // remove initial matching elements
    while (head && strcmp(head->sender,key)==0)
    {
        struct message* tmp = head;
        head = head->next;
        free(tmp);
    }

    // remove non-initial matching elements
    // loop invariant: "current != NULL && current->data != key"
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


int compare_people(const struct message *a, const struct message *b) {

     return strcmp(a->time_stamp, b->time_stamp);
}


void insert_sorted(struct message* headptr, struct message *new_node) {

    // Allocate heap space for a record
    struct message *ptr = new_node;

    if (headptr == NULL || compare_people(ptr, headptr) < 0) {
        ptr->next = headptr;
        return;
    } else {
        struct message *cur = headptr;
        while (cur->next != NULL && compare_people(ptr, cur->next) >= 0) {
            cur = cur->next;
        }
        ptr->next = cur->next;
        cur->next = ptr;
        return;
    }
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
