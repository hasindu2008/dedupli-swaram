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

#define BUFF_SIZE 1000

#define LOOPS 1

//#define STARTID 0
#define READS 80000

#define DEBUG 0
#define MANUAL_ARG 1  //if this is 1 make sure DEBUG is 0 and LOOPS os 1

long STARTID = 0;

//store the IPs
char node_ips[NODES][256] = {};

//array for storing mapping quality of each read
unsigned char mapq_array[NODES+1][READS];

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

//function protos
void pthread_check(int ret);
void errorCheckNULL(void *ret,char *msg);
void errorCheckScanVal(int ret,char *msg);
void encode_mapqarray(char *inputname, char *outputname, int devno);
void deduplicate(char *inputname, char *outputname);

void *client_connection(void *arg);
void *server_connection(void *arg);

int main(int argc, char **argv){
    
#ifdef MANUAL_ARG
    if(argc!=4){
        fprintf(stderr,"Usage : %s input.sam output.sam startID\n",argv[0]);
        exit(EXIT_FAILURE);
    }
#endif    
    
    int ret ,i;
    char inputfilename[256];
    char debugfilename[256];
    char outputfilename[256];
    
    
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
    int connect_fd[NODES][2];
    

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
        connect_fd[i][0] = i+1;
        //accept a client connection
        connect_fd[i][1] = TCP_server_accept_client(listenfd); 
        ret = pthread_create( &server_thread[i], NULL, server_connection, (void *)(connect_fd[i])) ;
        pthread_check(ret);           
    }
    
    
    //init the barrier
    pthread_barrier_init(&mybarrier, NULL, NODES*2 + 1);
    pthread_barrier_init(&mybarrier2, NULL, NODES*2 + 1);
    
    for(i=0;i<LOOPS;i++){
        
        //generate filenames
        sprintf(inputfilename,"/home/odroid/sorting_framework/data/set%d.sam",i);
#ifdef DEBUG
        sprintf(debugfilename,"/home/odroid/sorting_framework/data/debug%d.sam",i);
#endif  
        sprintf(outputfilename,"/home/odroid/sorting_framework/data/dedupli%d.sam",i);    

#ifdef MANUAL_ARG
        strcpy(inputfilename,argv[1]);
        strcpy(outputfilename,argv[2]);
        STARTID = atol(argv[3]);
#endif        
      
        //need to read the files and create an array containing the quality score of each read
        fprintf(stderr,"Creating arrays\n");

        encode_mapqarray(inputfilename, debugfilename, 0);
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
        deduplicate(inputfilename, outputfilename);
        STARTID += READS;

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



void errorCheckScanVal(int ret,char *msg){
    if(ret!=1){
        fprintf(stderr,"%s\n",msg);
		exit(EXIT_FAILURE);
    }  
    
}


void encode_mapqarray(char *inputname, char *outputname, int devno){

    FILE *input = fopen(inputname,"r");
    errorCheckNULL(input,"Cannot open file");

    
#ifdef DEBUG    
    FILE *output = fopen(outputname,"w");
    errorCheckNULL(output,"Cannot open file");    
#endif
 
    char *buffer = malloc(sizeof(char)*BUFF_SIZE);
    errorCheckNULL(buffer,"Out of memory");
    char *buffercpy = malloc(sizeof(char)*BUFF_SIZE);
    errorCheckNULL(buffercpy,"Out of memory");
    
    memset(mapq_array[devno],0,READS);  //can handle inside the loop as all reads are in the input sam and hence all un mapped will be cleared to 0
    
    size_t bufferSize = BUFF_SIZE;
    
    char * pch;
    int ret;
    int readlinebytes=0;
    long readID = 0;
    int read_array_index = 0;
    int flag = 0;
    int mapq;
        
    
    while(1){
        readlinebytes=getline(&buffer, &bufferSize, input); 
        //file has ended
        if(readlinebytes == -1){
            break;
        }        
        //errorCheck(ret,"File has prematurely ended");
        
        //ignore header lines
        if(buffer[0]=='@'){
            continue;
        }
        
        //copy buffer
        if(readlinebytes>=BUFF_SIZE){
            fprintf(stderr,"Need to implement realloc\n");
            exit(EXIT_FAILURE);
        }
        strcpy(buffercpy,buffer);
        
        //read ID (QNAME)
        pch = strtok (buffer,"\t\r\n"); 
        errorCheckNULL(pch,"A bad samfile. No QNAME in line?");
        //printf("%s\n",pch);
        ret=sscanf(pch,"%ld",&readID);
        errorCheckScanVal(ret,"Bad read ID format");
      
        //array index
        read_array_index = (int )(readID - STARTID);
        
        //FLAG
        
        pch = strtok (NULL,"\t\r\n");
        errorCheckNULL(pch,"A bad samfile. No FLAG in line?");
        ret=sscanf(pch,"%d",&flag);
        errorCheckScanVal(ret,"Bad read ID format");
        
        //if unmapped or secondary mapped or supplymentary mapped
        if((flag & 0x04) || (flag & 0x100) || (flag & 0x800)){
            //mapq_array[devno][read_array_index] = 0;
            continue;
        }

      
        //make efficient by scanning from back
      
        //RNAME
        pch = strtok (NULL,"\t\r\n"); 
        
        //POS
        pch = strtok (NULL,"\t\r\n"); 
        
        //MAPQ
        pch = strtok (NULL,"\t\r\n"); 
        //errorCheckNULL(pch,"A bad samfile. No MAPQ in line?");
        //ret=sscanf(pch,"%d",&mapq);
        //mapq is a star
        //if(ret!=1 || mapq >=255 || mapq<=0){
            //mapq_array[devno][read_array_index] = 0;
            //continue;           
        //}
        /*if(mapq >=255 || mapq<=0){
            mapq_array[devno][read_array_index] = 0;
            continue;           
        }*/
        
        //CIGAR
        pch = strtok (NULL,"\t\r\n");
        
        //RNEXT
        pch = strtok (NULL,"\t\r\n");
        
        //PNEXT
        pch = strtok (NULL,"\t\r\n");
        
        //TLEN
        pch = strtok (NULL,"\t\r\n");
        
        //SEQ
        pch = strtok (NULL,"\t\r\n");
        
        //QUAL
        pch = strtok (NULL,"\t\r\n");
        
        //NM
        pch = strtok (NULL,"\t\r\n");
        
        //MD
        pch = strtok (NULL,"\t\r\n");
        
        //alignment score
        pch = strtok (NULL,"\t\r\n");
        errorCheckNULL(pch,"A bad samfile. No Alignment Score in line?");
        ret=sscanf(pch,"AS:i:%d",&mapq);
        errorCheckScanVal(ret,"Bad alignment score format");
        if(mapq<0 || mapq>255){
            fprintf(stderr,"We have a problem. Alignment score has gone out of unsigned char limits\n");
            exit(EXIT_FAILURE);
        }
        
        //printf("Alignment score %s %d\n",pch,mapq);
        
        if(read_array_index>=READS){
            fprintf(stderr, "read_array_index is %d which is expected to be less than %d\n",read_array_index,READS);
        }
        
        mapq_array[devno][read_array_index] = (unsigned char)mapq;
      
#ifdef DEBUG 
        fprintf(output,"%s",buffercpy);  
#endif        
        
    } 

 
    //printf("%d %d %d %d\n", mapq_array[0][4179], mapq_array[1][4179], mapq_array[2][4179], mapq_array[3][4179]);
    
    free(buffer);
    free(buffercpy);
    fclose(input);
    
#ifdef DEBUG     
    fclose(output);
#endif 
    
}

void deduplicate(char *inputname, char *outputname){
    
    FILE *input = fopen(inputname,"r");
    errorCheckNULL(input,"Cannot open file");

    FILE *output = fopen(outputname,"w");
    errorCheckNULL(output,"Cannot open file");    
 
    char *buffer = malloc(sizeof(char)*BUFF_SIZE);
    errorCheckNULL(buffer,"Out of memory");
    char *buffercpy = malloc(sizeof(char)*BUFF_SIZE);
    errorCheckNULL(buffercpy,"Out of memory");
    

    size_t bufferSize = BUFF_SIZE;
    
    char * pch;
    int ret;
    int readlinebytes=0;
    long readID = 0;
    int read_array_index = 0;
    //int flag = 0;
    //int mapq;
    
    while(1){
        
        readlinebytes=getline(&buffer, &bufferSize, input); 
        //file has ended
        if(readlinebytes == -1){
            break;
        }            
        
        //ignore header lines  //empty new lines?
        if(buffer[0]=='@'){
            continue;
        }
        
        //copy buffer
        if(readlinebytes>=BUFF_SIZE){
            fprintf(stderr,"Need to implement realloc\n");
            exit(EXIT_FAILURE);
        }
        strcpy(buffercpy,buffer);
        
        //read ID (QNAME)
        pch = strtok (buffer,"\t\r\n"); 
        errorCheckNULL(pch,"A bad samfile. No QNAME in line?");
        //printf("%s\n",pch);
        ret=sscanf(pch,"%ld",&readID);
        errorCheckScanVal(ret,"Bad read ID format");
      
        //array index
        read_array_index = (int )(readID - STARTID);
        
        if(read_array_index>=READS){
            fprintf(stderr, "read_array_index is %d which is expected to be less than %d\n",read_array_index,READS);
        }
        
        //FLAG        
        //pch = strtok (NULL,"\t\r\n");

        //RNAME
        //pch = strtok (NULL,"\t\r\n"); 
        
        //POS
        //pch = strtok (NULL,"\t\r\n"); 
        
 
        
        unsigned char mapq = mapq_array[0][read_array_index];
        unsigned char mapq1 = mapq_array[1][read_array_index];
        unsigned char mapq2 = mapq_array[2][read_array_index];
        unsigned char mapq3 = mapq_array[3][read_array_index];
        if(mapq!=0 && mapq >= mapq1 && mapq >= mapq2 && mapq >= mapq3){     
            fprintf(output,"%s",buffercpy);  
        }
        
        //if(read_array_index==4179){
            //printf("%d %d %d %d\n",mapq,mapq1,mapq2,mapq3);
        //}
    }    
 
    
    free(buffer);
    free(buffercpy);
    fclose(input);
    fclose(output);
 
        
    
    
}

void *client_connection(void *arg)
{
	//sleep(1); //wait till servers are setup
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
        
        //char buffer[BUFF_SIZE]="Hi! I am the client. Serve me please!";
        
        //send the message
        //send_full_msg(socketfd, buffer, strlen(buffer));
        send_full_msg(socketfd, mapq_array[0], READS);
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
	//sleep(1); //not needed
    
    int *arguments  = (int *)arg;
    int devno = arguments[0];
    int connectfd = arguments[1];
    
    //char buffer[BUFFER_SIZE];   
    
    //this keeps on listening
 
    int i=0;
    for(i=0;i<LOOPS;i++){
        //get message from client
        recv_full_msg(connectfd,mapq_array[devno],READS);    
        
        //print the message
        //buffer[received]='\0'; //null character before priniting the stringsince
        //fprintf(stderr,"Recieved : %s\n",buffer); 
     
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

