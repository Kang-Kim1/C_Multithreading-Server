#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#define FILE_AND_DIR_SIZE 1024
#define TIME_BUFF_SIZE 1024
#define MAX_THREAD_NUM 300
#define MAX_QUEUE_SIZE 400

// Variables for -d
bool debug_on = false;
        
// Variable for -l
char log_file_name[FILE_AND_DIR_SIZE];
bool log_on = false;
        
// Variables for -p
int port = 8080;
bool port_oni = false;
       
// Variables for -r
char root_dir[FILE_AND_DIR_SIZE];
bool dir_on = false;
        
// Variables for -t
int time_in = 60;
        
// Varaibles for -n
int thread_num = 4;
        
// Variables for -s
char* scheduling_name = "FCFS";
bool scheduling_on = false;

// For -h
void print_usage(void);

// For threading
pthread_t listener_t;
pthread_t scheduler_t;
pthread_t executer_t[MAX_THREAD_NUM];

void* t_do_schedule(void*);
void* t_do_listen(void);
void* t_do_execute(void);

int curr_request_num;

// local server socket
int* temp_sock_ptr;
int local_sock;

// File name for response
//char* res_file_name;

// Mutex
pthread_mutex_t ready_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t exe_que_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t exe_file_mutex = PTHREAD_MUTEX_INITIALIZER;

struct packet 
{
	int sock_num;
	unsigned long ip;
	char file_name[FILE_AND_DIR_SIZE];
	char request_type; // can be G(et)/H(ead)/N(one)
	char arr_time[TIME_BUFF_SIZE];
	char mod_time[TIME_BUFF_SIZE];
	//This will be handled in executer thread
	char file_extention; // can be T(ext), I(mage), N(invalid)
	char content_type[1024];
	int file_size;
	// if true = 200 / if false = 404 
	bool file_exists;
};

struct queue
{
	struct packet items[MAX_QUEUE_SIZE]; 	
	int size;
};

struct queue ready_queue;

//Queue Functions
void init_ready_queue(struct queue* q);
void queue_insert(struct queue* q, struct packet* input);
struct packet queue_delete(struct queue* q);
void print_queue(struct queue q);
void print_packet(struct packet p);


void print_usage(){
        printf("\n-d : Enter debugging mode. That is, do not daemonize, only accept one connection at a time and enable logging to stdout. Without this option, the web server should run as a daemon process in the background.\n-h : Print a usage summary with all options and exit.\n-l file : Log all requests to the given file. See LOGGING for details.\n-p port : Listen on the given port. If not provided, myhttpd will listen on port 8080.\n-r dir : Set the root directory for the http server to dir.\n-t time : Set the queuing time to time seconds. The default should be 60 seconds.\n");
}


//Threading scheduling in this function

//Do we need void *arg in the parameters?
//Pointer to the thread function we usually use void*, (*),  (void*)
//Whatever you pass in is simply passed as the argument to the thread function when the thread begins executing.
//arg can be named anything, in this case it is named threadArgument1

//________________________________________________________________________
void* t_do_schedule(void *threadArgument1){
    if(strcmp(scheduling_name,"FCFS") == 0){}
     
    else if(strcmp(scheduling_name,"SJF") == 0){
        pthread_mutex_lock(&ready_queue_mutex);
   
        int least_size = 0;
        for(int i=0; i < ready_queue.size;i++){
            
            int least_size = ready_queue.items[i].file_size;
            int least_size_index = i;
            for(int j=i; j < ready_queue.size; j++){
                if(least_size > ready_queue.items[j].file_size){
                    least_size = ready_queue.items[j].file_size;
                    least_size_index = j;
                } 
            }    
            if(i != least_size_index){
                struct packet temp = ready_queue.items[i];
                ready_queue.items[i] = ready_queue.items[least_size_index];
                ready_queue.items[least_size_index] = temp;   
            }
        }
        pthread_mutex_unlock(&ready_queue_mutex);

    }else{
        printf("SchedulingName: [%s]\n",scheduling_name);
        printf("Wrong scheduling_name\n");
    }
}
//________________________________________________________________________

void* t_do_listen(){
	local_sock = *temp_sock_ptr;	

	int in_fd;
	struct sockaddr_in c_addr;
	
	// wait queue size as 10

	listen(local_sock, 5);
	

	int c_addr_len = sizeof(c_addr);

	char buff[1024];
	memset(buff, 0, sizeof(buff));

	
	// To get file infos
	struct stat file_info;

	while(1) {
		// Accept incoming requests
		if((in_fd = accept(local_sock, (struct sockaddr *)&c_addr, &c_addr_len)) < 0){
			printf("ACCEPT ERROR\n");
			exit(1);
		}
		int listener_ret;
	
		// RECV handling
		if((listener_ret = recv(in_fd, buff, 1024, 0)) < 0) {
			printf("RECV ERROR");
			printf("buffer : %s\n", buff);
			continue;
		}
		// Receivied packet Successfully
		else
		{
			struct packet* temp_packet;	
			temp_packet = (struct packet*)malloc(sizeof(struct packet));


			char* res_file_name = malloc(sizeof(char*));		
			char* res_type = malloc(sizeof(char*));

			res_type = strtok(buff," ");
			res_file_name = strtok(NULL, " ");

			
			if(strstr(res_type, "GET") != NULL) {
				temp_packet->request_type = 'G';
			} else if(strstr(res_type, "HEAD") != NULL) {
				temp_packet->request_type = 'H';
			} else {
				temp_packet->request_type = 'N';
			}

			// file name "/file_name"
			//res_file_name = strtok(NULL, " ");
			res_file_name[strlen(res_file_name)-1] = '\0';

			
			// remove / in front of file_name if exists
			char* temp_holder = malloc(sizeof(char*));
			strcpy(temp_holder, res_file_name);
			if(res_file_name[0] == '/'){
				//strncpy(res_file_name, res_file_name + 1, strlen(res_file_name));
				memcpy(temp_holder, res_file_name + 1, strlen(res_file_name));
			}
			strcpy(res_file_name, temp_holder);

			// check the file extention
			if(strstr(res_file_name, ".txt") != NULL || strstr(res_file_name, ".html") != NULL) {
				temp_packet->file_extention = 'T';
			} else if(strstr(res_file_name, ".gif") != NULL || strstr(res_file_name, ".jpg") != NULL) {
				temp_packet->file_extention = 'I';
			} else {
				temp_packet->file_extention = 'T';
			}


			
			if((stat(res_file_name, &file_info)) == -1) 
			{
				temp_packet->sock_num = in_fd;
				char temp_ip[1000];
				temp_packet->ip = c_addr.sin_addr.s_addr;
				strcpy(temp_packet->file_name, res_file_name);
				time_t now = time(NULL);
				struct tm* time_info;
				time_info = localtime(&now);
				strftime(temp_packet->arr_time, TIME_BUFF_SIZE, "[%d/%h/%Y:%H:%M:%S  %z]", time_info);
				// no file format
				strncpy(temp_packet->mod_time, "[00/000/0000:00:00:00  -0000]", sizeof(temp_packet->mod_time)-1);
				
				temp_packet->file_size = file_info.st_size;
				strncpy(temp_packet->content_type, "text/html", sizeof(temp_packet->content_type)-1);
				temp_packet->file_exists = false;
			}
			else
			{
				temp_packet->sock_num = in_fd;
                                temp_packet->ip = c_addr.sin_addr.s_addr;
				strcpy(temp_packet->file_name, res_file_name);
                                time_t now = time(NULL);
                                struct tm* time_info;
                                time_info = localtime(&now);
                                strftime(temp_packet->arr_time, TIME_BUFF_SIZE, "[%d/%h/%Y:%H:%M:%S  %z]", time_info);
                                time_info = localtime(&file_info.st_mtime);
                                strftime(temp_packet->mod_time, TIME_BUFF_SIZE, "[%d/%h/%Y:%H:%M:%S  %z]", time_info);
				temp_packet->file_size = file_info.st_size;
                                strncpy(temp_packet->content_type, "text/html", sizeof(temp_packet->content_type)-1);
				temp_packet->file_exists = true;
			}

			// Debug temp_packet
			/*
			printf("%d\n", temp_packet->sock_num);
			printf("%d\n", temp_packet->ip);
			printf("%s\n", temp_packet->file_name);
			printf("%s\n", temp_packet->arr_time);
			printf("%s\n", temp_packet->mod_time);
			printf("%s\n", temp_packet->content_type);
			printf("%d\n", temp_packet->file_size);
			printf("%s", temp_packet->file_exists? "true" : "false");
			*/

			// Debug queue insertion
			queue_insert(&ready_queue, temp_packet);
			//print_queue(ready_queue);
			memset(res_file_name, 0, sizeof(res_file_name));
			memset(res_type, 0, sizeof(res_type));

		}		
	}


}

void* t_do_execute(){

    struct packet temp;
    // While queue is empty handle all the packets
    
    int temp_code;
    char* temp_extention;

    char* send_buff[2048];

    if(ready_queue.size <= 0) {
	return 0;
    }

    while(ready_queue.size != 0){

        // MUTEX1_GET LOCK
        pthread_mutex_lock(&exe_que_mutex);
        temp = queue_delete(&ready_queue);
        pthread_mutex_unlock(&exe_que_mutex);

	char temp_time[TIME_BUFF_SIZE];

        char* temp_con_type;
        if(temp.request_type == 'G') {
                temp_con_type = "GET";
        }else if(temp.request_type == 'H') {
                temp_con_type = "HEAD";
        } else {
                temp_con_type = "ERROR";
        }

	temp_code = temp.file_exists ? 200 : 404;

	if(temp.file_extention == 'I') {
		temp_extention = "image/gif";
	} else {
		temp_extention = "text/html";
	}

        // handle log mode
        if(log_on == true) {

	    pthread_mutex_lock(&exe_file_mutex);
            FILE *log_file = fopen(log_file_name, "a");

            // to get current time
            time_t now = time(NULL);
            struct tm* time_info;
            time_info = localtime(&now);
            
	    strftime(temp_time, TIME_BUFF_SIZE, "[%d/%h/%Y:%H:%M:%S  %z]", time_info);

            struct in_addr ip_addr;
            ip_addr.s_addr = temp.ip;

            
	    // TEXT file
            if(temp.file_extention == 'T') {
                    fprintf(log_file,"%s - %s %s \"%s /%s HTTP/1.0\" %d %d\n", inet_ntoa(ip_addr), temp.arr_time, temp_time, temp_con_type, temp.file_name, temp_code, temp.file_size);

            // Image file
            } else if(temp.file_extention == 'I') {
                    fprintf(log_file,"%s - %s %s \"%s /%s HTTP/1.0\" %d %d\n", inet_ntoa(ip_addr), temp.arr_time, temp_time, temp_con_type, temp.file_name, temp_code, temp.file_size);

            } else {
                    // WRONG extention
                    // since we can assume all request would be valid, this wouldnt happen
            }
        
	    fclose(log_file);
	    pthread_mutex_unlock(&exe_file_mutex);
	    temp_time[0] = '\0';
        }

        if(debug_on == true) {
            time_t now = time(NULL);
            struct tm* time_info;
            time_info = localtime(&now);

            strftime(temp_time, TIME_BUFF_SIZE, "[%d/%h/%Y:%H:%M:%S  %z]", time_info);

            struct in_addr ip_addr;
            ip_addr.s_addr = temp.ip;

	    char o_buffer[2048];

            if(temp.file_extention == 'T') {

                    sprintf(o_buffer,"%s - %s %s \"%s /%s HTTP/1.0\" %d %d\n", inet_ntoa(ip_addr), temp.arr_time, temp_time, temp_con_type, temp.file_name, temp_code, temp.file_size);


            } else if(temp.file_extention == 'I') {

                    sprintf(o_buffer,"%s - %s %s \"%s /%s HTTP/1.0\" %d %d\n", inet_ntoa(ip_addr), temp.arr_time, temp_time, temp_con_type, temp.file_name, temp_code, temp.file_size);

            } else {

            }
	printf("%s", o_buffer);
        }

	    
	    memset(temp_time, 0, sizeof(temp_time));
            time_t now = time(NULL);
            struct tm* time_info;
            time_info = localtime(&now);

            strftime(temp_time, TIME_BUFF_SIZE, "[%d/%h/%Y:%H:%M:%S  %z]", time_info);
	
	if(temp_code == 404) {
	    strcpy(send_buff, "[HEADER]\n404 NOT FOUND\n");
	    send(temp.sock_num, send_buff, strlen(send_buff), 0);
	    
	    char temp1[2048];
	    sprintf(temp1, "Date : %s\n", temp_time);
	    send(temp.sock_num, temp1, strlen(temp1), 0);
	    
	    char temp2[2048];
	    sprintf(temp2, "Server : HTTP/1.0\n");
	    send(temp.sock_num, temp2, strlen(temp2), 0);

	    char temp3[2048];
	    sprintf(temp3, "Last-Modified : %s\n", temp.mod_time);
	    send(temp.sock_num, temp3, strlen(temp3), 0);

	    char temp4[2048];
	    sprintf(temp4, "Content-Type : %s\n", temp_extention);
	    send(temp.sock_num, temp4, strlen(temp4), 0); 

	    char temp5[2048];
	    sprintf(temp5, "Content-Length : %d\n", temp.file_size);
	    send(temp.sock_num, temp5, strlen(temp5), 0);
	}
	else
	{
            strcpy(send_buff, "[HEADER]\n200 OK \n");
            send(temp.sock_num, send_buff, strlen(send_buff), 0);

            char temp1[2048];
            sprintf(temp1, "Date : %s\n", temp_time);
            send(temp.sock_num, temp1, strlen(temp1), 0);

            char temp2[2048];
            sprintf(temp2, "Server : HTTP/1.0\n");
            send(temp.sock_num, temp2, strlen(temp2), 0);

            char temp3[2048];
            sprintf(temp3, "Last-Modified : %s\n", temp.mod_time);
            send(temp.sock_num, temp3, strlen(temp3), 0);

            char temp4[2048];
            sprintf(temp4, "Content-Type : %s\n", temp_extention);
            send(temp.sock_num, temp4, strlen(temp4), 0);

            char temp5[2048];
            sprintf(temp5, "Content-Length : %d\n", temp.file_size);
            send(temp.sock_num, temp5, strlen(temp5), 0);
	 
            if(temp.request_type == 'G') {
	    	char temp6[2048];
		FILE *opened_file = fopen(temp.file_name, "r");
		char* temp_c;
		
		if(opened_file != NULL) {
			while((temp_c = getc(opened_file)) != EOF) {
				strcat(temp6, &temp_c);
			}
			fclose(opened_file);
		}    
		char temp7[2048];
		sprintf(temp7, "[CONTENTS]\n%s", temp6);
		send(temp.sock_num, temp7, strlen(temp7), 0);	
	    }

	}
    }
    thread_num++;
	close(temp.sock_num);
    return 0;
}


int main(int argc, char* argv[])
{
	if(argc == 1) 
	{
		printf("Wrong number of arguments. Please try again.\n");
		exit(1);
	}


	int counter = 1;

	// should check all commands
	while(counter < argc){
	char* command = argv[counter];
	//printf("command : [%s]\n", command);

		if(strcmp(command,"-d") == 0){
		//	printf("[ Debugging Mode ]\n");
			debug_on = true;
			thread_num = 1;
			counter ++;
		}

		else if(strcmp(command, "-h") == 0) {
			print_usage();
			exit(0);
		}

		else if(strcmp(command, "-l") == 0) {
		//	printf("[ Log All Requests : ON ]\n");
			strcpy(log_file_name, argv[counter+1]);
			log_on = true;
			counter = counter + 2;
		}
		
		else if(strcmp(command, "-p") == 0) { 
		//	printf("[ Port Received ] \n");
			port = atoi(argv[counter+1]);
			counter = counter + 2;
		}
		
		else if(strcmp(command, "-r") == 0) {
		//	printf("[ Root Directory Received ]\n");
			//strcpy(root_dir, argv[2]);
			strcpy(root_dir, argv[counter+1]);
			dir_on = true;
		
			int ret_chdir = chdir(root_dir);	
			counter = counter + 2;	
		
			if(ret_chdir == -1) 
			{
				printf("Wrong Directory.\n");
				exit(1);
			} 
			else
			{
				printf("Directory Changed To %s\n", root_dir);
			}
		}

		else if(strcmp(command, "-t") == 0) {
		//	printf("[ Queing Time Received ]\n");
			time_in = atoi(argv[counter + 1]);
			counter = counter + 2;
		}

		else if(strcmp(command, "-n") == 0) {
		//	printf("[ Thread Number Received ]\n");
			thread_num = atoi(argv[counter + 1]);
			counter = counter + 2;
		}

		else if(strcmp(command, "-s") == 0) {
		//	printf("[ Scheduling Policy Recieved ]\n");
            if(strcmp(argv[counter+1],"SJF") == 0){
                scheduling_name = "SJF";
            }else if(strcmp(argv[counter+1],"FCFS") ==0){
                scheduling_name = "FCFS";
            }else{
                printf("Scheduling policy is not clear. (set default)\n");
            }
            printf("- Scheduling policy: %s\n",scheduling_name);
			counter = counter + 2;
		}

		else { 
			printf("Wrong Command. Please check commands through -h command\n");
			exit(1);	
		}
	
	}

	// Ready-Queue Setup
	init_ready_queue(&ready_queue);
	
	// Server Setup
	struct sockaddr_in saddr;
	int saddr_len = sizeof(saddr);

	// addr struct Setup
	bzero((char*)&saddr, saddr_len);
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = INADDR_ANY;
	saddr.sin_port = htons(port);
	saddr_len = sizeof(saddr);

	// Get Socket
	int sock_id;
	if((sock_id = socket(AF_INET, SOCK_STREAM,0))< -1) 
	{
		printf("Socket Error.\n");
		exit(1);
	}	

	// Bind Socket
	if(bind(sock_id, (struct sockaddr*)&saddr, sizeof(saddr)) != 0)
	{
		printf("Bind Error.\n");
		exit(1);
	}	
	
	temp_sock_ptr = &sock_id;
	
	pthread_create(&listener_t, NULL, (void*)&t_do_listen, NULL);
	int i=0;
	while(1){
		sleep(time_in);
		pthread_create(&scheduler_t, NULL, (void*)&t_do_schedule, NULL);

    		sleep(3);
/*
    while(1) {
		
		for(int i = 0; i < thread_num; i++){
                thread_num--;
                pthread_create(&executer_t[i], NULL, (void*)&t_do_execute, NULL);
        }
	}
*/
		while(ready_queue.size != 0) {
			if(i == MAX_THREAD_NUM) break;
			while(thread_num == 0) {}
			thread_num--;
			pthread_create(&executer_t[i], NULL, (void*)&t_do_execute,NULL);
			i++;
		}
	}
	return 0;
	
}
void init_ready_queue(struct queue* q)
{
    pthread_mutex_lock(&ready_queue_mutex);
	q->size = 0;
    pthread_mutex_unlock(&ready_queue_mutex);
}

void queue_insert(struct queue* q, struct packet* item) {
	if(q->size == MAX_QUEUE_SIZE)
	{	
		printf("OUT OF BOUND\n");
	}
	else 
	{
        pthread_mutex_lock(&ready_queue_mutex);
        q->items[q->size++] = *item;
        pthread_mutex_unlock(&ready_queue_mutex);
	}

}

struct packet queue_delete(struct queue* q) {
    pthread_mutex_lock(&ready_queue_mutex);

	struct packet temp;
    temp = q->items[0];		
    if(q->size == 1) {
        memset(&q->items[0], 0, sizeof(q->items[0]));
    } else {
        for(int i = 0; i < q->size - 1; i++){
            q->items[i] = q->items[i+1];	
        }
        memset(&q->items[q->size-1], 0, sizeof(q->items[q->size-1]));
    }
    q->size--;

    pthread_mutex_unlock(&ready_queue_mutex);
	return temp;
}

void print_queue(struct queue q) {
    pthread_mutex_lock(&ready_queue_mutex);
    for(int i = 0; i < q.size; i++) {
        printf("= index : %d ===================================\n", i);
        printf("fd num : %d\n", ready_queue.items[i].sock_num);
        printf("ip     : %d\n", ready_queue.items[i].ip);
        printf("file   : %s\n", ready_queue.items[i].file_name);
        printf("request: %c\n", ready_queue.items[i].request_type);
        printf("arrive : %s\n", ready_queue.items[i].arr_time);
        printf("mod    : %s\n", ready_queue.items[i].mod_time);
        printf("exten  : %c\n", ready_queue.items[i].file_extention);
        printf("type   : %s\n", ready_queue.items[i].content_type);
        printf("size   : %d\n", ready_queue.items[i].file_size);
        printf("exsits : %s\n", ready_queue.items[i].file_exists ? "true" : "false");
        printf("================================================\n");
	}
    pthread_mutex_unlock(&ready_queue_mutex);
}

void print_packet(struct packet p){
    printf("[Packet]\n");    
    printf("file_name: %s\n",p.file_name);
    printf("file_size: %d\n",p.file_size);

}
