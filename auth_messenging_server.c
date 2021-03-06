#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define DATA_SIZE 100
#define TRUE 1

static _Atomic unsigned int clnt_count = 0;
static int uid = 10;

/*Hash Function: Convert the password into a hash*/
unsigned long hash(unsigned char *str)
{
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

/* Client structure */
typedef struct{
	struct sockaddr_in address;
	int sockfd;
	int uid;
	char username[32];
    char passwd[32];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clnt_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_overwrite_stdout() {
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf (char* arr, int length) {
    int i;
    for (i = 0; i < length; i++) { // trim \n
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

void print_client_addr(struct sockaddr_in serv_addr){
    printf("%d.%d.%d.%d",
        serv_addr.sin_addr.s_addr & 0xff,
        (serv_addr.sin_addr.s_addr & 0xff00) >> 8,
        (serv_addr.sin_addr.s_addr & 0xff0000) >> 16,
        (serv_addr.sin_addr.s_addr & 0xff000000) >> 24);
}

/* Add clients to queue */
void queue_add(client_t *clnt){
	pthread_mutex_lock(&clnt_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(!clients[i]){
			clients[i] = clnt;
			break;
		}
	}
	pthread_mutex_unlock(&clnt_mutex);
}

/* Remove clients to queue */
void queue_remove(int uid){
	pthread_mutex_lock(&clnt_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid == uid){
				clients[i] = NULL;
				break;
			}
		}
	}

	pthread_mutex_unlock(&clnt_mutex);
}

/* Send message to all clients except sender */
void send_message(char *s, int uid){
	pthread_mutex_lock(&clnt_mutex);

	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid != uid){
				if(write(clients[i]->sockfd, s, strlen(s)) < 0){
					perror("ERROR: write to descriptor failed");
					break;
				}
			}
		}
	}

	pthread_mutex_unlock(&clnt_mutex);
}

/* Handle all communication with the client */
void *handle_client(void *arg){
	char buff_out[BUFFER_SZ];
	char username[32];
    char passwd[32];
	int leave_flag = 0;
    char hashpass2[100];
    char line[1000];

	clnt_count++;
	client_t *cli = (client_t *)arg;

	// username
	if(recv(cli->sockfd, username, 32, 0) <= 0 || strlen(username) <  2 || strlen(username) >= 32-1){
		printf("Didn't enter the Username.\n");
		leave_flag = 1;
	} 
    else{
        //password
        if(recv(cli->sockfd, passwd, 32, 0) <= 0 || strlen(passwd) <  2 || strlen(passwd) >= 32-1){
            printf("Didn't enter the Password.\n");
		    leave_flag = 1;
	    }
        else{
            strcpy(cli->passwd, passwd);

            // converting the resultant hash(int) to hashpass2(char)
            unsigned long hash1 = hash(passwd);
            snprintf( hashpass2, DATA_SIZE, "%d", hash1 );
            FILE * fPtr;

            
            /*
            * Open file in r (read) mode.
            * "user_auth.txt" is relative path to create file
            */
            fPtr = fopen("user_auth.txt", "r");

            /* fopen() return NULL if last operation was unsuccessful */
            if(fPtr == NULL){
                /* File not created hence exit */
                printf("Unable to read user_auth.txt file.\n");
                exit(EXIT_FAILURE);
            }

            fgets(line, 1000, fPtr);

            if(strcmp(line, hashpass2) == 10)
            {
                strcpy(cli->username, username);
		        sprintf(buff_out, "%s has joined\n", cli->username);
		        printf("%s", buff_out);
		        send_message(buff_out, cli->uid);
                fclose(fPtr);
            }
            else{
                printf("Incorrect Password.\n");
		        leave_flag = 1;
            }
        }
    }

	bzero(buff_out, BUFFER_SZ);

	while(1){
		if (leave_flag) {
			break;
		}

		int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
		if (receive > 0){
			if(strlen(buff_out) > 0){
				send_message(buff_out, cli->uid);

				str_trim_lf(buff_out, strlen(buff_out));
				printf("%s -> %s\n", buff_out, cli->username);
			}
		} 
        else if (receive == 0 || strcmp(buff_out, "exit") == 0){
			sprintf(buff_out, "%s has left\n", cli->username);
			printf("%s", buff_out);
			send_message(buff_out, cli->uid);
			leave_flag = 1;
		} 
        else {
			printf("ERROR: -1\n");
			leave_flag = 1;
		}

		bzero(buff_out, BUFFER_SZ);
	}

  /* Delete client from queue and yield thread */
	close(cli->sockfd);
    queue_remove(cli->uid);
    free(cli);
    clnt_count--;
    pthread_detach(pthread_self());

	return NULL;
}

int main(int argc, char **argv){
    char hashpass[100];
	if(argc != 3){
		printf("Usage: %s <port> <password>\n", argv[0]);
		exit(1);
	}

    //Creating a password file
    FILE * fPtr;
    fPtr = fopen("user_auth.txt", "w+");

    /* fopen() return NULL if last operation was unsuccessful */
    if(fPtr == NULL)
    {
        /* File not created hence exit */
        printf("Unable to create file.\n");
        exit(EXIT_FAILURE);
    }

    // converting the resultant hash(int) to hashpass(char)
    unsigned long hash1 = hash(argv[2]);
    snprintf( hashpass, DATA_SIZE, "%d\n", hash1 );

    /* Write data to file */
    fputs(hashpass, fPtr);

    /* Close file to save file data */
    fclose(fPtr);

	int option = 1;
	int serv_sock = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in clnt_addr;
    pthread_t tid;

    /* Socket settings */
    serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    /* Ignore pipe signals */
	signal(SIGPIPE, SIG_IGN);

	if(setsockopt(serv_sock, SOL_SOCKET,(SO_REUSEPORT | SO_REUSEADDR),(char*)&option,sizeof(option)) < 0){
		perror("ERROR: setsockopt failed");
        exit(1);
	}

	/* Bind */
    if(bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR: Socket binding failed");
        exit(1);
    }

    /* Listen */
    if (listen(serv_sock, 10) < 0) {
        perror("ERROR: Socket listening failed");
        exit(1);
	}

	printf("<>?<>?<>?<>? Capstone Design 2 Chatroom Server ?<>?<>?<>?<>\n");

	while(1){
		socklen_t clilen = sizeof(clnt_addr);
		connfd = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clilen);

		/* Check if max clients is reached */
		if((clnt_count + 1) == MAX_CLIENTS){
			printf("Max clients reached. Rejected: ");
			print_client_addr(clnt_addr);
			printf(":%d\n", clnt_addr.sin_port);
			close(connfd);
			continue;
		}

		/* Client settings */
		client_t *cli = (client_t *)malloc(sizeof(client_t));
		cli->address = clnt_addr;
		cli->sockfd = connfd;
		cli->uid = uid++;

		/* Add client to the queue and fork thread */
		queue_add(cli);
		pthread_create(&tid, NULL, &handle_client, (void*)cli);

		/* Reduce CPU usage */
		sleep(1);
	}

	return 0;
}
