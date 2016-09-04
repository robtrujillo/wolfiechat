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
#include "sfwrite.c"
#include <time.h>

#define BUFF_SIZE 1025
#define WELCOME(name) do{fprintf(stdout, "CHAT WITH %s\n", name);}while(0)
#define CLOSE_CMD "/close\n"

void close_cmd();
void Send(int sockfd, void* buff, int len, int flags);
void Recv(int sockfd, char* buff, int flags);
void send_msg(int sockfd);
void receive_msg(int sockfd);
void sigint_handler(int sig_num);
void sigchld_handler(int sig_num);
void wr_audit(char* event, char* str1, char* str2, char* str3);

char* name;
int sockfd, audit_fd, sigint, sensitive;
pthread_mutex_t std_lock = PTHREAD_MUTEX_INITIALIZER;


int main(int argc, char** argv){

	signal(SIGINT, sigint_handler);
	signal(SIGCHLD, sigchld_handler);
	
	/* RETRIEVE SOCKET FROM ARGV */
	sockfd = atoi(argv[1]);
	/* RETRIEVE FRIEND NAME FROM ARGV */
	name = argv[2];
	/* RETRIEVE AUDIT FD FROM ARGV */
	audit_fd = atoi(argv[3]);

	/* SIGINT HANDLER */
	sigint = 0;
	
	/* CREATE POLL STRUCTURE FOR TWO EVENTS */
	struct pollfd fds[2];

    /* MONITOR STDIN FOR INPUT TO SEND */
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;

    /* MONITOR CLIENT SOCKET FOR INCOMING */
	fds[1].fd = sockfd;
	fds[1].events = POLLIN;

	/* VARIABLE TO STORE RESULT FROM POLLS */
	int ret;

	/* WHILE LOOP TO POLL */
	while(1){

		/* WAIT 0.1 SECOND(S) FOR SOME REASON */
		ret = poll(fds, 2, 100);

		/* MAKE SURE POLL METHOD WAS SUCCESSFUL */
		if(ret == -1 ){
		   /* ERROR */
		}
		else if(ret == 0){
		    /* TIMED OUT */
		}
		else{
		    /* WAIT FOR ACTIVATED EVENT FOR STDIN */
			if(fds[0].revents & POLLIN){
				if(sensitive){
					close(sockfd);
					exit(EXIT_SUCCESS);
				}
				/* SEND MSG FROM CHAT TO CLIENT */
				send_msg(sockfd);
				fds[0].revents = 0;
			}
			/* RECEIVE MSG FROM CLIENT TO CHAT */
			if(fds[1].revents & POLLIN){
				receive_msg(sockfd);
				if(sensitive){
					int c = getc(stdin);
					c++;
					close(sockfd);
					exit(EXIT_SUCCESS);
				}
				fds[1].revents = 0;
			}
		}
	}
	return 0;	
}

void send_msg(int sockfd) {
	int  i;
	i = sigint = 0;
	char* msg = calloc(1, BUFF_SIZE);
	char ch;

	/* READ FROM STDIN */
	while((ch = fgetc(stdin)) != EOF) {
		if(ch == 10) {
			msg[i] = (char)ch;
			msg[i+1] = '\0';
			break;
		} else {
			msg[i] = (char)ch;
			i++;
		}
	}
	/* CHECK FOR "/close" COMMAND */
	if(!strcmp(msg, CLOSE_CMD) || sigint){
		free(msg);
		close_cmd();
	}
	if(!strcmp(msg, ""))
	{
		free(msg);
		return;
	}
	if(!strcmp(msg, " "))
	{
		free(msg);
		return;
	}
	/* NOW SEND MSG TO BACK TO CLIENT */
	Send(sockfd, msg, strlen(msg), 0);
	free(msg);
}

void receive_msg(int sockfd) {
	char* msg_rec = calloc(1, BUFF_SIZE);
	msg_rec[BUFF_SIZE - 1] = '\0';
	/* READ WHOLE MSG FROM CLIENT */
	if(recv(sockfd, msg_rec, BUFF_SIZE-1, 0) < 1){
		sensitive = 1;
	}
	//fprintf(stdout, "%c", msg_rec[0]);
	// int i = 0;
	// for(; i < strlen(msg_rec); i++){
	// 	fprintf(stdout, "\b");
	// }
	
	if(msg_rec[0] == '>'){
		sfwrite(&std_lock, stdout, "\x1B[1;37m%c\x1B[0m", msg_rec[0]);
		fprintf(stdout, "\x1B[0;34m%s\x1B[0m\n", msg_rec+1);
	}
	else if (msg_rec[0] == '<'){
		sfwrite(&std_lock, stdout, "\x1B[1;37m%c\x1B[0m", msg_rec[0]);
		sfwrite(&std_lock, stdout, "\x1B[0;31m%s\x1B[0m\n", msg_rec+1);
	}
	else sfwrite(&std_lock, stdout, "\x1B[1;33m%s\x1B[0m\n", msg_rec);

	free(msg_rec);
}

void sigint_handler(int sig_num){
	close(sockfd);
	close(audit_fd);
	exit(0);
	sigint = 0;

}
void sigchld_handler(int sig_num){
	close(sockfd);
	close(audit_fd);
	exit(0);

}

void wr_audit(char* event, char* str1, char* str2, char* str3){
	/* GET CURRENT DATE & TIME */
	time_t rawtime;
    struct tm *info;
    char time_stamp[64];
    time(&rawtime);
    info = localtime(&rawtime);
    strftime(time_stamp, 64,"%D-%I:%M%p", info);
    /* ALLOCATE MEMORY FOR NEW LOG */
    char* new_log = (char*)calloc(BUFF_SIZE, 1);
    /*CONCATONATE THE NEW LOG CORRECTLY */
    sprintf(new_log, "%s, %s, %s, %s, %s, %s\n"
    	, time_stamp, name, event, str1, str2, str3);

    /* LOCK THE FILE */
    flock(audit_fd, LOCK_EX);
    /* WRITE TO THE FILE */
    write(audit_fd, new_log, strlen(new_log));
    //perror("write");

    /* UNLOCK THE FILE */
    flock(audit_fd, LOCK_UN);
    //sfwrite(&std_lock, stdout, "THIS IS THE NEW LOG : %s", new_log);
    free(new_log);
}

void close_cmd(){
	/* 
	 * EITHER CLOSE THE SOCKET AND EXIT 
	 * OR ALERT THE CLIENT FIRST, AND WAIT FOR RESPONSE
	 * BEFORE WE CLOSE AND EXIT
	 */
	 wr_audit("CMD", "/close", "success", "chat");
	 close(sockfd);
	 close(audit_fd);
	 exit(EXIT_SUCCESS);
}


void Send(int sockfd, void* buff, int len, int flags){

/* ATTEMPT TO SEND BYTES */
	int bytes_sent = send(sockfd, buff, len, flags);
/* CHECK IF ALL BYTES WERE SENT */
	while(bytes_sent != len){
	/* ATTEMPT TO SEND LEFTOVER BYTES ONE BYTE AT A TIME */
		if(send(sockfd, &(((char*)buff)[bytes_sent]), 1, flags) != 1)
			continue;
		else bytes_sent++;
	}
}