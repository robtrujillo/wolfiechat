

#include <stdlib.h>
#include <string.h>
#include <pthread.h>


typedef struct Friend {
	int chat_fd;
	pid_t pid;
	char* username;
	struct Friend* next;
}Friend;

/* THE HEAD OF FRIEND LIST THAT .C FILE HAS */
extern Friend* friend_list;

/* FIND FRIEND FROM USERNAME */
Friend* find_friend_frm_username(char* username) {
	/* RETURN SOCKET OF FRIEND IN LIST */
	Friend* temp = friend_list;
 	/* CHECK IF USERNAME ALREADY EXISTS IN FRIEND LIST ALREADY */
	while(temp != NULL) {
		if(strcmp(username, temp->username))
			temp = temp->next;
		else return temp;
	}
 	/* IF USERNAME ISNT IN FRIEND LIST, RETURN -1 */
	return NULL;
}

/* FIND FRIEND FROM SOCKET FD */
Friend* find_friend_frm_sock(int sockfd){
	/* IF LIST IS EMPTY, CAN'T FIND USERNAME */
	if(friend_list == NULL)
		return NULL;

	/* CREATE TEMP TO TRAVERSE LIST */
	Friend* temp = friend_list;
	while(temp != NULL){
		/* RETURN USERNAME IF THE SOCKETS MATCH */
		if(temp->chat_fd == sockfd)
			return temp;
		else temp = temp->next;
	}

	/* IF NO MATCH RETURN NULL */
	return NULL;
}

/* FIND FRIEND FROM PID NUM */
Friend* find_friend_frm_pid(pid_t pid){
 	/* RETURN PTR TO FRIEND IN LIST THAT HAS PID */
 	Friend* temp = friend_list;
 	while(temp != NULL){
 		if(temp->pid == pid)
 			return temp;
 		else{
 		 temp = temp->next;
 		}
 	}
 	return NULL;
 }

/* FIRST STEP OF ADDING NEW FRIEND. ADD W/ USERNAME */ 
void add_friend_name(char* username){
 	/* CREATE NEW FRIEND */
 	Friend* new_friend = (Friend*)calloc(1, sizeof(Friend));
 	
 	/* STORE NEW FRIEND INFO */
 	new_friend->username = (char*)calloc(1, strlen(username)+1);
 	strcpy(new_friend->username, username);

	/* ADD NEW FRIEND TO THE LIST */
 	if(friend_list == NULL)
 		friend_list = new_friend;
 	else{
 		/* CREATE TEMP TO TRAVERSE LIST TO ADD FRIEND TO END */
 		Friend* temp = friend_list;
 		while(temp->next != NULL){
 			temp = temp->next;
 		}
 		temp->next = new_friend;
 	}
}

/* SECONDARY STEP TO ADDING FRIEND. ADD SOCKFD TO SPECIFIC FRIEND */
void add_friend_sock(int chatfd, char* friend_name){
 	Friend* temp = friend_list;
 	while(temp != NULL){
 		if(strcmp(friend_name, temp->username))
 			temp = temp->next;
 		else{
 		 temp->chat_fd = chatfd;
 		 return;
 		}
 	}
}

/* SECONDARY STEP TO ADDING FRIEND. ADD CHAT PID TO SPECIFIC FRIEND */
void add_friend_pid(pid_t pid, char* friend_name){
 	
 	Friend* temp = friend_list;
 	while(temp != NULL){
 		if(strcmp(friend_name, temp->username))
 			temp = temp->next;
 		else{
 		 temp->pid = pid;
 		 return;
 		}
 	}
 }

/* CHECK IF A FRIEND WITH USERNAME EXISTS ALREADY. RETURN SOCKFD IF TRUE */
int check_friend(char* username) {
 	/* RETURN SOCKET OF FRIEND IN LIST */

 	Friend* temp = friend_list;
 	/* CHECK IF USERNAME ALREADY EXISTS IN FRIEND LIST ALREADY */
 	while(temp != NULL) {
 		if(strcmp(username, temp->username))
 			temp = temp->next;
 		else return temp->chat_fd;
 	}
 	/* IF USERNAME ISNT IN FRIEND LIST, RETURN -1 */
 	return -1;
 }