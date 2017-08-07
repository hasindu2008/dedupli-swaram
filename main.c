#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "socket.h"
#include <netdb.h> 
#include <arpa/inet.h>
#include <signal.h>

#define NODES 3

#define BUFFER_SIZE 1000
#define BUFF_SIZE 1000

#define LOOPS 5

//store the IPs
char node_ips[NODES][256] = {};

//conditional variable for notifying to send
pthread_cond_t send_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t send_mutex = PTHREAD_MUTEX_INITIALIZER;
char send_flag = 0;

//conditional variable for notify that data received 
pthread_cond_t rcvd_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t rcvd_mutex = PTHREAD_MUTEX_INITIALIZER;
char rcvd = 0;

//barrier to reset the send_flag variable
pthread_barrier_t mybarrier;
pthread_barrier_t mybarrier2;

//error check all pthreads

void pthread_check(int ret);
void errorCheckNULL(void *ret,char *msg);
void *client_connection(void *arg);
void *server_connection(void *arg);

int main(){
    
    int ret ,i;
    
    //reading config file, Contains what other hosts should be connected
    FILE *config = fopen("/etc/odroid_topology","r");
    errorCheckNULL(config,"Cannot open file");   
    
    for(i=0;i<NODES;i++){
        fgets(&node_ips[i][0], 256, config);  //check returned value, 
    }
    fclose(config);    
    
    
    //data structures for clients (connect to servers running on other ips)
    pthread_t client_thread[3];     
    int thread_id[NODES];
    
    //data structures for servers 
    pthread_t server_thread[3] ;       
    int connect_fd[NODES];
    

    //First create threads for clients
    for(i=0;i<NODES;i++){
        thread_id[i]=i;
        ret = pthread_create( &client_thread[i], NULL, client_connection, (void *)(&thread_id[i])) ;
        pthread_check(ret);
    }
    
    //create a listening socket on port 20000
	int listenfd=TCP_server_init(20000);
 
    //threads for server
    for(i=0;i<NODES;i++){
        //accept a client connection
        connect_fd[i] = TCP_server_accept_client(listenfd); 
        ret = pthread_create( &server_thread[i], NULL, server_connection, (void *)(&connect_fd[i])) ;
        pthread_check(ret);           
    }
    
    
    //init the barrier
    pthread_barrier_init(&mybarrier, NULL, NODES*2 + 1);
    pthread_barrier_init(&mybarrier2, NULL, NODES*2 + 1);
    
    for(i=0;i<LOOPS;i++){
        //need to read the files and create an array containing the quality score of each read
        fprintf(stderr,"Creating arrays\n");
        sleep(rand()%10);
        fprintf(stderr,"Finished creating arrays\n");
        
        //send client threads a signal to send the data
        pthread_mutex_lock(&send_mutex);
        send_flag = 1;
        pthread_cond_broadcast(&send_cond);
        pthread_mutex_unlock(&send_mutex);
        


        //need to wait until arrays from all the nodes have been received
        fprintf(stderr,"Waiting till all data is received\n");
        pthread_mutex_lock(&rcvd_mutex);
        while(rcvd<NODES){
             pthread_cond_wait(&rcvd_cond, &rcvd_mutex);
        } 
        pthread_mutex_unlock(&rcvd_mutex);
        fprintf(stderr,"All data received\n");
        
        
        //barrier
        pthread_barrier_wait(&mybarrier);
        send_flag = 0;
        rcvd = 0;    
        pthread_barrier_wait(&mybarrier2);        
        
        //write new files based on received information
        sleep(rand()%10);

    }

    //joining client side threads
    for(i=0;i<NODES;i++){
        ret = pthread_join ( client_thread[i], NULL );
        pthread_check(ret);
    }    
    
     //joining client side threads
    for(i=0;i<NODES;i++){
        ret = pthread_join ( server_thread[i], NULL );
        pthread_check(ret);
    }    
    
    //close the down the listening socket
    TCP_server_shutdown(listenfd);

    return 0;
    
}




void pthread_check(int ret){
    if(ret!=0){
        perror("Error creating thread");
        exit(EXIT_FAILURE);
    }
}

/*Die on error. Print the error and exit if the return value of the previous function NULL*/
void errorCheckNULL(void *ret,char *msg){
	if(ret==NULL){
		perror(msg);
		exit(EXIT_FAILURE);
	}
}




void *client_connection(void *arg)
{
	sleep(1); //wait till servers are setup
    int id = *((int *)arg);
    
    //char ip[16];
    //get_ip(node_ips[id],ip);
    
    //Connect to the server given as argument on port 20000
    int socketfd = TCP_client_connect(node_ips[id], 20000);
    
    int i;
    for(i=0;i<LOOPS;i++){
    
        //need to wait till we get a signal to send data
        pthread_mutex_lock(&send_mutex);
        while (send_flag ==0){
            pthread_cond_wait(&send_cond, &send_mutex);
        }
        pthread_mutex_unlock(&send_mutex);
        
        char buffer[BUFF_SIZE]="Hi! I am the client. Serve me please!";
        
        //send the message
        send_full_msg(socketfd, buffer, strlen(buffer));
        fprintf(stderr,"Message sent\n");
        
        //barrier
        pthread_barrier_wait(&mybarrier);
        pthread_barrier_wait(&mybarrier2);
    }
    
    //receive the message
	//int received = recv_full_msg(socketfd,buffer,BUFF_SIZE);
    
    //print the message
	//buffer[received]='\0';  //null character before priniting the string
	//fprintf(stderr,"Recieved : %s\n",buffer);
	
    //close the connection
	TCP_client_disconnect(socketfd); 

	return NULL;
}

//server thread
void *server_connection(void *arg)
{
	sleep(1); //not needed
    int connectfd = *((int *)arg);
    
    char buffer[BUFFER_SIZE];   
    
    //this keeps on listening
 
    int i=0;
    for(i=0;i<LOOPS;i++){
        //get message from client
        int received = recv_full_msg(connectfd,buffer,BUFFER_SIZE);    
        
        //print the message
        buffer[received]='\0'; //null character before priniting the stringsince
        fprintf(stderr,"Recieved : %s\n",buffer); 
     
        //need to say that we received stuff
        pthread_mutex_lock(&rcvd_mutex);
        rcvd ++;
        if(rcvd == NODES){
            pthread_cond_signal(&rcvd_cond);
        }
        pthread_mutex_unlock(&rcvd_mutex); 
        
        //barrier
        pthread_barrier_wait(&mybarrier);
        pthread_barrier_wait(&mybarrier2);

 
    }
 
    //Copy a string to buffer
    //strcpy(buffer,"Hi! This is the server reply.");
 
    //send a message to the client
    //send_full_msg(connectfd,buffer,strlen(buffer));

    //close doen the client connection
    TCP_server_disconnect_client(connectfd);
    
	return NULL;
}







//a function to get ip using host name
int get_ip(char * hostname , char* ip) 
{  
   struct hostent *he;     
   struct in_addr **addr_list;     
   int i;     
   if ( (he = gethostbyname( hostname ) ) == NULL){ 
        herror("gethostbyname");         
        return 1;
   }     
   addr_list = (struct in_addr **) he->h_addr_list;
   for(i = 0; addr_list[i] != NULL; i++){   
        strcpy(ip , inet_ntoa(*addr_list[i]) );
        return 0;
   }
   return 1;
} 

