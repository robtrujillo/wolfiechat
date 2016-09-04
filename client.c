
#include "client.h"
#include "friend.h"

char* PORT_NUMBER;
char* SERVER_IP;
char* NAME;
char* AUDIT_LOG = NULL;
int audit_fd = -1;
FILE* auditfd = NULL;
pthread_mutex_t std_lock = PTHREAD_MUTEX_INITIALIZER;
int chat_count, uoff, print_verbs, req_new, socket_fd;

struct pollfd fds_comm[200];
int nfds = 0;
int shutdn = 0;
Friend* friend_list;

int file_exists(char* file_path){
	struct stat checkPath;
	if(stat(file_path, &checkPath) == -1){
		/* FILE DOESN'T EXIST */
		return -1;
	} /* FILE EXISTS */
	else return 0;
}
void invalid_args(){
	sfwrite(&std_lock, stderr, "\x1B[1;31mError: Invalid args\x1B[0m\n");
	print_usage();
}

int init_args(int argc, char**argv){
	int opt, hcount,vcount, ccount, acount;
	hcount = vcount = ccount = acount = 0;
	if(argc < 4){
		invalid_args();
		return -1;
	}
	while((opt = getopt(argc, argv, "hvca")) != -1) {
		switch(opt) {
			sfwrite(&std_lock, stdout, "%d\n\n", argc);
			case 'h':
				hcount++;
				break;
			case 'v':
				vcount++;
				break;
			case 'c':
				ccount++;
				break;
			case 'a':
				acount++;
				if(optind < argc){
					/* COPY AUDIT LOG PATH */
					AUDIT_LOG = Strcpy(argv[optind]);
				}
				else{
					invalid_args();
					return -1;
				}
				break;
			case '?':
				invalid_args();
				return -1;
				break;
		}
	}

	/* IF ANY -h PRINT USAGE STATEMENT AND EXIT */
	if(hcount){
		print_usage();
		return 1;
	}

	/* CHECK NUMBER OF OPTIONAL ARGUMENTS */
	if(vcount > 1 || ccount > 1 || acount > 1){
		invalid_args();
		return -1;
	}
	/* PRINT VERBOSE? */
	if(vcount)
		print_verbs++;
	/* MAKING NEW ACCOUNT? */
	if(ccount)
		req_new++;
	/* IF FIRST POS ARG IS AUDIT LOG, SKIP */
	if(AUDIT_LOG != NULL){
		optind++;
	}
	/* SAVE NAME */
	if(optind < argc)
		NAME = Strcpy(argv[optind++]);
	else{
		invalid_args();
		return -1;
	}
	/* SAVE SERVER_IP */
	if(optind < argc)
		SERVER_IP = Strcpy(argv[optind++]);
	else{
		invalid_args();
		return -1;
	}
	/* SAVE PORT_NUMBER */
	if(optind < argc)
		PORT_NUMBER = Strcpy(argv[optind++]);
	else{
		invalid_args();
		return -1;
	}
	
	if(optind == argc)
		return 0;
	else if(!strcmp(argv[optind], AUDIT_LOG))
		return 0;
	else return -1;
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
    sprintf(new_log, "%s, %s, %s, %s", time_stamp, NAME, event, str1);
    if(str2 != NULL){
    	sprintf(new_log+strlen(new_log), ", %s, %s", str2, str3);
    }
    strcat(new_log, "\n");

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

int main(int argc, char** argv){
	signal(SIGCHLD, chat_handler);
	signal(SIGPIPE, SIG_IGN);
	friend_list = NULL;
	int i, index, bad_args;
	i = index = bad_args = print_verbs = req_new = 0;

	for(i=nfds;i<MAX_FDS;i++)
 		fds_comm[i].fd = -1;

	signal(SIGINT, sigint_handler);

	/* INITIALIZE ARGS */
	if((bad_args = init_args(argc, argv))){
		if(bad_args == 1){ 
    		/* ARGS WERE GOOD, -h WILL EXIT_SUCCESS */
			return EXIT_SUCCESS;
		} else {
			/* ARGS WERE BAD. EXIT_FAILURE */
			print_usage();
			return EXIT_FAILURE;
		}
	}
	/* INIT AUDIT LOG FD. "a+" OPEN FOR READING AND APPENDING */
	if(AUDIT_LOG == NULL)
		AUDIT_LOG = Strcpy("./audit.log");
	/* DISPLAY CLIENT VERBOSE FOR DEBUGGING */
	sfwrite(&std_lock, stdout, "NAME: %s\nIP: %s\nPORT: %s\nAUDIT: %s\n", NAME, SERVER_IP, PORT_NUMBER, AUDIT_LOG);
	audit_fd = open(AUDIT_LOG, O_APPEND | O_CREAT | O_RDWR, 0666);
	if(audit_fd == -1){
		/* BAD PATH */
		perror("open audit");
		//sfwrite(&std_lock, stderr, "\x1B[1;31mERROR: BAD PATH\x1B[0m\n");
		free_args();
		exit(EXIT_FAILURE);
	}
	
	
	//write(audit_fd, "BLA", 3);	

	/* START CREATING A SOCKET FOR CONNECTIONS*/
	int result;
	struct addrinfo hints, *addr;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if ((result = getaddrinfo(SERVER_IP, PORT_NUMBER, &hints, &addr))) {
		sfwrite(&std_lock, stderr, "\x1B[1;31mgetaddrinfo: %s\n\x1B[0m", gai_strerror(result));
		exit(1);
	}

	struct addrinfo *c;
	for(c = addr; c; c = c->ai_next) {
		if((socket_fd = socket(c->ai_family, c->ai_socktype, c->ai_protocol)) < 0) {
			continue;
		}
		break;
	}

	/* CONNECT TO SERVER AND INIT SOCKET_FD AS SERVER SOCKET (HAPPENS IN CONNECT) */
	Connect(&socket_fd, c->ai_addr, c->ai_addrlen);

    /* CREATE POLL STRUCTURE FOR TWO EVENTS */
	struct pollfd fds[2];

    /* MONITOR STDIN FOR INPUT */
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;

    /* MONITOR SERVER SOCKET FOR INCOMING */
	fds[1].fd = socket_fd;
	fds[1].events = POLLIN;
	int success = wolfie_protocol(socket_fd);
	/* CHECK FOR FAILURE */
	if(success){
		char* ip_port = calloc(strlen(SERVER_IP) + strlen(PORT_NUMBER) + 2, 1);
		sprintf(ip_port, "%s:%s", SERVER_IP, PORT_NUMBER);
		if(success == -1){
			/* ERR 01 USER NOT AVAILABLE */
			sfwrite(&std_lock, stderr, ERR_01_PRINT);
			wr_audit(LOGIN, ip_port, FAIL, ERR_01);
		}
		else if(success == -2){
			/* ERR 02 BAD PASSWORD */
			sfwrite(&std_lock, stderr, ERR_02_PRINT);
			wr_audit(LOGIN, ip_port, FAIL, ERR_02);
		}
		else if(success == -3){
			/* ERR 00 BAD PASSWORD */
			sfwrite(&std_lock, stderr, ERR_00_PRINT);
			wr_audit(LOGIN, ip_port, FAIL, ERR_00);
		}
		else if(success == 1){
			/* ERR 100 BAD PROTOCOL (macro) */
			ERR_100("BAD PROTOCOL");
			wr_audit(LOGIN, ip_port, FAIL, ERR100);
		}
		free(ip_port);
		free_args();
		exit(EXIT_FAILURE);
	}

	/* LOGIN SUCCESSFUL! WRITE TO AUDIT FILE */
	// char* ip_port = calloc(strlen(SERVER_IP) + strlen(PORT_NUMBER) + 2, 1);
	// sprintf(ip_port, "%s:%s", SERVER_IP, PORT_NUMBER);
	// wr_audit(LOGIN, ip_port, SUCCESS, )


	int ret;
	char* msg = calloc(1, BUFF_SIZE);
	print_prompt();
	/* CLIENT WHILE LOOP MIGHT NEED TO BE DIFFERENT THAN SERVER WHILE LOOP (NO SOCKET LISTENER I THINK) */
	while(1){

		/* WAIT 10 SECONDS FOR SOME REASON */
		ret = poll(fds, 2, 1000);

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
				/* PERFORM THE COMMAND SENT FROM SERVER */
				int result;
				result = compare(msg, socket_fd);
				if(result == 1){
					//sfwrite(&std_lock, stdout, "LOGGING OUT NOW\n");
					free(msg);
					free_args();
					close(audit_fd);
					exit(EXIT_SUCCESS);
				}
				else if(result == -1){
					ERR_100("BAD PROTOCOL");
					wr_audit(ERR, ERR100, NULL, NULL);
				}
				print_prompt();
				fds[0].revents = 0;
				memset(msg, 0, strlen(msg));
			}
			/* LISTEN FOR INPUT FROM SERVER */
			if(fds[1].revents & POLLIN){
				// char* msg = calloc(1, BUFF_SIZE);
				Recv(socket_fd, msg, 0);
				server_cmd(msg);
				memset(msg, 0, strlen(msg));
			}
		}

		ret = poll(fds_comm, nfds+1, 1000);
		/* MAKE SURE POLL METHOD WAS SUCCESSFUL */
		if(ret == -1 ){
		   /* ERROR */
		}
		else if(ret == 0){
		    /* TIMED OUT */
		}
		else{
			/* LOOP THROUGH FDS SET */
			for(i = 0; i < nfds; i++){
  		 		/* IF NEGATIVE SKIP */
				if(fds_comm[i].fd < 0)
					continue;
	  			/* CHECK FOR INPUT ON CHAT SOCKETS */
				if(fds_comm[i].revents & POLLIN){
					/* RECEIVE FROM CHAT */
					fds_comm[i].revents = 0;
					sfwrite(&std_lock, stdout,"Received Message from Chat\n");
					recv(fds_comm[i].fd, msg, BUFF_SIZE-1, 0);
					if(strlen(msg)){
						/* SEND WRAPPED MSG TO SERVER */
						send_msg(fds_comm[i].fd, msg);
					}

					//sfwrite(&std_lock, stdout, "MSG LENGTH: %lu\n", strlen(msg));

					/* CLEAR MSG AND FREE STR */
					memset(msg, 0, BUFF_SIZE);
				}
			}
		}

	}
	/* PRINT ARG INFORMATION FOR DEBUGGING PURPOSES */
	//sfwrite(&std_lock, stdout, "NAME: %s\r\nSERVER_IP: %s\r\nSERVER_PORT: %s\r\n", NAME, SERVER_IP, PORT_NUMBER);
	return 0;	
}


void chat_handler(int signum){
	pid_t child_pid;
	int status;
	/* REAP ANY CHAT PROCESSESS */
	while((child_pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0){
		/* REMOVE FRIEND FROM LIST AND REMOVE SOCKET FROM FDS */
		remove_friend(child_pid, NULL);
		write(1,"CHAT ENDED\n", 11);
	}
}

void sigpipe_handler(int signum){
	/*  */
	write(1,"HANDLED2\n", 9);
}

int remove_friend(pid_t pid, char* username){
	if(username != NULL) {
		int fr_pid = find_friend_frm_username(username)->pid;
		return remove_friend(fr_pid, NULL);
	}
	/* REMOVE SOCKET FROM FDS_COMM  */
	Friend* friend = find_friend_frm_pid(pid);
	if(friend == NULL)
		return -1;
	int sockfd = friend->chat_fd;
	int i, j;
	for(i=0;i<nfds;i++){
		if(fds_comm[i].fd == sockfd){
			for(j=i; j<nfds-1;j++){
				fds_comm[j].fd = fds_comm[j+1].fd;
			}
			fds_comm[nfds-1].fd = -1;
			break;
		}
	}
	nfds--;
	
	/* CHECK IF FRIEND LOGGED OFF OR DISCONNECTED */
	if(uoff){
		/* SEND DISCONNECTED MESSAGE TO CHAT */
		char* msg2 = calloc(1, strlen(friend->username) + strlen(" has left the chat!") + 1);
		Strcat2(msg2, friend->username, "has left the chat!");
		Send(sockfd, msg2, strlen(msg2), 0);
		free(msg2);
		uoff = 0;
	}
	/* CLOSE SOCKET WHICH WILL ALSO MAKE CHAT "SENSITIVE" */


	/* REMOVE FRIEND FROM FRIEND LIST AND FREE THEIR MEMORY */
	char* friend_name = friend->username;
	Friend* temp = friend_list;
	if(strcmp(temp->username, friend_name)){
		while(temp->next != NULL){
			/* IF THE NEXT FRIEND IS THE FRIEND TO REMOVE */
			if(!strcmp(friend_name, temp->next->username)){
				free(temp->next->username);
				Friend* temp2 = temp->next;
				temp->next = temp->next->next;
				free(temp2);
				close(sockfd);
				return 0;
			}
			else temp = temp->next;
		}
	}
	else{
		/* FIRST FRIEND IS FRIEND TO REMOVE */
		close(sockfd);
		friend_list = friend_list->next;
		free(temp->username);
		free(temp);
		return 0;
	}

	return -1;
}

void send_msg(int chatfd, char* msg){

	/* FIND OUT USERNAME OF FRIEND THIS CHAT IS CONNECTED TO */
	char* friend_name = find_friend_frm_sock(chatfd)->username;

	/* WRAP THE MESSAGE IN PROTOCOL MUMBO JUMBO */
	char* str = calloc(1, strlen(MSG) + strlen(friend_name) + strlen(NAME) +
	 strlen(msg) + strlen(PROTO_TERM) + 5);
	
	/* SEND WRAPPED MESSAGE TO SERVER */
	//sfwrite(&std_lock, stdout, "CHAT IS SENDING THIS MESSAGE TO CLIENT TO WRAP: %s\n", msg);
	Strcat5(str, MSG, friend_name, NAME, msg, PROTO_TERM);
	Send(socket_fd, str, strlen(str), 0);

	free(str);
	return;
}

int parse_msg(char* msg){
	/* msg IN FORM: "MSG <TO> <FROM> <MESSAGE> PROTO_TERM" */
	char* copy = Strcpy(msg);
	char* token = strtok(copy, " ");
	char* to, *from, message[100];
	memset(message, 0, 100);
	
	/* IF THERE WERE NO SPACES THEN BAD PROTOCOL */
	if(token == NULL)
		return -1;
	/* IF FIRST TOKEN IS NOT "MSG" BAD PROTOCOL */
	if(strcmp(token, MSG))
		return -1;
	/* VARIABLES */
	int i, am_receiver;
	am_receiver = 0;

	/* FIND BOTH NAMES */
	for(i = 0; i < 2; i++){
		token = strtok(NULL, " ");
		if(token == NULL)
			return -1;
		/* STORE OTHER NAME */
		if(i == 0)
			to = Strcpy(token);
		if(i==1)
			from = Strcpy(token);
	}
	/* GET THE MESSAGE */
	token = strtok(NULL, "\r\n");
	//message = Strcpy(token);
	
	/* CHECK IF CLIENT IS THE RECEIVER i IF NAME = <TO> */
	if(!strcmp(to, NAME))
		am_receiver = 1;

	/* VARIABLES FOR FRIEND INFO */
	int friend_fd;
	char* friend;

	if(am_receiver){
		friend_fd = check_friend(from);
		friend = from;
		message[0] = '>';
		message[1] = ' ';
		strcat(message+2, token);
		wr_audit(MSG, FROM, friend, message+2);
	}
	else{
	 	friend_fd = check_friend(to);
	 	friend = to;
	 	message[0] = '<';
	 	message[1] = ' ';
	 	strcat(message+2, token);
	 	wr_audit(MSG, TO, friend, message+2);
	}



	//sfwrite(&std_lock, stdout, "%s\n", message);

	/* IF NOT IN LIST, THEN SPAWN NEW CHAT */
	if(friend_fd == -1){
		/* ADD TO LIST */
		add_friend_name(friend);
		/* SPAWN NEW CHAT. SOCKET WILL BE UPDATED IN SPAWN_CHAT */
		friend_fd = spawn_chat(friend);
		/* CHAT NOW OPEN. SEND MSG TO CHAT */
		send(friend_fd, message, strlen(message), 0);
	}
	else{
		/* USERNAME FOUND IN LIST */
		/* CHECK IF CHAT WAS CLOSED */

		/* CHAT ALREADY OPEN. SEND MSG TO CHAT */
		send(friend_fd, message, strlen(message), 0);
	}

	//sfwrite(&std_lock, stdout, "PARSE_MSG: SENDING TO %s: %s\nON SOCKET: %d\n", friend, token, friend_fd);

 	/* FREE MEMORY */
 	free(to);
 	free(from);
 	free(copy);

	return 0;
}

int spawn_chat(char* friend_name){

 	/* CREATE A UNIX SOCKET AND FORK */
 	int chat_fds[2];		
 	socketpair(AF_UNIX, SOCK_STREAM, 0, chat_fds);

 	/* ADD NEW CHAT SOCKET TO FRIEND IN LIST */
 	add_friend_sock(chat_fds[0], friend_name);

 	/* ADD NEW CHAT SOCKET TO FDS SET */
 	fds_comm[nfds].fd = chat_fds[0];
 	fds_comm[nfds].events = POLLIN;
 	nfds++;

	/* FORK THE CHAT */
 	pid_t pid;	
 	if((pid =fork()) == -1) {
 		perror("Failed fork process.");
 		return -1;
 	} else if(pid == 0) {
 		/* CLOSE PARENT'S FD */
 		close(chat_fds[0]);
 		chat_count++;
 		char* geo = calloc(1, 16);
 		char* fd = calloc(1, 16);
 		sprintf(geo, "45x35+%d", nfds);
 		sprintf(fd, "%d", audit_fd);
 		char* sock = calloc(1, 10);
 		sprintf(sock, "%d", chat_fds[1]);
 		char* args[] = {
 			"xterm",
 			"-fn",
 			"8x16",
 			"-fg",
 			"PapayaWhip",
 			"-bg",
 			"rgb:128/255/255",
 			"-geometry",
 			geo,
 			"-T",
 			friend_name,
 			"-e",
 			"./chat",
 			sock,
 			NAME,
 			fd
 		};	
 		execv("/usr/bin/xterm", args);
 	}
 	/* ADD PID OF CHAT TO FRIEND IN LIST */
 	add_friend_pid(pid, friend_name);
 	/* CLOSE FD THAT IS FOR THE CHILD */
 	close(chat_fds[1]);
 	/* RETURN NEW FD */	
 	return chat_fds[0];
 }


int chat_protocol(char* commands, int sockfd) {
 	
 	/* commands in form of: "MSG <TO> <MSG>" */

 	/* OVERWRITE COMMANDS TO START FROM <TO> */		
 	commands = strstr(commands, " ");		
 	commands++;	

	/*GET <TO> */		
 	char* receiver = strtok(commands, " ");
 	commands++;		
	
	/* GET <MESSAGE> */		
 	char* msg = strtok(NULL, "\n");
 	if(msg == NULL)
 		return -1;	
	
	/* SEND MSG <TO> <FROM> <MESSAGE> TO SERVER */		
 	char* init_chat = calloc(1, strlen(MSG) + strlen(receiver) + 
 		strlen(NAME) + strlen(msg) + strlen(PROTO_TERM) + 5);		
 	Strcat5(init_chat, MSG, receiver, NAME, msg, PROTO_TERM);		
 	Send(sockfd, init_chat, strlen(init_chat), 0);	

 	/* 
 	 * AFTER SENDING THE SERVER WILL RESEND IT BACK
 	 * AND IT WILL BE HANDLED BY THE MULTIPLEX ON THE
 	 * SERVER SOCKET 
 	 */
 	return 0;	
 }

int server_cmd(char* msg){
	
	/* CHECK FOR "BYE" FROM SERVER  TO SIGNAL LOGOUT */
	if(!Strcmp2(msg, BYE, PROTO_TERM)){
		sfwrite(&std_lock, stdout, "Server shutting down...\n");
		shutdn = 1;
		logout_protocol();
		sfwrite(&std_lock, stdout,"Logging off...\n");
		free(msg);
		close(audit_fd);
		free_args();
		exit(EXIT_SUCCESS);
	}
	/* CHECK FOR "MSG <NAME> <NAME> <MESSAGE>" FROM SERVER */
	else if(strstr(msg, MSG) != NULL && strstr(msg, MSG) == msg){
		return parse_msg(msg);
	}
	else if(strstr(msg, UOFF) != NULL && strstr(msg, UOFF) == msg) {
	 	char* token = strtok(msg, " ");
		token = strtok(NULL, " ");
		int fd;
		if((fd = check_friend(token)) != -1){
			uoff =1;
			pid_t fr_pid = find_friend_frm_sock(fd)->pid;
			return remove_friend(fr_pid, NULL);
		}
		return -1;
	 }
	else return -1;	
			 	
}

void audit_protocol(int desc_flag){
	char buff;
	int bytes;
	/* LOCK AUDIT LOG FILE */
	flock(audit_fd, LOCK_EX);
	/* SET FILE POS TO BEGINNING OF THE FILE */
	lseek(audit_fd, 0, SEEK_SET);
	/* READ AND PRINT THE ENTIRE FILE */
	while((bytes = read(audit_fd, &buff, 1)) > 0){
		sfwrite(&std_lock, stdout, "%c", buff);
	}
	/* SET THE FILE POS BACK TO THE END OF THE FILE */
	lseek(audit_fd, 0, SEEK_END);
	/* UNLOCK AUDIT LOG FILE */
	flock(audit_fd, LOCK_UN);
}

int compare(char* commands,int socketfd) {
	int ch, ret, i;
	i = ret = 0;
	commands[BUFF_SIZE-1] = '\0';
	while((ch = fgetc(stdin)) != EOF) {
		/* IF CHAR IS SPACE */
		if(ch == 32 && strstr(commands, CHAT_CMD) == NULL) {
			continue;
		} else if(ch == 10) {
			commands[i] = '\0';
			commands[i+1] = '\0';
			break;
		} else {
			commands[i] = (char) ch;
			i++;
		}
	}
	if(!strcmp(commands, LISTU_CMD)) {
		listu_protocol(socketfd);
		wr_audit(CMD, LISTU_CMD, SUCCESS, CLIENT);
		ret = 0;
	} 
	else if(!strcmp(commands, HELP_CMD)) {
		help_protocol();
		wr_audit(CMD, HELP_CMD, SUCCESS, CLIENT);
		ret = 0;
	} 
	else if(!strcmp(commands, LOGOUT_CMD)) {
		ret = logout_protocol();
		if(ret)
			wr_audit(CMD, LOGOUT_CMD, SUCCESS, CLIENT);
		else wr_audit(CMD, HELP_CMD, FAIL, CLIENT);
	} 
	else if(!strcmp(commands, TIME_CMD)){
		ret = time_protocol();
		if(ret)
			wr_audit(CMD, TIME_CMD, FAIL, CLIENT);
		else wr_audit(CMD, TIME_CMD, SUCCESS, CLIENT);
	} 
	else if(strstr(commands, CHAT_CMD) != NULL && strstr(commands, CHAT_CMD) == commands) {
		ret = chat_protocol(commands, socketfd);
		if(ret)
			wr_audit(CMD, CHAT_CMD, FAIL, CLIENT);
		else wr_audit(CMD, CHAT_CMD, SUCCESS, CLIENT);
 	}
 	else if(!strcmp(commands, AUDIT_CMD)){
 		audit_protocol(0);
 		wr_audit(CMD, AUDIT_CMD, SUCCESS, CLIENT);
 		ret = 0;
 	}
 	else if(!strcmp(commands, "/audit -d")){
 		audit_protocol(1);
 		wr_audit(CMD, AUDIT_CMD, SUCCESS, CLIENT);
 		ret = 0;
 	}
 	else if(!strcmp(commands, "\n")){
 		ret = 0;
 	}
	else {
		sfwrite(&std_lock, stdout, "Invalid command: %sThe following are valid client commands:\n", commands);
		help_protocol();
		wr_audit(CMD, commands, FAIL, CLIENT);
		ret = 0;
	}
	return ret;
	//fprintf(stdout, "%s", commands);

	//memset(commands, 0, strlen(*commands));
}
void print_prompt(){
	sfwrite(&std_lock, stdout, "\x1B[1;36mclient cmd>\x1B[0m ");
	fflush(stdout);
}

void password_protocol(int sockfd){
	char* msg, *password;
	if(req_new){
		/* SENDING NEW PASSWORD FOR CREATION OF ACCOUNT */
		password = getpass(NEW_PASS_PROMPT);
		msg = calloc(1, strlen(NEWPASS) + strlen(password) + strlen(PROTO_TERM) + 3);
		Strcat3(msg, NEWPASS, password, PROTO_TERM);
	}
	else{
		/* SENDING PASSWORD FOR LOGIN */
		password = getpass(PASS_PROMPT);
		msg = calloc(1, strlen(PASS) + strlen(password) + strlen(PROTO_TERM) + 3);
		Strcat3(msg, PASS, password, PROTO_TERM);
	}
	/* SEND THAT PASSWORD */
	Send(sockfd, msg, strlen(msg), 0);
	free(password);
	free(msg);

}

int goodpass_protocol(int sockfd, char* buff){
 	char* cmp = calloc(1, BUFF_SIZE);

 	/* CHECK FOR "SSAP" OR "SSAPWEN" */
 	if(req_new){
 		Strcat2(cmp, SSAPWEN, PROTO_TERM);
 		if(strcmp(buff, cmp)){
 			free(cmp);
 			return -1;
 		}
 		memset(cmp, 0, BUFF_SIZE);
 	}
 	else{
 		Strcat2(cmp, SSAP, PROTO_TERM);
 		if(strcmp(buff, cmp)){
 			free(cmp);
 			return -1;
 		}
 		memset(cmp, 0, BUFF_SIZE);
 	}

 	/* WE ARE DONE W/ CMP */
 	free(cmp);
 	
 	/* RECV "HI <NAME>" AND CHECK FOR "HI" */
 	buff = realloc(buff, BUFF_SIZE);
 	memset(buff, 0, BUFF_SIZE);
 	Recv(sockfd, buff, 0);

 	/* B/C THERE IS A SPACE BETWEEN THE VERBS TO CHECK, STRTOK */
 	buff = strtok(buff, " ");
 	//strcat(cmp, HI);
 	if(strcmp(buff, HI)){
 		return -1;
 	}
 	/* CLEAR BUFF FOR NEXT RECV */
 	memset(buff, 0, BUFF_SIZE);

 	/* LAST THING IS TO CHECK FOR "MOTD <MOTD>" BY LOOKING FOR "MOTD" */
 	Recv(sockfd, buff, 0);
 	buff = strtok(buff, " ");
 	if(strcmp(buff, MOTD)){
 		return -1;
 	}
 	buff = strtok(NULL, CRNL);
 	sfwrite(&std_lock, stdout, "\x1B[1;33m%s\x1B[0m\n", buff);

 	/* SUCCESSFUL LOGIN. RETURN SUCCESS */
 	/* LOGIN SUCCESSFUL! WRITE TO AUDIT FILE */
	char* ip_port = calloc(strlen(SERVER_IP) + strlen(PORT_NUMBER) + 2, 1);
	sprintf(ip_port, "%s:%s", SERVER_IP, PORT_NUMBER);
	wr_audit(LOGIN, ip_port, SUCCESS, buff);
	free(ip_port);
 	return 0;
}

int check_hinew(char* msg){
	char* verbs = calloc(1, strlen(HINEW) + strlen(NAME) + strlen(PROTO_TERM) + 3);
	Strcat3(verbs, HINEW, NAME, PROTO_TERM);
	if(strcmp(verbs,msg)){
		free(verbs);
		return -1;
	}
	free(verbs);
	return 0;
}

int new_protocol(int sockfd){
	/* NOW SEND BACK TO CLIENT "IAMNEW <NAME>" */
	char* msg = calloc(1, BUFF_SIZE);
	Strcat3(msg, IAMNEW, NAME, PROTO_TERM);
 	Send(sockfd, msg, strlen(msg), 0);

 	/* RECEIVE THE RESPONSE. IT'LL EITHER BE ERR00 OR "HINEW <NAME>" */
 	memset(msg, 0, strlen(msg));
 	Recv(sockfd, msg, 0);

 	int err_num;
 	/* CHECK FOR ERR00 */
 	if((err_num = check_error(msg))){
 		recv_bye(sockfd);
 		free(msg);
 		return err_num;
 	}

 	/* NOT AN ERROR SO CHECK FOR "HINEW <NAME>" */
 	if(check_hinew(msg)){
 		//recv_bye(sockfd);
 		free(msg);
 		return 1;
 	}

 	/* RECEIVED "HINEW <NAME>". NOW SEND PASSWORD TO SERVER */
 	password_protocol(sockfd);

 	/* RECEIVE RESPONSE FROM SERVER. EXPECT EITHER "SSAPWEN" OR ERR 02 */
 	memset(msg, 0, BUFF_SIZE);
 	Recv(sockfd, msg, 0);

 	/* CHECK IF ERROR WAS RECEIVED FROM SERVER */
 	if((err_num = check_error(msg))){
 		recv_bye(sockfd);
 		free(msg);
 		return err_num;
 	}

 	/* NOT AN ERROR SO CHECK FOR GOOD PASSWORD SEQUENCE IN PROTOCOL ie: "SSAPWEN" + "HI <NAME>" + "MOTD <MOTD>" */
 	if(goodpass_protocol(sockfd, msg)){
 		free(msg);
 		return 1;
 	}

 	/* LOGIN SUCCESSFUL. RETURN 0 */
 	free(msg);
 	return 0;
}

int check_error(char* msg){
	
	char* err;
	/* CHECK FOR ERR 00 */
	err = calloc(1, BUFF_SIZE);
	Strcat2(err, ERR_00, PROTO_TERM);
	if(!strcmp(msg, err)){
		free(err);
		return -3;
	}
	
	/* CHECK FOR ERR 01 */
	memset(err, 0, strlen(err));
	Strcat2(err, ERR_01, PROTO_TERM);
	if(!strcmp(msg, err)){
		free(err);
		return -1;
	}

	/* CHECK FOR ERR 02 */
	memset(err, 0, strlen(err));
	Strcat2(err, ERR_02, PROTO_TERM);
	if(!strcmp(msg, err)){
		free(err);
		return -2;
	}
	free(err);

	/* MSG WAS NOT AN ERROR STATEMENT */
	return 0;
}

int recv_bye(int sockfd){
	/* RECV THE "BYE" */
	char* msg = calloc(1, BUFF_SIZE);
	Recv(sockfd, msg, 0);

	/* CHECK THAT IT SAYS "BYE" */
	char* cmp = calloc(1, strlen(BYE) + strlen(PROTO_TERM) + 2);
	Strcat2(cmp, BYE, PROTO_TERM);
	if(strcmp(msg, cmp)){
		free(msg);
		free(cmp);
		return -1;
	}
	else return 0;
}

int check_auth(char* msg){
	char* auth = calloc(1, strlen(AUTH) + strlen(NAME) + strlen(PROTO_TERM) + 3);
	Strcat3(auth, AUTH, NAME, PROTO_TERM);
	if(strcmp(auth,msg)){
		free(auth);
		return -1;
	}
	free(auth);
	return 0;
}

int old_protocol(int sockfd){
	/* NOW SEND BACK TO CLIENT "IAM <NAME>" */
	char* msg = calloc(1, BUFF_SIZE);
	Strcat3(msg, IAM, NAME, PROTO_TERM);
 	Send(sockfd, msg, strlen(msg), 0);

 	/* RECEIVE THE RESPONSE. IT'LL EITHER BE ERR00, ERR01, OR "AUTH" */
 	memset(msg, 0, BUFF_SIZE);
 	Recv(sockfd, msg, 0);

 	/* CHECK IF ERROR WAS RECEIVED FROM SERVER */
 	int err_num;
 	if((err_num = check_error(msg))){
 		recv_bye(sockfd);
 		free(msg);
 		return err_num;
 	}

 	/* NOT AN ERROR SO CHECK FOR "AUTH" */
 	if(check_auth(msg)){
 		//recv_bye(sockfd);
 		free(msg);
 		return 1;
 	}

 	/* RECEIVED "AUTH". NOW SEND PASSWORD TO SERVER */
 	password_protocol(sockfd);


 	/* RECEIVE RESPONSE FROM SERVER. EXPECT EITHER "SSAP" OR ERR 02 */
 	memset(msg, 0, BUFF_SIZE);
 	Recv(sockfd, msg, 0);
 	
 	/* CHECK IF ERROR WAS RECEIVED FROM SERVER */
 	if((err_num = check_error(msg))){
 		recv_bye(sockfd);
 		free(msg);
 		return err_num;
 	}

 	/* NOT AN ERROR SO CHECK FOR GOOD PASSWORD SEQUENCE IN PROTOCOL ie: "SSAP" + "HI <NAME>" + "MOTD <MOTD>" */
 	if(goodpass_protocol(sockfd, msg)){
 		free(msg);
 		return 1;
 	}

 	/* LOGIN SUCCESSFUL. RETURN 0 */
 	free(msg);
 	return 0;
}

void sigint_handler(int sig_num){
	/* RESET THE SIGINT THINGY. (NOT REALLY NECESSARY I GUESS) */
	//signal(SIGINT, sigint_handler);
	/* FREE */
	//free_args();
	/* INITIATE LOGOUT */
	logout_protocol();
	close(audit_fd);
	free_args();
	/* EXIT GRACEFULLY */
	exit(EXIT_SUCCESS);
}

int wolfie_protocol(int serverfd){

 	/* NOW SEND BACK TO CLIENT "WOLFIE" */
 	Send(serverfd, "WOLFIE \r\n\r\n", strlen("WOLFIE \r\n\r\n"), 0);
	/* CREATE STRING TO RECEIVE "EIFLOW" FROM CLIENT */
 	char* buff = calloc(1, strlen("EIFLOW \r\n\r\n")+1);
 	/* RECEIVE "EIFLOW" */
    Recv(serverfd, buff, 0);
    /* CHECK THAT THE SERVER IS FOLLOWING PROTOCOL */
    Strcmp(buff, "EIFLOW \r\n\r\n", serverfd);

    free(buff);
    /* INT TO STORE RETURN RESULT OF PROTOCOL FUNCTIONS */
    int success;
    if(req_new)
    	success = new_protocol(serverfd);
    else{
    	/* IF ERROR WAS RECEIVED FROM SERVER RETURN +1 */
    	success = old_protocol(serverfd);
    }
    return success;
}

void free_args(){
	free(NAME);
	free(PORT_NUMBER);
	free(SERVER_IP);
	free(AUDIT_LOG);
}

int time_protocol(){

	/* CREATE TIME STRING */
	char* time_verb = calloc(1, strlen(TIME) + strlen(PROTO_TERM) + 2);
	Strcat2(time_verb, TIME, PROTO_TERM);
	
	/* SEND "TIME" TO SERVER */
	Send(socket_fd, time_verb, strlen(time_verb), 0);
	memset(time_verb, 0, strlen(time_verb));

	/* GRAP "EMIT <TIME> " */
	Recv(socket_fd, time_verb, 0);
	char* token = strtok(time_verb, PROTO_TERM);
	if(token == NULL)
		return -1;
	
	/* GRAB "EMIT" */
	token = strtok(token, " ");
	if(strcmp(token, EMIT))
		return -1;
	
	/* NOW GRAB THE TIME */
	token = strtok(NULL, " ");
	
	/* CHECK THAT IT IS A NUMBER */
	if(!atoi(token))
		return -1;
	
	/* GET HOURS, MINS, SECS AND PRINT TO STDOUT */
	int total_sec = atoi(token);
	int hours = total_sec / 3600;
	total_sec = total_sec % 3600;
	int mins = total_sec / 60;
	total_sec = total_sec % 60;
	sfwrite(&std_lock, stdout, "connected for %d hour(s), "
		"%d minute(s), and %d second(s)\n", hours,mins,total_sec);
	return 0;
}

int logout_protocol(){

	if(!shutdn){
		sfwrite(&std_lock, stdout, "\nLogging out...\n");
		/* CREATE GOODBYE STRING */
		char* goodbye = calloc(1, strlen(BYE) + strlen(PROTO_TERM) + 2);
		Strcat2(goodbye, BYE, PROTO_TERM);
		/* SEND "BYE" TO SERVER TO SIGNAL LOGGING OUT */
		Send(socket_fd, goodbye, strlen(goodbye), 0);
		/* RECEIVE "BYE" FROM SERVER TO POLITELY LOGOUT */
		memset(goodbye, 0, strlen(goodbye));
		/* CLOSE SOCKET TO SERVER */
		Recv(socket_fd, goodbye, 0);
		free(goodbye);
		wr_audit(LOGOUT, INTENT, NULL, NULL);
	}
	else wr_audit(LOGOUT, ERROR, NULL, NULL);
	/* CLOSE SERVER SOCKET */
	close(socket_fd);
	/* FREE, BID TIDINGS, CLOSE SOCKETS */
	Friend* temp = friend_list;
	while(temp != NULL){
		/* FREE USERNAME */
		free(temp->username);
		/* CLOSE CHAT */
		kill(temp->pid, SIGINT);
		/* CLOSE FRIEND CHAT SOCKET */
		close(temp->chat_fd);
		/* CREATE TEMP2 TO STORE PTR TO THIS FRIEND */
		Friend* temp2 = temp;
		/* SET TEMP TO NEXT FRIEND IN LIST */
		temp = temp->next;
		/* FREE PREV FRIEND IN LIST */
		free(temp2);
	}
	return 1;
}

void Connect(int* sockfd, const struct sockaddr* addr, socklen_t addrlen){
	
	if(connect(*sockfd, addr,addrlen)) {
		perror("\x1B[1;31mconnect\x1B[0m");
		close(*sockfd);
		exit(EXIT_FAILURE);
	} else {
		/* CONNECTION MADE! */
		//sfwrite(&std_lock, stdout,"CONNECTION SUCCESSFUL\n");
	}
}

int Socket(int domain, int type, int protocol){
	int sockfd;
	if((sockfd = socket(domain,type, protocol)) == -1) {
		perror("\x1B[1;31msocket\x1B[0m");
		exit(EXIT_FAILURE);
	}
	return sockfd;
}

void Setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen){
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int))) {
		perror("\x1B[1;31msetsockopt\x1B[0m");
		exit(EXIT_FAILURE);
	}
}

void Recv(int sockfd, char* buff, int flags) {
     int eof;
     char* ptr = buff;
     while((eof =recv(sockfd, ptr, 1, 0)) > 0) {
         if(*ptr == '\n') {
             if(strstr(buff, PROTO_TERM) == NULL) {
                 ptr++;
                 continue;
             } else {
                 break;
             }
         }
         ptr++;
     }

     if(eof==0){
     	int i;
  		for(i = 0; i < nfds; i++){
  			if(sockfd == fds_comm[i].fd){
  				Friend* fr = find_friend_frm_sock(fds_comm[i].fd);
  				char* msg = calloc(1, 18 + 2 + strlen(fr->username));
  				Strcat2(msg, fr->username, "has left the chat.");
  				sfwrite(&std_lock, stdout, "FRIEND HAS LEFT CHAT\n");
  				remove_friend(fds_comm[i].fd, NULL);
  				close(sockfd);
  				break;
  			}
  		}
  		/* CHECK IF SERVER SOCKET */
  		if(sockfd == socket_fd){
  			shutdn = 1;
  			logout_protocol();
  			sfwrite(&std_lock, stdout, "DISCONNECTED FROM SEVER...\nLOGGING OFF...\n");
  			close(audit_fd);
			free_args();
  			exit(EXIT_FAILURE);
  		}
         
         //perror("\x1B[1;31mclosed connection\x1B[0m");
        //exit(EXIT_FAILURE);
     }

     if(print_verbs && !flags){
         char* verbs = calloc(1, strlen(buff)+1);
         strcpy(verbs,buff);
         char* token = strtok(verbs, PROTO_TERM);
         while(token != NULL){
             sfwrite(&std_lock, stdout,"RECEIVING: \x1B[1;34m%s\x1B[0m\n", token);
             token = strtok(NULL, PROTO_TERM);
         }
         free(verbs);
     }
      else if(print_verbs && flags) {
     	 char* verbs = calloc(1, strlen(buff)+1);
         strcpy(verbs,buff);
         char* token = strtok(verbs, " ");
         sfwrite(&std_lock, stdout,"RECEIVING: \x1B[1;34m%s\x1B[0m\n", token);
         token = strtok(NULL, CRNL);
         while(token != NULL){
             sfwrite(&std_lock, stdout,"RECEIVING: \x1B[1;34m%s\x1B[0m\n", token);
             token = strtok(NULL, CRNL);
         }
         free(verbs);
     }    
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
	if(print_verbs){
		char* verbs = calloc(1, strlen(buff)+1);
		strcpy(verbs,buff);
		char* token = strtok(verbs, "\r");
		sfwrite(&std_lock, stdout,"SENDING:   \x1B[1;34m%s\x1B[0m\n", token);
		free(verbs);
	}
}

void listu_protocol(int sockfd) {
	/* CREATE VERB "LISTU" TO SEND SERVER */
 	char* buff = (char*) calloc(1, strlen(LISTU) + strlen(PROTO_TERM) + 2);
 	Strcat2(buff, LISTU, PROTO_TERM);
 	/* SEND TO SERVER */
 	Send(sockfd, buff, strlen(buff), 0);

	/* RESET msg BUFFER */
 	memset(buff, 0, strlen(buff));
	/* RECEIVE "UTSIL" FROM SERVER */
 	buff = realloc(buff, 2049);
 	
 	/* RECEIVE UTSIL WITH USERNAMES */
 	recv(sockfd, buff, 2048, 0);
 	char* end = strstr(buff, PROTO_TERM);
 	buff[end-buff+4] = '\0';
 	//sfwrite(&std_lock, stdout, "%s\n", buff);
 	//buff = strtok(buff, PROTO_TERM);
 	char* verbs = calloc(1, strlen(buff)+1);
	strcpy(verbs,buff);
	char* token = strtok(verbs, " ");
	sfwrite(&std_lock, stdout,"RECEIVING: \x1B[1;34m%s\x1B[0m\n", token);
	token = strtok(NULL, CRNL);
	while(token != NULL){
	    sfwrite(&std_lock, stdout,"RECEIVING: \x1B[1;34m%s\x1B[0m\n", token);
	    token = strtok(NULL, CRNL);
	}
	free(verbs);
 	
 	//sfwrite(&std_lock, stdout, "RECEIVING FROM SERVER AS LISTU:\n%s", buff);

 	/* SEPERATE WHOLE USERNAME LIST FROM UTSIL AND PROTOCOL TERM */
 	//fprintf(stdout, "%s\n", buff);
 	char* username = strtok(buff, " ");
 	username = strtok(NULL, CRNL);
 	sfwrite(&std_lock, stdout, "+---------------------------------------+\n"
 		"|\t\tUsername\t\t|\n"
 		"+---------------------------------------+\n");
 	while(username != NULL){
 		sfwrite(&std_lock, stdout, "\t\t");
		while(*username != ' ') {
			sfwrite(&std_lock, stdout, "%c", *username);
			username++;
		}
		sfwrite(&std_lock, stdout, "\t\t\n"
			"+---------------------------------------+\n");

		username = strtok(NULL, CRNL);
		if(username != NULL)
			username++;
 	}
 	return;
}

void print_usage(){
	sfwrite(&std_lock,stdout, "./client [-h|-c|-v] [-a FILE] NAME SERVER_IP SERVER_PORT\n"
		"-a FILE\t\tPath to the audit log file.\n"
		"-h\t\tDisplays help menu & returns EXIT_SUCCESS.\n"
		"-c\t\tRequests to server to create a new user\n"
		"-v\t\tVerbose print all incoming and outgoing protocol verbs & content.\n"
		"NAME\t\tThis is the username to display when chatting.\n"
		"SERVER_IP\tThe ipaddress of the server to connect to.\n"
		"SERVER_PORT\tThe port to connect to.\n"
		);
}

void help_protocol(){
	sfwrite(&std_lock, stdout, "\n/time\t\t\tAsks the server how long it has been connected appearing\n\t\t\tin hours, minutes and seconds.\n\n"
		"/help\t\t\tShows all the commands which the client accepts and their\n\t\t\tfunctionality.\n\n"
		"/logout\t\t\tDisconnects client from server.\n\n"
		"/listu\t\t\tAsks the server to show the clients that are connected\n\t\t\tto the server.\n\n"
		"/chat <TO> <MSG>\tBegins a chat with <TO> if they are currently logged in\n\t\t\tby sending them <MSG>.\n\n"
		"/audit\t\t\tPrint the contents of the audit log.\n"
		);
}