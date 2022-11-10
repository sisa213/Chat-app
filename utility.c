
#define RCVBUFSIZE 128 /* Size of receive buffer */
#define MSG_BUFF 1024
#define BUFFER_SIZE 1024
#define DEFAULT_CL_PORT "4242"   // porta su cui ascolta il server
#define CMD_SIZE 3
#define RES_SIZE 1
#define USER_LEN 50
#define MSG_LEN 1024
#define DIM_BUF 1024
#define TIME_LEN 20
#define CRYPT_SALT 0xFACA

struct user_device {
    char* username;
    int port;
    char* password;
};  //da memorizzare in un file presso il server

struct message {

    char sender[USER_LEN+1];
    char recipient[USER_LEN+1];   // \0 se di gruppo
    char time_stamp[TIME_LEN+1];
    char group[USER_LEN+2];     // positivo se fa parte di una chat di gruppo, -1 altrimenti
    uint16_t m_len;
    char text[MSG_BUFF+1];
    char status[3];        // *: memorizzato dal server, **: inviato al destinatario
    struct message* next;
};

struct connection_log {
    char username[USER_LEN+1];
    uint16_t port;      
    char timestamp_login[TIME_LEN+1];
    char timestamp_logout[TIME_LEN+1];
    int socket_fd;          //non presente nel registro dei  e -1 se si tratta din una connesione vecchia da aggiornare
    struct connection_log* next;
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
