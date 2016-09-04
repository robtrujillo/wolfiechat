#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h> 
#include <errno.h>
#include "sfwrite.c"
#include <time.h>

extern pthread_mutex_t std_lock;

#define ERR_00 "ERR 00 USER NAME TAKEN"
#define ERR_01 "ERR 01 USER NOT AVAILABLE"
#define ERR_02 "ERR 02 BAD PASSWORD"
#define ERR100 "INTERNAL SERVER ERROR: BAD PROTOCOL"

#define ERR_00_PRINT "\x1B[1;31mERR 00 USER NAME TAKEN\x1B[0m\n"
#define ERR_01_PRINT "\x1B[1;31mERR 01 USER NOT AVAILABLE\x1B[0m\n"
#define ERR_02_PRINT "\x1B[1;31mERR 02 BAD PASSWORD\x1B[0m\n"
#define ERR_100(err) do{sfwrite(&std_lock, stderr, "\x1B[1;31mINTERNAL SERVER ERROR: %s\x1B[0m\n",err);}while(0)
#define PROTO_TERM "\r\n\r\n"

#define WOLFIE "WOLFIE"
#define IAM "IAM"
#define IAMNEW "IAMNEW"
#define PASS "PASS"
#define NEWPASS "NEWPASS"


#define EIFLOW "EIFLOW"
#define HINEW "HINEW"
#define AUTH "AUTH"
#define HI "HI"
#define SSAP "SSAP"
#define SSAPWEN "SSAPWEN"
#define MOTD "MOTD"
#define BYE "BYE"

#define TIME "TIME"
#define LISTU "LISTU"
#define MSG "MSG"
#define UOFF "UOFF"

#define EMIT "EMIT"
#define UTSIL "UTSIL"
#define CRNL "\r\n"

#define MESSAGE 1000
#define BUFF_SIZE 1024
#define MAX_FDS 200

#define NEW_PASS_PROMPT "Enter new password> "
#define PASS_PROMPT "Enter password> "

#define TIME_CMD "/time"
#define HELP_CMD "/help"
#define LOGOUT_CMD "/logout"
#define LISTU_CMD "/listu"
#define CHAT_CMD "/chat"
#define AUDIT_CMD "/audit"

#define LOGIN "LOGIN"
#define CMD "CMD"
#define LOGOUT "LOGOUT"
#define ERR "ERR"
#define SUCCESS "success"
#define FAIL "fail"
#define CHAT "chat"
#define CLIENT "client"
#define INTENT "intentional"
#define ERROR "error"
#define FROM "from"
#define TO "to"

/* ARG DETERMINATION FUNCTIONS */
int init_args(int argc, char**argv);
void invalid_args();

/* LOGIN PROTOCOL FUNCTIONS */
int wolfie_protocol(int serverfd);
int check_error(char* msg);
void password_protocol(int sockfd);
int goodpass_protocol(int sockfd, char* buff);
int check_hinew(char* msg);
int new_protocol(int sockfd);
int check_auth(char* msg);
int old_protocol(int sockfd);

/* MISC. FUNCTIONS */
void free_args();
void print_prompt();
void print_usage();

/* SIGNAL HANDLERS */
void sigint_handler(int sig_num);
void chat_handler(int signum);
void sigpipe_handler(int signum);

/* WRAPPERS FOR COMMON SOCKET FUNCTIONS */
int Socket(int domain, int type, int protocol);
void Connect(int* sockfd, const struct sockaddr* addr, socklen_t addrlen);
void Send(int sockfd, void* buff, int len, int flags);
void Recv(int sockfd, char* buff, int flags);
void Setsockopt(int socket_fd, int level, int optname, const void* optval, socklen_t optlen);

/* CLIENT COMMAND FUNCTIONS */
int compare(char* commands,int socketfd);
int remove_friend(pid_t pid, char* username);
int recv_bye(int sockfd);
int logout_protocol();
int time_protocol();
void help_protocol();
void listu_protocol(int sockfd);
void audit_protocol(int desc_flag);
int server_cmd(char* msg);

/* CHAT HANDLING FUNCTIONS */
int chat_protocol(char* commands, int sockfd);
int spawn_chat(char* username);
void send_msg(int chatfd, char* msg);
int parse_msg(char* msg);

/* WRAPPERS FOR COMMON STRING FUNCTIONS */
void Strcmp(char* str, char* str2, int sockfd){
	 if(strcmp(str, str2)){
 	 	close(sockfd);
 	 	free(str);
 	 	perror("\x1B[1;31mnot adherent to protocol\x1B[0m");
 	 	exit(EXIT_FAILURE);
 	}
}

void Strcat2(char* buff, char* str1, char* str2){
	strcat(buff, str1);
	strcat(buff, " ");
	strcat(buff, str2);
}

void Strcat3(char* buff, char* str1, char* str2, char* str3){
	strcat(buff, str1);
	strcat(buff, " ");
	strcat(buff, str2);
	strcat(buff, " ");
	strcat(buff, str3);
}

void Strcat5(char* buff, char* str1, char* str2, char* str3, char* str4, char* str5){		
	strcat(buff, str1);		
	strcat(buff, " ");		
	strcat(buff, str2);		
	strcat(buff, " ");		
	strcat(buff, str3);		
	strcat(buff, " ");		
	strcat(buff, str4);		
	strcat(buff, " ");		
	strcat(buff, str5);		
}

char* Strcpy(char* src){
	char* dest = (char*)calloc(1, strlen(src)+1);
	strcpy(dest, src);
	return dest;
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

