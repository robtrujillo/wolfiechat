#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <semaphore.h>


#define MAX_LENGTH 1024
#define LOGIN_Q_SIZE 1000
#define EXISTING_USER 0
#define NEW_USER 1
#define INACTIVE 0
#define ACTIVE 1
#define MAX_FDS 200
#define BUFF_SIZE 1024
#define MAX_FDS  200

#define ERR_00 "ERR 00 USER NAME TAKEN"
#define ERR_01 "ERR 01 USER NOT AVAILABLE"
#define ERR_02 "ERR 02 BAD PASSWORD"
#define PROTO_TERM "\r\n\r\n"
#define CRNL "\r\n"

#define WOLFIE "WOLFIE"
#define IAM "IAM"
#define IAMNEW "IAMNEW"
#define PASS "PASS"
#define NEWPASS "NEWPASS"

#define EIFLOW "EIFLOW"
#define HINEW "HINEW"
#define AUTH "AUTH"
#define HI "HI"
#define BYE "BYE"

#define TIME "TIME"
#define LISTU "LISTU"
#define MSG "MSG"

#define EMIT "EMIT"
#define UTSIL "UTSIL"
#define CRNL "\r\n"
#define UOFF "UOFF"

#define USERS_CMD "/users\n"
#define SHUT_CMD "/shutdown\n"
#define HELP_CMD "/help\n"
#define ACCTS_CMD "/accts\n"

#define LOGIN_THREAD "LOGIN"
#define COMM_THREAD "COMMUNICATION"

#define TEMP_USER "TEMP_USER"

#define ONLINE "ONLINE"
#define OFFLINE "OFFLINE"

typedef struct user{
	time_t loginT;
	int clientfd;
	char* username;
	char* password;
	char* salt;
	int active;
	struct user* next;
}User;

int check_if_logging_alrdy(char* username);
void remove_frm_logging(char* username);

/* ARG DETERMINATION FUNCTIONS */
int init_args(int argc, char**argv);
void invalid_args();

/* LOGIN PROTOCOL FUNCTIONS */
const char* hash_password(char salt[], char* password);
int passCheck(char* name, int clientfd, int add_new);
void add_user(int sockfd, char* name, char* password);
int validate_password(char* pass);
char* parse_args(char* args, int* seq);
int wolfie_protocol(int clientfd);
void error_protocol(int sockfd, int err, char* name);
void hi_protocol(int sockfd, char* name, int add_new, char* pass);
void hi_new_protocol(int sockfd, char* name);

/* MISC. FUNCTIONS */
void free_args();
void print_prompt();
void print_usage();

/* WRAPPERS FOR COMMON SOCKET FUNCTIONS */
int Socket(int domain, int type, int protocol);
int Accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
void Setsockopt(int socket_fd, int level, int optname, const void* optval, socklen_t optlen);
void Listen(int sockfd, int backlog);
void Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
void Recv(int sockfd, char* buff, int flags);
void Send(int sockfd, void* buff, int len, int flags);

/* CLIENT COMMAND FUNCTIONS */
int commands(char* command,int sockfd);
void logout_protocol(int sockfd);
void listu_protocol(int sockfd);
void time_protocol(int sockfd);
int msg_protocol(char* msg);

/* SERVER COMMAND FUNCTIONS */
int compare(char* commands, int socket_fd);
void help_protocol();
int shutdown_server();
void print_users();
void print_accounts();
int check_online(char* username);

/* ACCOUNT SAVING/LOADING FUNCTIONS */
void create_file();
void read_file();

/* THREADS */
void *login_thread(void *vargp);
void *comm_thread(void* vargp);

/* SIGNAL HANDLERS */
void shutdown_handler(int sig_num);

/* WRAPPERS FOR COMMON STRING FUNCTIONS */
void Strcat(char* str1, char* str2){
	strcat(str1, str2);
	strcat(str1, " ");
}

void Strcat2(char* buff, char* str1, char* str2){
 	strcat(buff, str1);
 	strcat(buff, " ");
 	strcat(buff, str2);
 }

 void Strcat2_with_space(char* buff, char* str1, char* str2){
 	strcat(buff, str1);
 	strcat(buff, " ");
 	strcat(buff, str2);
 	strcat(buff, " ");
 }

 void Strcat3(char* buff, char* str1, char* str2, char* str3){
 	strcat(buff, str1);
 	strcat(buff, " ");
 	strcat(buff, str2);
 	strcat(buff, " ");
 	strcat(buff, str3);
 }

int Strcmp2(char* msg, char* cmp1, char* cmp2){
	char* cmp = (char*)calloc(1, strlen(cmp1) + strlen(cmp2) + 2);
	Strcat2(cmp, cmp1, cmp2);
	int ret = strcmp(msg, cmp);
	free(cmp);
	return ret;
}

int Strcmp3(char* msg, char* cmp1, char* cmp2, char* cmp3){
	char* cmp = (char*)calloc(1, strlen(cmp1) + strlen(cmp2) + strlen(cmp3) + 3);
	Strcat3(cmp, cmp1, cmp2, cmp3);
	int ret = strcmp(msg, cmp);
	free(cmp);
	return ret;
}

char* Strcpy(char* src){
	char* dest = (char*)calloc(1, strlen(src)+1);
	strcpy(dest, src);
	return dest;
}