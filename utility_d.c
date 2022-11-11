
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


/*
 * Function:  menu_client
 * -----------------------
 * mostra il menù iniziale dell'utente che ha eseguito il login
 */
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

void basic_send(int sck, const char* mes){

    uint16_t lmsg;
    char buff[BUFFER_SIZE];

    strcpy(buff, mes);
    lmsg = strlen(mes)+1;
    lmsg = htons(lmsg);
    send(sck, (void*)&lmsg, sizeof(uint16_t), 0);
    send(sck, (void*)buff, strlen(buff)+1, 0);
}


void basic_receive(int sck, char* buff){

    uint16_t lmsg;
    
    recv(sck, (void*)&lmsg, sizeof(uint16_t), 0);
    lmsg = ntohs(lmsg);
    recv(sck, (void*)buff, lmsg, 0);
}

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

void first_ack_peer(const char* hn, int hp, int peer_sck, bool send_cmd){

    uint16_t lmsg, host_port;
    char cmd [CMD_SIZE+1] = "FAK";

    // invio info di chi sono ossia nome e porta
    // invio il comando
    if (send_cmd==true){
        send(peer_sck, (void*)cmd, CMD_SIZE+1, 0);
    }
    
    // invio il nome
    lmsg = strlen(hn)+1;
    lmsg = htons(lmsg);
    send(peer_sck, (void*)&lmsg, sizeof(uint16_t), 0);
    send(peer_sck, (void*)hn, strlen(hn)+1, 0);

    // invio porta
    host_port = htons(hp);
    send(peer_sck, (void*)&host_port, sizeof(uint16_t), 0);
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


void add_contact_list(char* name, int po){

    FILE* fptr;
    fptr = fopen("./contact_list.txt", "a");
    
    fprintf(fptr, "%s %d\n", name, po);
    fclose(fptr);
}

/*
* Function: sort_messages
* --------------------------
* ordina il contenuto dei file chat in base al timestamp
*/
void sort_messages(char* id){

    FILE *fp, *fp1;
    char file_name[USER_LEN+20];
    char file_name1[USER_LEN+20];
    char buff_info[DIM_BUF];
    char buff_chat[MSG_BUFF];
    struct message* new_list = NULL;
    struct message* temp;

    // ottengo i nomi dei file
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

    // scorro le righe dei file
    while( fgets(buff_info, DIM_BUF, fp)!=NULL && fgets(buff_chat, MSG_BUFF, fp1)!=NULL ) {

        struct message* new_msg = malloc(sizeof(struct message));
        if (new_msg == NULL){
            perror("[-]Memory not allocated");
            exit(-1);
        }

        // memorizzo i singoli dati riportati in ogni riga in una struct message
        sscanf (buff_info, "%s %s %s", new_msg->time_stamp, new_msg->group, new_msg->sender);
        sscanf (buff_chat, "%s %s", new_msg->status, new_msg->text);
        // aggiungo il nuovo oggetto message ad una lista 
        insert_sorted(new_list, new_msg);   // inserimento ordinato
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

    printf("[+]Cache sorted.\n");
}


char* get_name_from_sck(struct con_peer* p, int s){

    struct con_peer* temp = p;
    while (temp){
        if (temp->socket_fd==s){
            return temp->username;
        }
        temp = temp->next;
    }
    return NULL;
}

void encrypt(char password[],int key)
{
    unsigned int i;
    for(i=0;i<strlen(password);++i)
    {
        password[i] = password[i] - key;
    }
}