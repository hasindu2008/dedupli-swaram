#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "socket.h"
#include <netdb.h> 
#include <arpa/inet.h>

#define NODES 3

#define BUFFER_SIZE 1000
#define BUFF_SIZE 1000


char node_ips[NODES][256] = {};


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


void *client_connection(void *arg)
{
	sleep(1);
    int id = *((int *)arg);
    
    //char ip[16];
    //get_ip(node_ips[id],ip);
    
    //Connect to the server given as argument on port 20000
    int socketfd = TCP_client_connect(node_ips[id], 20000);
    
    //need to wait till we get a signal to send data
    
    
    
    char buffer[BUFF_SIZE]="Hi! I am the client. Serve me please!";
    
    //send the message
    send_full_msg(socketfd, buffer, strlen(buffer));

    //receive the message
	int received = recv_full_msg(socketfd,buffer,BUFF_SIZE);
    
    //print the message
	buffer[received]='\0';  //null character before priniting the string
	fprintf(stderr,"Recieved : %s\n",buffer);
	
    //close the connection
	TCP_client_disconnect(socketfd); 

 
	return NULL;
}


void *server_connection(void *arg)
{
	sleep(1);
    int connectfd = *((int *)arg);
    
    char buffer[BUFFER_SIZE];   
    
	//get message from client
	int received = recv_full_msg(connectfd,buffer,BUFFER_SIZE);
	
    //print the message
    buffer[received]='\0'; //null character before priniting the stringsince
	fprintf(stderr,"Recieved : %s\n",buffer); 
 
    //Copy a string to buffer
    strcpy(buffer,"Hi! This is the server reply.");
 
    //send a message to the client
    send_full_msg(connectfd,buffer,strlen(buffer));

    //close doen the client connection
    TCP_server_disconnect_client(connectfd);
    
	return NULL;
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




int main(){
    
    int ret ,i;
    
    //reading config file
    FILE *config = fopen("/etc/odroid_topology","r");
    errorCheckNULL(config,"Cannot open file");   
    
    for(i=0;i<NODES;i++){
        fgets(&node_ips[i][0], 256, config);  //check returned value, 
    }
    
    fclose(config);    
    
    
    //clients
    pthread_t client_thread[3];     
    int thread_id[NODES];
    
    //servers
    pthread_t server_thread[3] ;       
    int connect_fd[NODES];
    

    //First create threads for client side
    for(i=0;i<NODES;i++){
        thread_id[i]=i;
        ret = pthread_create( &client_thread[i], NULL, client_connection, (void *)(&thread_id[i])) ;
        pthread_check(ret);
    }
    
    
    //create a listening socket on port 20000
	int listenfd=TCP_server_init(20000);
 
    //threads for server side
    for(i=0;i<NODES;i++){
        //accept a client connection
        connect_fd[i] = TCP_server_accept_client(listenfd); 
        ret = pthread_create( &server_thread[i], NULL, server_connection, (void *)(&connect_fd[i])) ;
        pthread_check(ret);           
    }
    
    //actual processing happens here
    
    
    //need to send signals to therads
    
    
    
    
    

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

