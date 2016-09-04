#include "server.h"
#include "sfwrite.c"

int print_verbs;
pthread_mutex_t std_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t list_lock = PTHREAD_RWLOCK_INITIALIZER;
//pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;

char* MOTD;
char* PORT_NUMBER;
char* ACCOUNTS_FILE = "./accts.txt";
int THREAD_COUNT = 1;
User* users;

/* QUEUE FOR LOGIN REQUESTS */
int login_q[LOGIN_Q_SIZE];
int front = 0;
int end = 0;

/* MUTEX FOR LOGIN_Q */
pthread_mutex_t Q_lock = PTHREAD_MUTEX_INITIALIZER;
/* SEMAPHORE FOR LOGIN_Q */
sem_t items_sem;

// The structure for two events
struct pollfd fds[2];
struct pollfd fds_comm[200];
int nfds;
int client_fd, socket_fd, disconnected;

char** logging_users;
int logging_in = 0;

void insert_fd(int fd){
	login_q[(++end)%(LOGIN_Q_SIZE)] = fd;
}

int remove_fd(){
	return login_q[(++front)%(LOGIN_Q_SIZE)];
}

int main(int argc, char** argv){

	signal(SIGINT, shutdown_handler);
	//signal(SIGKILL, shutdown_handler);

 	/* STRING TO STORE COMMANDS FROM STDIN LATER */
	char* commands = (char*) calloc(1, sizeof(char*) + 1);
	int bad_args = 0;
 	/* LIST OF USER ACCOUNTS */
	users = NULL;
	read_file();
 	/* COUNT OF FDS FOR COMMUNICATION THREAD LATER */
	nfds = 0;

	/* VALIDATE & INITIALIZE SERVER ARGUMENTS */
	if((bad_args = init_args(argc, argv))){
    	if(bad_args == 1){ /* ARGS WERE GOOD, -h WILL EXIT_SUCCESS */
			return EXIT_SUCCESS;
		}
    	else{ /* ARGS WERE BAD. EXIT_FAILURE */
			return EXIT_FAILURE;
		}
	}

	logging_users = calloc(1, 200);

	/* START SOCKET STUFF FOR ACCEPT THREAD aka main() */

	/* CREATE SOCKET TO LISTEN FOR CONNECTION REQUESTS ON */
	int result;

	struct addrinfo hints, *res;
	char hostname[_POSIX_HOST_NAME_MAX];
	hostname[_POSIX_HOST_NAME_MAX - 1] = '\0';
	gethostname(hostname, _POSIX_HOST_NAME_MAX - 1);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	struct ifaddrs *address, *tmp;
	getifaddrs(&address);
	tmp = address;

	struct sockaddr_in *pAddr;


	while (tmp){
		if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_INET){
			pAddr = (struct sockaddr_in *)tmp->ifa_addr;
			sfwrite(&std_lock, stdout, "%s: %s\n", tmp->ifa_name, inet_ntoa(pAddr->sin_addr));
		}

		tmp = tmp->ifa_next;
	}


	freeifaddrs(address);

	if ((result = getaddrinfo(inet_ntoa(pAddr->sin_addr), PORT_NUMBER, &hints, &res))) {
		sfwrite(&std_lock, stderr, "\x1B[1;31mgetaddrinfo: %s\x1B[0m\n", gai_strerror(result));
		exit(1);
	}

	struct addrinfo *a;
	for(a = res; a; a = a->ai_next) {
		if((socket_fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol)) < 0) {
			continue;
		}
		break;
	}

	/* BIND SOCKET TO SPECIFIC PORT AND IP */
	Bind(socket_fd, a->ai_addr, a->ai_addrlen);

	/* SOME INT THAT THE PROF USED */
	int optval = 1;

	/* MAKE SOCKET REUSABLE */
	Setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));

	/* LISTEN TO SOCKET FOR A CONNECTION */
	Listen(socket_fd, 128);

	/* CREATE VARIABLES TO STORE RETURN OF POLL AND CLIENT SOCKET FD. THREAD ID TOO, I GUESS */
	int ret;
	pthread_t tid;
	print_prompt();

	/* INITIALIZE THE SEMAPHORE */
	sem_init(&items_sem, 0, 0);

	/* CREATE LOGIN THREAD(S) */
	while(THREAD_COUNT--){
		/* SPAWN LOGIN THREAD */
		pthread_create(&tid, NULL, login_thread, &users);
		/* NAME LOGIN THREAD */
		pthread_setname_np(tid, LOGIN_THREAD);
	}

	/* WHILE LOOP TO CONTINUOUSLY LISTEN FOR CONNECTION REQUESTS AND SERVER COMMANDS ON STDIN */
	while(1){
		/* MONITOR STDIN FOR SERVER COMMANDS */
		fds[0].fd = STDIN_FILENO;
		fds[0].events = POLLIN;

		/* MONITOR SERVER SOCKET FOR CONNECTION REQUESTS */
		fds[1].fd = socket_fd;
		fds[1].events = POLLIN;

		/* WAIT 1 SECONDS FOR SOME REASON */
		ret = poll(fds, 2, 1000);

		if(ret == -1){
			/* POLL ERROR */
		}
		else if(ret == 0){
			/* TIMED OUT */
		}
		else{
			/* CHECK FOR INPUT ON STDIN */
			if(fds[0].revents & POLLIN){
				compare(commands, socket_fd);
				print_prompt();
				fds[0].revents = 0;
			}

			/* CHECK FOR CONNECTION REQUESTS ON SERVER SOCKERT */
			if(fds[1].revents & POLLIN){
				/* ACCEPT THE CONNECTION */
				client_fd = Accept(socket_fd, a->ai_addr, &(a->ai_addrlen));
				/* LOCK MUTEX ON LOGIN_Q */
				pthread_mutex_lock(&Q_lock);
				/* ADD NEW CONNECTION TO LOGIN_Q */
				insert_fd(client_fd);
				/* UNLOCK */
				pthread_mutex_unlock(&Q_lock);
				/* WAKE UP ANY OTHER THREADS WAITING FOR AN AVAILABLE ITEM IN SEMAPHORE */
				sem_post(&items_sem);
				/* RESET EVENT LISTENER THING */
				fds[1].revents = 0;
			}
		}
	}
	freeaddrinfo(res);
	return 0;
}

void invalid_args(){
	sfwrite(&std_lock, stderr, "\x1B[1;31mError: Invalid args\x1B[0m\n");
	print_usage();
}

int init_args(int argc, char**argv){
	int opt, hcount,vcount, tcount, i;
	hcount = vcount = tcount = i = 0;

	/* THERE SHOULD BE AT LEAST 3 ARGS: ./server, PORT_NUMBER, MOTD */
	if(argc < 3){
		invalid_args();
		return -1;
	}

	/* COUNT THE NUMBER OF OPTIONAL ARGS THERE ARE */
	while((opt = getopt(argc, argv, "hvt")) != -1) {
		switch(opt) {
			sfwrite(&std_lock, stdout, "%d\n\n", argc);
			case 'h':
				hcount++;
				break;
			case 'v':
				vcount++;
				break;
			case 't':
				if(!(THREAD_COUNT = atoi(argv[optind]))){
					invalid_args();
					return -1;
				}
				tcount++;
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
	if(vcount > 1 || tcount > 1){
		invalid_args();
		return -1;
	}

	if(vcount)
		print_verbs++;

	/* EDIT OPTIND TO REFLECT -t SECONDARY ARGUMENT */
	if(atoi(argv[optind]) == THREAD_COUNT)
		optind++;

	/* INIT PORT NUMBER */
	PORT_NUMBER = Strcpy(argv[optind++]);
	/* CHECK IF PORT NUMBER IS A NUMBER */
	if(!atoi(PORT_NUMBER)){
		invalid_args();
		return -1;
	}

	/* INIT MESSAGE OF THE DAY */
	MOTD = Strcpy(argv[optind++]);
	sfwrite(&std_lock, stdout, "PORT_NUMBER: %s\r\nMOTD: %s\r\n", PORT_NUMBER, MOTD);

	/* EDIT OPTIND TO REFLECT -t SECONDARY ARGUMENT */
	if(optind < argc && atoi(argv[optind]))
		optind++;
	
	/* INIT ACCOUNT_FILE IF THERE IS ONE */
	if(optind < argc){
		ACCOUNTS_FILE = Strcpy(argv[optind++]);
	}
    sfwrite(&std_lock, stdout,"ACCOUNTS_FILE: %s\nTHREAD_COUNT = %d\n", ACCOUNTS_FILE, THREAD_COUNT);

	return 0;
}

void create_file() {
	FILE *user_file = fopen(ACCOUNTS_FILE, "w");
	if(user_file == NULL) {
		sfwrite(&std_lock, stdout, "%s\n", "Unable to save ACCOUNTS_FILE");
		return;
	}
	pthread_rwlock_rdlock(&list_lock);
	User* temp = users;
	// int i;
	while(temp != NULL) {
		// for(i = 0; i < strlen(temp->username))
		sfwrite(&std_lock, user_file, "%s\n", temp->username);
		// fwrite(users->password, hash_size, 1, user_file);
		sfwrite(&std_lock, user_file, "%s\n", temp->password);
	    sfwrite(&std_lock, user_file, "%s", temp->salt);
		if(temp->next != NULL)
			sfwrite(&std_lock, user_file, "\n");
		//else sfwrite(&std_lock, user_file, "***");
		temp = temp->next;
	}
	pthread_rwlock_unlock(&list_lock);
	fclose(user_file);
}

void read_file() {
	/* OPEN ACCTS FILE */
	FILE *user_file = fopen(ACCOUNTS_FILE, "r");
	
	/* CHECK IF FILE WAS OPENED CORRECTLY */
	if(user_file == NULL){
		return;
		sfwrite(&std_lock, stderr, "ERROR\n");
	}
	
	/* CREATE */
	pthread_rwlock_rdlock(&list_lock);
	users = calloc(1, sizeof(User));
	User* temp = users;
	temp->clientfd = -1;

	/* VARUABLES FOR LOOPS AND STUFF */
	int i;
	char* buff = NULL;
	char *pos;
	size_t len = 0;
	ssize_t read;

	/* MAKE EMPTY SHELL FOR USER LIST */
	for(i = 0; i < 20; i++){
		temp->next = calloc(1, sizeof(User));
		temp->next->clientfd = -2;
		temp = temp->next;
	}
	temp = users;

	/* KEEP READING LINE FROM FILE AS LONG AS THERE IS ONE */
	 while((read = getline(&buff, &len, user_file)) != -1){
	 	/* REPLACE THE NEW LINE CHARACTER */
	 	if ((pos=strchr(buff, '\n')) != NULL)
    		*pos = '\0';
    	/* STORE IN USERNAME */
	 	temp->username = Strcpy(buff);

	 	/* READ IN PASSWORD AND REPLACE NEW LINE */
	 	read = getline(&buff, &len, user_file);
	 	if ((pos=strchr(buff, '\n')) != NULL)
    		*pos = '\0';
	 	temp->password = Strcpy(buff);

	 	/* READ IN SALT AND REPLACE NEW LINE */
	 	read = getline(&buff, &len, user_file);
	 	if ((pos=strchr(buff, '\n')) != NULL)
    		*pos = '\0';
	 	temp->salt = Strcpy(buff);

	 	/* SET CLIENTFD TO -1 AS MARKER TO KEEP THIS USER IN LIST */
	 	temp->clientfd = -1;
	 	/* GO TO NEXT SHELL OF USER */
	 	temp = temp->next;
    }
    /* DONE WITH BUFF */
    free(buff);

    /* CHECK IF FILE WAS EMPTY */
    if(users->clientfd==-2){
    	free(users);
    	users = NULL;
    	return;
    }
    /* REMOVE EXTRA USERS FROM LIST */
	temp = users;
	for(i = 0; i < 20; i++){
		if(temp->next->clientfd == -2){
			temp->next = NULL;
			break;
		}
		else temp = temp->next;
	}
	pthread_rwlock_unlock(&list_lock);
	/* CLOSE FILE */
	fclose(user_file);
}

void error_protocol(int sockfd, int err, char* name){
	if(name != NULL){
		remove_frm_logging(name);
	}
	/* SEND APPROPRIATE ERROR AND REMOVE FROM USER LIST (ALWAYS AT THE END) */
	if(err == 0){
		Send(sockfd, "ERR 00 USER NAME TAKEN \r\n\r\n", 27, 0);
	}
	else if(err == 1){
		Send(sockfd, "ERR 01 USER NOT AVAILABLE \r\n\r\n", 30, 0);
	}
	else if(err == 2){
		Send(sockfd, "ERR 02 BAD PASSWORD \r\n\r\n", 24, 0);
	}
	/* SEND BYE MESSAGE */
	Send(sockfd, "BYE \r\n\r\n", 8, 0);
	/* RECEIVE BYE MESSAGE */
	// char* buff = calloc(1, 9);
	// Recv(sockfd, buff, 8, 0);
	// if(strcmp(buff,"BYE \r\n\r\n")){
	// 	close(sockfd);
	// 	perror("\x1B[1;31mnot adherent to protocol\x1B[0m");
	// 	exit(EXIT_FAILURE);
	// }
	close(sockfd);
	/* REMOVE LAST USER FROM USER LIST (IT WAS JUST A TEMP)*/
	//remove_user();
	
}

void hi_protocol(int sockfd, char* name, int add_new, char* pass){
	pthread_rwlock_wrlock(&list_lock);
	/* FIND USER */
	User* new_user = users;
	/* IF NEW USER, LOCATED AT END OF LIST */
	if(add_new){
		add_user(sockfd, name, pass);
	}
	else{
		/* OLD USER LOGGING IN. FIND THEM IN LIST */
		while(strcmp(new_user->username, name)){
			new_user = new_user->next;
		}
		/* UPDATE USER ACCOUNT */
		new_user->loginT = time(NULL);
		new_user->clientfd = sockfd;
		new_user->active = ACTIVE;
	}
	pthread_rwlock_unlock(&list_lock);
	remove_frm_logging(name);
	/* CONCATONATE STRING TO SEND - "HI <NAME> \R\N\R\N" */
	char* msg = (char*)calloc(1, strlen(name)+strlen("HI  \r\n\r\n")+1);
	strcat(msg, "HI ");
	strcat(msg, name);
	strcat(msg, " \r\n\r\n");
	Send(sockfd, msg, strlen(msg), 0);
	/* CONCATONATE STRING TO SEND - "MOTD <MOTD> \R\N\R\N" */
	memset(msg, 0, strlen(msg));
	msg = realloc(msg, strlen(MOTD) + strlen("MOTD  \r\n\r\n")+1);
	strcat(msg, "MOTD ");
	strcat(msg, MOTD);
	strcat(msg, " \r\n\r\n");
	Send(sockfd, msg, strlen(msg), 0);
	free(msg);
}

void hi_new_protocol(int sockfd, char* name) {
	
	/* CONCATONATE STRING TO SEND - "HINEW <NAME> \R\N\R\N" */
	char* msg = (char*)calloc(1, strlen(name)+strlen("HINEW  \r\n\r\n")+1);
	strcat(msg, "HINEW ");
	strcat(msg, name);
	strcat(msg, " \r\n\r\n");
	Send(sockfd, msg, strlen(msg), 0);
	free(msg);
}


void add_user(int sockfd, char* name, char* password){
	//pthread_mutex_lock(&list_lock);
	if(users == NULL){
		/* INIT EMPTY USER LIST */
		users = (User*)calloc(1, sizeof(User));
		users->next = NULL;
		users->active = ACTIVE;
		users->clientfd = sockfd;
		users->loginT = time(NULL);
		users->username = calloc(1, strlen(name)+1);
		/* CREATE AND SAVE SALT */
 		unsigned char salt[32];
 		RAND_bytes(salt, 32);
 		salt[31] = '\0';
 		users->salt = Strcpy((char*)salt);
 		const char* hashed = hash_password((char*)salt, password);

 		/* APPEND SALT TO PASSWORD AND SAVE HASH */ 
 		users->password = calloc(1, strlen(hashed)+ 1);
 		users->password = (char*)hashed;
 		strcpy(users->username, name);

	}
	else{
 		/* CREATE TEMP TO TRAVERSE LIST TO ADD USER TO END */
		User* temp = users;
		while(temp->next != NULL){
			temp = temp->next;
		}
		User* new = (User*)calloc(1, sizeof(User));
		new->next = NULL;
		new->active = ACTIVE;
		new->clientfd = sockfd;
		new->loginT = time(NULL);
		new->username = calloc(1, strlen(name)+1);
		 
		unsigned char salt[32];
 		RAND_bytes(salt, 32);
 		salt[31] = '\0';
 		new->salt = Strcpy((char*)salt);
 		const char* hashed = hash_password((char*)salt, password);

 		/* APPEND SALT TO PASSWORD AND SAVE HASH */
 		new->password = calloc(1, strlen(hashed)+ 1);
 		new->password = (char*)hashed;
		strcpy(new->username, name);
		temp->next = new;
	}
	//pthread_mutex_unlock(&list_lock);
}

int passCheck(char* name, int clientfd, int new) {
 	/* IF EXISTING USER */
	if(!new) {
 		/* MESSAGE SENT TO CLIENT ASKING FOR PASSWORD */
		char* msg = (char*)calloc(1, strlen("AUTH  \r\n\r\n") + strlen(name) + 1);
		Strcat3(msg, AUTH, name, PROTO_TERM);
		int seq = 0;
		Send(clientfd, msg, strlen(msg), 0);
 		/* SERVER RECEIVING PASSWORD FROM CLIENT */
		memset(msg, 0, strlen(msg));
 		/* ALLOCATE MEMORY FOR THE RECEIVED PASSWORD */
		char* pass = (char*)calloc(1, 1025);
 		/* REALLOCATE MEMORY */
		msg = realloc(msg, 1025);
		Recv(clientfd, msg, 0);
		pass = parse_args(msg, &seq);

 		/* TRAVERSE LIST LOOKING FOR USERNAME TO COMPARE PASSWORDS */
		pthread_rwlock_rdlock(&list_lock);
		User* tmp = users;
		while(tmp != NULL){
			if(!strcmp(tmp->username,name))
				break;
			tmp = tmp->next;
		}
 		/* CHECK IF PASSWORD IS CORRECT. IF WRONG, ERR2. ELSE, SEND HI AND MOTD. */
		char *salt = tmp->salt;
 		const char* hashed = hash_password((char*)salt, pass);
 		if(strcmp(tmp->password, hashed) || seq!=2){
 			pthread_rwlock_unlock(&list_lock);
 			return -1;
 		}
 		pthread_rwlock_unlock(&list_lock);
 		/* PASSWORD WAS A MATCH */
		Send(clientfd, "SSAP \r\n\r\n", 9, 0);
		hi_protocol(clientfd, name, 0, pass);
		free(msg);
		return 0;
	}
 	/* IF NEW USER */ 
	else {
 		/* RECEIVE NEW PASSWORD FROM CLIENT */
		char* msg = (char*)calloc(1, 1025);
		Recv(clientfd, msg, 0);
		int seq;
		char* pass = parse_args(msg, &seq);
		if(seq != 4){
			error_protocol(clientfd, 1, name);
			return -1;
		}
		if(validate_password(pass)){
			return -1;
		}
 		/* SEND CLIENT SSAPWEN, SAVE PASSWORD, SAY HI */
		Send(clientfd, "SSAPWEN \r\n\r\n", 12, 0);
		hi_protocol(clientfd,name,1,pass);
 		/* SEND CLIENT HI */
		memset(msg, 0, 1025);
		free(msg);
		return 0;
	}
}

int validate_password(char* pass){
	int i;
	int upper, symbol, number;
	upper = symbol = number = 0;
	if(strlen(pass) < 5)
		return -1;
	for(i = 0; i < strlen(pass); i++) {
		if(pass[i] >= 33 && pass[i] <= 47)
			symbol++;
		else if(pass[i] >= 48 && pass[i] <= 57)
			number++;
		else if(pass[i] >= 58 && pass[i] <= 64)
			symbol++;
		else if(pass[i] >= 65 && pass[i] <= 90)
			upper++;
		else if(pass[i] >= 91 && pass[i] <= 96)
			symbol++;
		else if(pass[i] >= 123 && pass[i] <= 126)
			symbol++;
	}
	if(upper && symbol && number)
		return 0;
	return -1;
}

void *login_thread(void *vargp){

	/* DETACH LOGIN THREAD FROM MAIN PROCESS */
	pthread_detach(pthread_self());
	
	/* BEGIN LOGIN THREAD STUFF */
	while(1){
		
		/* RETRIEVE NEXT AVAILABLE LOGIN REQUEST FROM LOGIN_Q */
		sem_wait(&items_sem);
		pthread_mutex_lock(&Q_lock);
		int connfd = remove_fd();
		pthread_mutex_unlock(&Q_lock);

		/* ATTEMPT TO LOGIN */
		int ret = wolfie_protocol(connfd);
		/* IF LOGIN FAILED, RESTART LOGIN THREAD */
		if(ret){
			continue;
		}

	 	/* ADD NEW SOCKET TO FDS SET */
	 	pthread_rwlock_wrlock(&list_lock);
		fds_comm[nfds].fd = connfd;
		fds_comm[nfds].events = POLLIN;
		/* INCREMENT FDS COUNT */
		nfds++;
		pthread_rwlock_unlock(&list_lock);

		/* SPAWN COMM THREAD IF NOT ALREADY CREATED */
		if(nfds==1){
			int i;
	 		/* CLEAR THE SET */
			for(i=nfds;i<MAX_FDS;i++)
				fds_comm[i].fd = -1;
	 		/* THREAD ID */
			pthread_t tid;
		 	/* SPAWN COMMUNICATION THREAD. PASS NAME */
			pthread_create(&tid, NULL, comm_thread, NULL);
			/* NAME COMMUNICATION THREAD */
			pthread_setname_np(tid, COMM_THREAD);
		}
	}
	return NULL;
}

void *comm_thread(void* vargp){

	/* DETACH THREAD */
	pthread_detach(pthread_self());
	/* ALLOCATE MEMORY FOR MESSAGES FROM CLIENTS */
	char* msg = calloc(1, BUFF_SIZE+1);

	int ret, i;
	ret = i = 0;

	/* TIME TO START COMM THREAD */
	while(1){

		/* IF NO CLIENTS ONLINE, TERMINATE THREAD */
		if(nfds == 0){
			free(msg);
			return NULL;
		}
		/* POLL */
		ret = poll(fds_comm, nfds+1, 5000);
		if(ret == -1){
			/* POLL ERROR */
			sfwrite(&std_lock, stdout,"POLL ERROR\n");
		}
		else if(ret == 0){
			/* TIMED OUT */
			//sfwrite(&std_lock, stdout,"POLL TIMED OUT\n");
		}
		else{
			/* LOOP THROUGH FDS SET */
			for(i = 0; i < nfds; i++){
  		 		/* IF NEGATIVE SKIP */
				if(fds_comm[i].fd < 0)
					continue;
	  			/* CHECK FOR INPUT ON CLIENT SOCKET */
				if(fds_comm[i].revents & POLLIN){
					/* RECEIVE FROM ACTIVE SOCKET */
					fds_comm[i].revents = 0;
					Recv(fds_comm[i].fd, msg, 0);
					/* FIND OUT WHAT THEY WANT */
					commands(msg, fds_comm[i].fd);
					/* MAY BE WEIRD ERROR AFTER AWHILE IF MSG KEEPS GETTING SMALLER */
					memset(msg, 0, BUFF_SIZE);
				}
			}
		}
	}
	return NULL;

}


void time_protocol(int sockfd){
 	/* TMP TO FIND USER WITH THAT SOCKET */
 	pthread_rwlock_rdlock(&list_lock);
	User* tmp = users;
  	/* STORE LOGIN TIME */
	time_t login_time;
  	/* FIND USER */
	while(tmp!=NULL){
		if(tmp->clientfd == sockfd)
			login_time = tmp->loginT;
		tmp = tmp->next;
	}
	pthread_rwlock_unlock(&list_lock);
  	/* CONVERT TIME TO A DOUBLE */
	double actual_loginT = difftime(time(NULL),login_time);
  	/* STRING TO STORE MESSAGE */
	char* msg = calloc(1, 30);
	char* time_s = calloc(1,30);
  	/* CREATE MESSAGE */
	strcat(msg, EMIT);
	sprintf(time_s, " %d ", (int)actual_loginT);
	Strcat2(msg, time_s, PROTO_TERM);
  	/* SEND MESSAGE */
	Send(sockfd, msg, strlen(msg), 0);
	free(msg);
	free(time_s);
}

void logout_protocol(int sockfd){
  	/* LOCK LIST FOR WRITING */
  	pthread_rwlock_wrlock(&list_lock);
  	/* FIND USER WHO WANTS TO LOGOFF */
	User* client = users;
	char* name = calloc(1, BUFF_SIZE);
	char* name_off;
  	/* SET ACCOUNT TO INACTIVE AND SAVE USERNAME */
	while(client != NULL){
		if(client->clientfd == sockfd){
			client->active = INACTIVE;
			client->clientfd = -1;
			name = strcpy(name, client->username);
			name_off = calloc(1, strlen(name));
			strcpy(name_off, client->username);
			break;
		}
		client = client->next;
	}
	/* REMOVE CONNECTION FROM FD SET AND SHIFT */
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
	/* IF CLIENT WAS DISCONNECTED DONT SEND THEM BYE */
	if(!disconnected){
		memset(name,0,strlen(name));
		Strcat2(name, BYE, PROTO_TERM);
		/* SEND BYE TO CLIENT LOGGING OFF */
		Send(sockfd, name, strlen(name), 0);
	}
	/* NOW SEND UOFF TO ALL ONLINE CLIENTS ABOUT THE LEAVING CLIENT */
	memset(name,0,strlen(name));
	client = users;
	Strcat3(name, UOFF, name_off, PROTO_TERM);
	while(client != NULL) {
		if(client->active == ACTIVE)
			Send(client->clientfd, name, strlen(name),0);
		client = client->next;
	}
	pthread_rwlock_unlock(&list_lock);
}

int check_online(char* username){
	pthread_rwlock_rdlock(&list_lock);
	sfwrite(&std_lock, stdout, "LOCKED CHECKING ONLINE\n");
	User* temp = users;
	int fd = -1;
	while(temp != NULL){
		if(strcmp(username, temp->username))
			temp = temp->next;
		else{ 
			if(temp->active){
				fd = temp->clientfd;
				break;
			}
		}
	}
	sfwrite(&std_lock, stdout, "UNLOCKING CHECK ONLINE\n");
	pthread_rwlock_unlock(&list_lock);
	return fd;
}

int msg_protocol(char* msg){

	/* COPY MESSAGE SO NOTHING IS LOST WHEN STRTOK */
	char* copy = Strcpy(msg);
	char* token = strtok(copy, " ");

	/* IF THERE WERE NO SPACES THEN BAD PROTOCOL */
	if(token == NULL)
		return 1;
	/* IF FIRST TOKEN IS NOT "MSG" BAD PROTOCOL */
	if(strcmp(token, MSG))
		return 1;

	int chat_fds[2];
	int i;

	/* FIND BOTH NAMES */
	for(i = 0; i < 3; i++){
		token = strtok(NULL, " ");
		if(token == NULL)
			return 1;
		/* STORE OTHER NAME */
		if(i != 2){
			/* CHECK IF BOTH USERS ARE ONLINE AND GET THEIR SOCKETS */
			sfwrite(&std_lock, stdout, "CHECKING IF ONLINE\n");
			if((chat_fds[i]=check_online(token)) == -1){
				return -1;
			}
		}
	}
	if(token == NULL)
		return -1;
	if(!strcmp(" ", token))
		return -1;

 	/* SEND THE MSG TO BOTH CLIENTS */
	for(i = 0; i < 2; i++)
		Send(chat_fds[i], msg, strlen(msg), 0);

 	/* FREE MEMORY */
	free(copy);

	return 0;

}

int commands(char* command,int sockfd) {
	
	/* CHECK IF CLIENT SENT "TIME" VERB */
	char* cmp = calloc(1, BUFF_SIZE);
	Strcat2(cmp, TIME, PROTO_TERM);
	if(!strcmp(command, cmp)) {
		time_protocol(sockfd);
		return 0;
	}
	memset(cmp, 0, strlen(cmp));
	
	/* CHECK IF CLIENT SENT "LISTU" TO RECEIVE ONLINE CLIENTS */	
	Strcat2(cmp, LISTU, PROTO_TERM);
	if(!strcmp(command, cmp)) {
		listu_protocol(sockfd);
		return 0;
	}
	memset(cmp, 0, strlen(cmp));
	
	/* CHECK IF CLIENT SENT "BYE" TO LOGOUT */
	Strcat2(cmp, BYE, PROTO_TERM);
	if(!strcmp(command, cmp)) {
		logout_protocol(sockfd);
		close(sockfd);
		return 0;
	}

	/* CHECK IF CLIENT SENT "MSG <TO> <FROM> <MESSAGE> " */
	if(strstr(command, MSG) && strstr(command, MSG) == command){
		if(msg_protocol(command) == 1){
			sfwrite(&std_lock, stderr, "BAD PROTOCOL\n");
			return -1;
		}
	}
	return 0;
}


const char* hash_password(char salt[], char* password){
	const unsigned char* pre_hash = calloc(1, strlen(password) + 32 + 1);
	Strcat2((char*)pre_hash, (char*)salt, password);
	const char* hashed = calloc(1,strlen((char*)pre_hash));
	SHA256(pre_hash, strlen((char*)pre_hash), (unsigned char*)hashed);
 	return hashed;
return NULL;

}

void listu_protocol(int sockfd) {
	pthread_rwlock_rdlock(&list_lock);
	User* tmp = users;
	char* buff = (char*) calloc(1, 2048);
	Strcat(buff, UTSIL);
	if(tmp == NULL) {
		pthread_rwlock_unlock(&list_lock);
		return;
	}
	while(tmp != NULL) {
		if(tmp->active == 1)
			Strcat3(buff, tmp->username, CRNL, "");
		tmp = tmp->next;
	}
	pthread_rwlock_unlock(&list_lock);

	buff[strlen(buff) - 1] = '\0';
	strcat(buff, CRNL);
	//sfwrite(&std_lock, stdout, "SENDING AS LISTU TO CLIENT:\n%s", buff);
	Send(sockfd, buff, strlen(buff), 1);
}

void print_users() {
	pthread_rwlock_rdlock(&list_lock);
	User* temp = users;
	int num_online = 0;
	while(temp != NULL) {
		if(temp->active){
    		/* FIRST TIME PRINTING. PRINT TABLE HEADER */
			if(!num_online){
    			//sfwrite(&std_lock, stdout, "+--------------+---------------+---------------+\n");
				sfwrite(&std_lock, stdout, "USER # | USERNAME | SOCKET #\n\n");
			}
			sfwrite(&std_lock, stdout, "USER %d | %s | %d\n", ++num_online, temp->username, temp->clientfd);
		}
		temp = temp->next;
	}
	if(!num_online)
		sfwrite(&std_lock, stdout, "No users currently online.\n");
	pthread_rwlock_unlock(&list_lock);
}

void print_accounts() {
	pthread_rwlock_rdlock(&list_lock);
	User* temp = users;
	int num_accts = 0;
	while(temp != NULL) {
		if(!num_accts){
			sfwrite(&std_lock, stdout, "USER # | USERNAME | SOCKET # | STATUS\n\n");
		}
		if(temp->active){
			sfwrite(&std_lock, stdout, "USER %d | %s | %d | %s\n", ++num_accts, temp->username, temp->clientfd, ONLINE);
		}
		else sfwrite(&std_lock, stdout, "USER %d | %s | %d | %s\n", ++num_accts, temp->username, temp->clientfd, OFFLINE);
		temp = temp->next;
	}
	if(!num_accts)
		sfwrite(&std_lock, stdout, "No user accounts have been created yet.\n");
	pthread_rwlock_unlock(&list_lock);
}

void shutdown_handler(int sig_num){
	sfwrite(&std_lock, stdout, "\n");
	shutdown_server();
}

int shutdown_server(){

	sfwrite(&std_lock, stdout, "Server shutting down...\n");
	/* SAVE ACCOUNTS IN FILE */
	create_file();

	/* SAVE ACCOUNT INFORMATION */

	/* SEND BYE TO USER. FDS SAVED IN FDS_COMM */
	char* bye = calloc(1, strlen(BYE) + strlen(PROTO_TERM) + 2);
	Strcat2(bye, BYE, PROTO_TERM);

	/* FREE, BID TIDINGS, CLOSE SOCKETS */
	pthread_rwlock_wrlock(&list_lock);
	User* temp = users;
	while(temp != NULL){
		/* FREE USERNAME */
		free(temp->username);
		/* FREE PASSWORD */
		free(temp->password);
		/* SEND "BYE PROTO_TERM" TO CLIENT IF ONLINE*/
		if(temp->active){
			Send(temp->clientfd, bye, strlen(bye), 0);
			usleep(1500000);
		}
		/* CLOSE CLIENT SOCKET */
		close(temp->clientfd);
		/* CREATE TEMP2 TO STORE PTR TO THIS USER */
		User* temp2 = temp;
		/* SET TEMP TO NEXT USER IN LIST */
		temp = temp->next;
		/* FREE PREV USER IN LIST */
		free(temp2);
	}

	/* PERHAPS "RECV" AFTER SEND AND THEN CLOSE SOCKET SO WE KNOW */
	/* FREE PORT_NUMBER & MOTD */
	free_args();
	/* CLOSE SERVER SOCKET */
	//shutdown(socket_fd,SHUT_RDWR);
	close(socket_fd);
	// /* SEND BYE TO EVERY USER. FDS SAVED IN FDS_COMM */
	// char* bye = calloc(1, strlen(BYE) + strlen(PROTO_TERM) + 2);
	// Strcat2(bye, BYE, PROTO_TERM);
	// int i;
	// /* SENDING BYE AND CLOSING SOCKET */
	// for(i = 0; i < nfds; i++){
	// 	Send(fds_comm[i].fd, bye, strlen(bye), 0);
	// 	close(fds_comm[i].fd);
	// }
	sfwrite(&std_lock, stdout, "Server shutdown\n");
	pthread_rwlock_unlock(&list_lock);
	exit(EXIT_SUCCESS);
}


void free_args(){
	free(PORT_NUMBER);
	free(MOTD);
}

int compare(char* commands, int socket_fd) {
	int ch;
	int i = 0;
	while((ch = fgetc(stdin)) != EOF) {
		/* IF CHAR IS SPACE */
		if(ch == 32) {
			continue;
		} else if(ch == 10) {
			commands[i] = '\n';
			commands[i+1] = '\0';
			break;
		} else {
			commands[i] = (char) ch;
			i++;
		}
	}
	if(!strcmp(commands, USERS_CMD)) {
		print_users();
		return 0;
	} 
	else if(!strcmp(commands, ACCTS_CMD)){
		print_accounts();
		return 0;
	}
	else if(!strcmp(commands, HELP_CMD)) {
		help_protocol();
		return 0;
	} else if(!strcmp(commands, SHUT_CMD)) {
		shutdown_server();
	} 
	else if(!strcmp(commands, "\n")){
		return 0;
	}
	else{
		sfwrite(&std_lock, stdout, "Invalid command: %sThe following are valid server commands:\n", commands);
		help_protocol();
	}
	return -1;
}

void print_prompt(){
	sfwrite(&std_lock, stdout, "\x1B[1;36mKRINC server cmd>\x1B[0m ");
	fflush(stdout);
}

/*
 * parse_args function
 * - Parses the string, args, to decide if it adheres to
 *   the rules and regulations of WOLFIE protocol.
 * - Alters the value, seq, to inform the parent function
 *   which sequence the WOLFIE protocol verbs are following.
 * 
 *   The following are possible values to store at seq:
 * 1 - IAM <name> \r\n\r\n
 * 2 - PASS <password> \r\n\r\n
 * 3 - IAMNEW <name> \r\n\r\n
 * 4 - NEWPASS <password> \r\n\r\n

 * @return char* - returns the value of the string
 *                 e.g. <name> or <password>
 */
char* parse_args(char* args, int* seq){

	/* PTRS TO DETERMINE IF ADHERING TO WOLFIE PROTOCOL AND TO SAVE NAME/PASSWORD */
 	char *tmp, *str, *value;
 	value = NULL;
	/* CHECK IF IT COULD BE IAM  (NOT IAMNEW) */
 	if((str=strstr(args, "IAM ")) != NULL){
		/* IAM SHOULD BE IN BEGINNING OF THE STRING */
 		if(str == args){
 			str += 4;
			/* FIND THE NEXT&LAST SPACE */
 			tmp = strstr(str, " ");
			/* CHECK IF PROTOCOL TERMINATOR IS PRESENT AFTER SPACE */
 			if(*(tmp+1) == '\r'){
				/* SAVE THE VALUE (NAME OR PASSWORD) */
 				value = calloc(1, tmp-str+1);
 				strncpy(value, str, tmp-str);
				/* FINALLY, CHECK FOR WHOLE PROTOCOL TERMINATOR */
 				if((str=strstr(tmp, "\r\n\r\n")) != NULL){
					/* SET SEQUENCE TO 1 AND RETURN VALUE */
 					*seq = 1;
 					return value;
 				}
 			}
 		}
 	}
 	else if((str=strstr(args, "IAMNEW ")) != NULL){
		/* IAM SHOULD BE IN BEGINNING OF THE STRING */
 		if(str == args){
 			str += 7;
			/* FIND THE NEXT&LAST SPACE */
 			tmp = strstr(str, " ");
			/* CHECK IF PROTOCOL TERMINATOR IS PRESENT AFTER SPACE */
 			if(*(tmp+1) == '\r'){
				/* SAVE THE VALUE (NAME OR PASSWORD) */
 				value = calloc(1, tmp-str+1);
 				strncpy(value, str, tmp-str);
				/* FINALLY, CHECK FOR WHOLE PROTOCOL TERMINATOR */
 				if((str=strstr(tmp, "\r\n\r\n")) != NULL){
					/* SET SEQUENCE TO 1 AND RETURN VALUE */
 					*seq = 3;
 					return value;
 				}
 			}
 		}
 	}
 	else if((str=strstr(args, "NEWPASS ")) != NULL){
		/* IAM SHOULD BE IN BEGINNING OF THE STRING */
 		if(str == args){
 			str += 8;
			/* FIND THE NEXT&LAST SPACE */
 			tmp = strstr(str, " ");
			/* CHECK IF PROTOCOL TERMINATOR IS PRESENT AFTER SPACE */
 			if(*(tmp+1) == '\r'){
				/* SAVE THE VALUE (NAME OR PASSWORD) */
 				value = calloc(1, tmp-str+1);
 				strncpy(value, str, tmp-str);
				/* FINALLY, CHECK FOR WHOLE PROTOCOL TERMINATOR */
 				if((str=strstr(tmp, "\r\n\r\n")) != NULL){
					/* SET SEQUENCE TO 1 AND RETURN VALUE */
 					*seq = 4;
 					return value;
 				}
 			}
 		}
 	}
 	else if((str=strstr(args, "PASS ")) != NULL){
		/* IAM SHOULD BE IN BEGINNING OF THE STRING */
 		if(str == args){
 			str += 5;
			/* FIND THE NEXT&LAST SPACE */
 			tmp = strstr(str, " ");
			/* CHECK IF PROTOCOL TERMINATOR IS PRESENT AFTER SPACE */
 			if(*(tmp+1) == '\r'){
				/* SAVE THE VALUE (NAME OR PASSWORD) */
 				value = calloc(1, tmp-str+1);
 				strncpy(value, str, tmp-str);
				/* FINALLY, CHECK FOR WHOLE PROTOCOL TERMINATOR */
 				if((str=strstr(tmp, "\r\n\r\n")) != NULL){
					/* SET SEQUENCE TO 1 AND RETURN VALUE */
 					*seq = 2;
 					return value;
 				}
 			}
 		}
 	}
 	return NULL;
 }

int check_if_logging_alrdy(char* username){
 	/* CHECK IF USER IS ALREADY TRYING TO LOGIN SOMEWHERE */
	int count = logging_in;
	int alrdy_trying = 0;
	// if(count == 0){
	// 	logging_in++;
	// 	return 0;
	// }
	pthread_rwlock_rdlock(&list_lock);
	while(count > 0){
		if(!strcmp(logging_users[count-1], username)){
			alrdy_trying++;
			break;
		}
		//sfwrite(&std_lock, stdout, "STUCK IN LOOP\n");
		count--;
	}
	pthread_rwlock_unlock(&list_lock);
	if(alrdy_trying){
		/* ACCOUNT IS ATTEMPTING LOGIN ALREADY. RETURN -1 */
		return -1;
	}
	else{
		pthread_rwlock_wrlock(&list_lock);
		/* STORE USERNAME IN LIST OF LOGGING IN USERS */
		logging_users[logging_in] = Strcpy(username);
		logging_in++;
		pthread_rwlock_unlock(&list_lock);
		return 0;
	}
 }

void remove_frm_logging(char* username){
	int indexOf = 0;
	int count = logging_in;
	/* FIND INDEX OF USERNAME IN LIST OF LOGGING IN NAMES */
	/* REMOVE AND SHIFT */
	if(logging_in == 1){
		logging_in--;
		//sfwrite(&std_lock, stdout, "REMOVING : %s\n", logging_users[indexOf]);
		free(logging_users[indexOf]);
		return;
	}
	pthread_rwlock_wrlock(&list_lock);
	while(count--){
		if(!strcmp(logging_users[count], username)){
			indexOf = count;
			break;
		}
	}
	for(; indexOf < logging_in - 1; indexOf++){
		/* FREE DATA AT INDEXOF */
		if(indexOf==count)
			//sfwrite(&std_lock, stdout, "REMOVING : %s\n", logging_users[indexOf]);
		free(logging_users[indexOf]);
		logging_users[indexOf] = Strcpy(logging_users[indexOf+1]);
	}
	//sfwrite(&std_lock, stdout, "REMOVING : %s\n", logging_users[indexOf]);
	free(logging_users[logging_in-1]);
	logging_in--;
	pthread_rwlock_unlock(&list_lock);
}

int wolfie_protocol(int clientfd){
	/* CREATE STRING TO RECEIVE ""WOLFIE" FROM CLIENT */
 	char* buff = calloc(1, strlen("WOLFIE \r\n\r\n")+1);
 	Recv(clientfd, buff, 0);
 	if(strcmp(buff, "WOLFIE \r\n\r\n")){
 		close(clientfd);
 		free(buff);
 		perror("\x1B[1;31mnot adherent to protocol\x1B[0m");
 		return -1;
 		exit(EXIT_FAILURE);
 	}
	/* NOW SEND BACK TO CLIENT "EIFLOW" */
 	Send(clientfd, "EIFLOW \r\n\r\n", strlen("EIFLOW \r\n\r\n"), 0);
	/* READY TO RECEIVE "IAM" OR "IAMNEW" */
 	buff = realloc(buff, 1025);
 	memset(buff, 0, 1025);
 	Recv(clientfd, buff, 0);
 	char* name = NULL;
 	int seq = 0;
	/* PARSE THE ARGS TO FIND IF IAMNEW OR IAM. Returns username. seq WILL BE INIT */
 	name = parse_args(buff, &seq);
 	if(name == NULL) {
 		close(clientfd);
 		free(buff);
 		perror("\x1B[1;31mnot adherent to protocol\x1B[0m");
 		return -1;
 		exit(EXIT_FAILURE);
 	}
 	pthread_rwlock_rdlock(&list_lock);
 	User* temp = users;
 	int status = 0;
 	/* USER LOGGING IN OLD ACCOUNT */
 	if(seq == 1){
 		/* FIND ACCOUNT WITH SAME USERNAME */
 		while(temp != NULL){
 			if(strcmp(temp->username, name))
 				temp = temp->next;
 			else{
 				status = temp->active;
 			 	break;
 			}
 		}
 		pthread_rwlock_unlock(&list_lock);
 		if(temp != NULL){
 			if(check_if_logging_alrdy(name)){
 				sfwrite(&std_lock, stdout, "ERROR USER TRYING TO LOGIN ALREADY\n");
 				/* ACCOUNT IS TRYING TO LOGIN ALREADY. ERR 00 */
 				free(buff);
 				error_protocol(clientfd, 0, NULL);
 				return -1;
 			}
 		}
 		/* IF FOUND, CHECK IF ACCOUNT IS ACTIVE */
 		if(temp != NULL){
 			if(status){
 				/* ACCOUNT IS ACTIVE ALREADY. ERR 00 */
 				free(buff);
 				error_protocol(clientfd, 0, name);
 				return -1;
 			}
 			else{
 				/* ACCOUNT NOT ACTIVE. ABLE TO LOGIN. ASK FOR PASSWORD */
 				if(passCheck(name, clientfd, EXISTING_USER)){
 					free(buff);
 					error_protocol(clientfd, 2, name);
 					return -1;
 				}
 				/* passCheck TAKES CARE OF THE REST */
 				return 0;
 			}
 		}
 		else{
			/* NO ACCOUNT WITH THAT USERNAME EXISTS */
 			free(buff);
 			error_protocol(clientfd, 1, NULL);
 			return -1;
 		}
 	}
 	/* CREATE NEW ACCOUNT */
 	else if(seq == 3){
 		pthread_rwlock_unlock(&list_lock);

		/* CHECK IF ACCOUNT IS ALREADY TRYING TO BE CREATED WITH THAT NAME */
		if(check_if_logging_alrdy(name)){
			/* ACCOUNT IS TRYING TO LOGIN ALREADY. ERR 00 */
			sfwrite(&std_lock, stdout, "ERROR USER TRYING TO LOGIN ALREADY ALREADY\n");
			free(buff);
			error_protocol(clientfd, 0, NULL);
			return -1;
		}
		/* ATTEMP TO CREATE NEW ACCOUNT */

		/* CHECK IF LIST IS EMPTY EXCEPT FOR TEMP ACCOUNT */
 		if(temp == NULL){
			/* NO OLD ACCOUNTS TO STOP NEW ACCOUNT CREATION! GO AHEAD */
 			free(buff);
 			hi_new_protocol(clientfd, name);
 			if(passCheck(name, clientfd, NEW_USER)){
 				free(buff);
 				error_protocol(clientfd, 2, name);
 				return -1;
 			}
 			return 0;
 		}
 		pthread_rwlock_rdlock(&list_lock);
		/* NOT EMPTY. SEARCH ALL USERS */
 		while(temp != NULL) {
 			/* IF MATCH, THEN CAN'T BE NEW USER. ERR 00 */
 			if(!strcmp(temp->username, name)) {
 				free(buff);
 				pthread_rwlock_unlock(&list_lock);
 				error_protocol(clientfd, 0, name);
 				return -1;
 			} else {
 				temp = temp->next;
 			}
 		}
 		
 		pthread_rwlock_unlock(&list_lock);
 		/* NO MATCH! ASK FOR NEW PASSWORD */
 		free(buff);
 		hi_new_protocol(clientfd, name);
 		if(passCheck(name, clientfd, NEW_USER)){
 			error_protocol(clientfd, 2, name);
 			free(buff);
 			return -1;
 		}
 		else{
 		 	return 0;
 		}
 	}
 	return -1;
 }

void Listen(int sockfd, int backlog){
	if(listen(sockfd, backlog)) {
		perror("\x1B[1;31mlisten\x1B[0m");
	}
	else sfwrite(&std_lock, stdout, "Currently listening on port %s\x1B[0m\n", PORT_NUMBER);
}

void Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	if((bind(sockfd, addr, addrlen))){
		perror("\x1B[1;31mbind\x1B[0m");
		exit(EXIT_FAILURE);
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

int Accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen){
	int child_fd;
	if((child_fd = accept(sockfd, addr, addrlen))==-1){
		perror("\x1B[1;31maccept\x1B[0m");
		exit(EXIT_FAILURE);
	}
	return child_fd;
}

void Recv(int sockfd, char* buff, int flags) {
	int eof;
	char* ptr = buff;
	while((eof =recv(sockfd, ptr, 1, flags)) > 0) {
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
		disconnected = 1;
		//close(sockfd);
		logout_protocol(sockfd);
		perror("\x1B[1;31mclosed connection\x1B[0m");
        //exit(EXIT_FAILURE);
	}

	if(print_verbs){
		char* verbs = calloc(1, strlen(buff)+1);
		strcpy(verbs,buff);
		char* token = strtok(verbs, PROTO_TERM);
		while(token != NULL){
			sfwrite(&std_lock, stdout,"RECEIVING: \x1B[1;34m%s\x1B[0m\n", token);
			token = strtok(NULL, PROTO_TERM);
		}
		free(verbs);
	}   
}

void Send(int sockfd, void* buff, int len, int flags){
	
	/* ATTEMPT TO SEND BYTES */
	int bytes_sent = send(sockfd, buff, len, 0);
	/* CHECK IF ALL BYTES WERE SENT */
	while(bytes_sent != len){
		/* ATTEMPT TO SEND LEFTOVER BYTES ONE BYTE AT A TIME */
		if(send(sockfd, &(((char*)buff)[bytes_sent]), 1, 0) != 1)
			continue;
		else bytes_sent++;
	}
	if(print_verbs){
		char* verbs = calloc(1, strlen(buff)+1);
		strcpy(verbs,buff);
		char* token = strtok(verbs, "\r");
		if(!flags) {
			sfwrite(&std_lock, stdout,"SENDING:   \x1B[1;34m%s\x1B[0m\n", token);
		} else if(flags) {
			while(strncmp(token, "", 1)){
				sfwrite(&std_lock, stdout,"SENDING:   \x1B[1;34m%s\x1B[0m\n", token);
				token = strtok(NULL, "\r");
				token++;
			}
		}
		free(verbs);
	}		
}
/* USAGE AND HELP PRINT OUTS */
void print_usage(){
	sfwrite(&std_lock, stdout, "./server [-h|-v] [-t THREAD_COUNT] PORT_NUMBER MOTD [ACCOUNTS_FILE]\n"
		"-h\t\tDisplays help menu & returns EXIT_SUCCESS.\n"
		"-t THREAD_COUNT\tThe number of threads used for the login queue.\n"
		"-v\t\tVerbose print all incoming and outgoing protocol verbs & content.\n"
		"PORT_NUMBER\tPort number to listen on.\n"
		"MOTD\t\tMessage to display to the client when they connect.\n"
		"ACCOUNTS_FILE\tFile containing username and password data to be loaded upon execution.\n"
		);
}

void help_protocol(){
	sfwrite(&std_lock, stdout, "\n/users\t\tPrint a list of currently logged in users to server's stdout.\n\n"
		"/accts\t\tPrint a list of all user accounts to server's stdout,\n\n"
		"/help\t\tShows all the commands which the server accepts and their \n\t\tfunctionality.\n\n"
		"/shutdown\tDisconnects all connected users. Saves all needed states, close\n\t\t"
		"all sockets and files and free any heap memory allocated.\n"
		);
}