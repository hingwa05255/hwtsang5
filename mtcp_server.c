#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mtcp_server.h"
#define SYN 0
#define SYNACK 1
#define FIN 2
#define FINACK 3
#define ACK 4
#define DATA 5

/* ThreadID for Sending Thread and Receiving Thread */
static pthread_t send_thread_pid;
static pthread_t recv_thread_pid;

static pthread_cond_t app_thread_sig = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t app_thread_sig_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t send_thread_sig = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t send_thread_sig_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t info_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t end_thread_sig = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t end_thread_sig_mutex = PTHREAD_MUTEX_INITIALIZER;

unsigned int seq = 0;
unsigned int ACKNO = 0 ;
unsigned int mode;
unsigned char buffer[4];
unsigned char packet[1004];
unsigned char receivebuff[1000];
struct sockaddr_in global_server_addr;
int global_sock_fd;
socklen_t addrLen;

/* The Sending Thread and Receive Thread Function */
static void *send_thread();
static void *receive_thread();

void mtcp_accept(int socket_fd, struct sockaddr_in *server_addr){
	pthread_mutex_lock(&info_mutex);
	global_sock_fd = socket_fd;
	addrLen = sizeof(global_server_addr);
	pthread_mutex_unlock(&info_mutex);
	global_server_addr.sin_family = server_addr->sin_family;
	global_server_addr.sin_port = server_addr->sin_port;
	global_server_addr.sin_addr = server_addr->sin_addr;
	pthread_create(&recv_thread_pid, NULL, receive_thread, NULL);
	pthread_create(&send_thread_pid, NULL, send_thread, NULL);
	pthread_cond_wait(&app_thread_sig, &app_thread_sig_mutex);
}



int mtcp_read(int socket_fd, unsigned char *buf, int buf_len){
	int i;
	int len;
	pthread_cond_wait(&app_thread_sig, &app_thread_sig_mutex);

	pthread_mutex_lock(&info_mutex);
	printf("Reading data from the buffer...\n");
	for (i = 0; i < 1000; i++){
		if(receivebuff[i] == '\0')
			break;
		else
			buf[i] = receivebuff[i];
	}
	pthread_mutex_unlock(&info_mutex);
	return i;
}



void mtcp_close(int socket_fd){
	pthread_cond_wait(&end_thread_sig, &end_thread_sig_mutex);
	pthread_join(recv_thread_pid,NULL);
	pthread_join(send_thread_pid,NULL);
}



static void *send_thread(){
	while(1){
		int i;
		pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex);
		pthread_mutex_lock(&info_mutex);

		if(mode == SYN){
			seq = 1;         // seq = y
			mode = SYNACK;
			seq = htonl(seq);
			memcpy(buffer, &seq, 4);
			buffer[0] = buffer[0] | (mode << 4);
			for (i = 0; i < 4; ++i){
					packet[i] = buffer[i];
			}
			sendto(global_sock_fd,packet,sizeof(packet),0,(struct sockaddr*)&global_server_addr, addrLen);
			printf("send SYNACK\n" );
		}

		if(mode == DATA){
			mode = ACK;
			seq = htonl(seq);
			memcpy(buffer, &seq, 4);
			buffer[0] = buffer[0] | (mode << 4);
			for (i = 0; i < 4; ++i){
					packet[i] = buffer[i];
			}
			sendto(global_sock_fd,packet,sizeof(packet),0,(struct sockaddr*)&global_server_addr, addrLen);
			printf("send ACK\n");
			sleep(1);
			pthread_cond_signal(&app_thread_sig);
			pthread_mutex_unlock(&info_mutex);
		}

		if(mode == FIN){
			mode = FINACK;
			seq = htonl(seq);
			memcpy(buffer, &seq, 4);
			buffer[0] = buffer[0] | (mode << 4);
			for (i = 0; i < 4; ++i){
					packet[i] = buffer[i];
			}
			sendto(global_sock_fd,packet,sizeof(packet),0,(struct sockaddr*)&global_server_addr, addrLen);
			printf("send FINACK\n");
			sleep(1);
		}

			pthread_mutex_unlock(&send_thread_sig_mutex);
			pthread_mutex_unlock(&info_mutex);
		}
}



static void *receive_thread(){
	while(1){
		int i;
		int len = 0;

		recvfrom(global_sock_fd, packet, sizeof(packet), 0, (struct sockaddr*)&global_server_addr, &addrLen);
		pthread_mutex_lock(&info_mutex);
		for (i = 0; i < 4; ++i){
				buffer[i] = packet[i];
		}

		mode = buffer[0] >> 4;
		buffer[0] = buffer[0] & 0x0F;
		memcpy(&seq, buffer, 4);
		seq = ntohl(seq);

		if(mode == SYN){
			printf("received SYN\n");
			pthread_mutex_unlock(&info_mutex);
			pthread_cond_signal(&send_thread_sig);
		}

		if(mode == ACK && seq == 1){
			printf("received ACK\n");
			pthread_cond_signal(&app_thread_sig);
			pthread_mutex_unlock(&info_mutex);
		}

		if(mode == DATA){
			printf("receive DATA\n");
			for (i = 4; i < 1004; i++){
				if(packet[i] == '\0'){
					break;
				}else{
					receivebuff[i-4] = packet[i];   // save data into receive buffer	
					len++;
				}
			}
			printf("len = %d\n", len );
			seq = seq + len; 	//ACK = seq +lens
			printf("ACK = %d \n" ,seq);
			pthread_mutex_unlock(&info_mutex);
			pthread_cond_signal(&send_thread_sig);
		}

		if(mode == FIN){
			printf("receive FIN\n");
			seq = seq + 1; // ACK = n+1 (FIN ACK)
			pthread_mutex_unlock(&info_mutex);
			pthread_cond_signal(&send_thread_sig);
		}

		if(mode == ACK && seq != 1){
			printf("receive Final ACK\n");
			pthread_mutex_unlock(&info_mutex);
			pthread_cond_signal(&end_thread_sig);
			exit(1);
		}

	}

}