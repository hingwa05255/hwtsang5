#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "mtcp_client.h"
#define SYN 0
#define SYNACK 1
#define FIN 2
#define FINACK 3
#define ACK 4
#define DATA 5

/* -------------------- Global Variables -------------------- */
unsigned int seq = 0;
unsigned int mode;
unsigned char buffer[4];
unsigned char packet[1004];
unsigned char remain[24];
unsigned int lastseq = 1;
unsigned int lastbuf_len = 0;
unsigned int char_sent = 0;
unsigned int char_remain = 0;
int global_sock_fd;
socklen_t addrLen;
struct sockaddr_in global_server_addr;
/* ThreadID for Sending Thread and Receiving Thread */
static pthread_t send_thread_pid;
static pthread_t recv_thread_pid;

static pthread_cond_t app_thread_sig = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t app_thread_sig_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t send_thread_sig = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t send_thread_sig_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t info_mutex = PTHREAD_MUTEX_INITIALIZER;


static void *send_thread();
static void *receive_thread();

/* Connect Function Call (mtcp Version) */
void mtcp_connect(int socket_fd, struct sockaddr_in *server_addr){
	int i;
	pthread_mutex_lock(&info_mutex);
	global_sock_fd = socket_fd;
	addrLen = sizeof(global_server_addr);
	pthread_mutex_unlock(&info_mutex);
	global_server_addr.sin_family = server_addr->sin_family;
	global_server_addr.sin_port = server_addr->sin_port;
	global_server_addr.sin_addr = server_addr->sin_addr;
	pthread_create(&recv_thread_pid, NULL, receive_thread,NULL);
	pthread_create(&send_thread_pid, NULL, send_thread, NULL);
	pthread_mutex_lock(&send_thread_sig_mutex);
	sleep(1);
	pthread_cond_signal(&send_thread_sig);
	pthread_mutex_unlock(&send_thread_sig_mutex);
	pthread_cond_wait(&app_thread_sig, &app_thread_sig_mutex);
}

/* Write Function Call (mtcp Version) */
int mtcp_write(int socket_fd, unsigned char *buf, int buf_len){
	printf("Write the data to the packet...\n");
	int i = 4;
	int j;
	int len;
	pthread_mutex_lock(&info_mutex);

	for(j = char_sent; j < buf_len; j++){
		packet[i] = buf[j];    // data body
		i++;
		char_sent++;
		if(buf[j] == '\0'){
			char_sent = 0;
			break;
		}
		if(i == 1004){
			i = 4;
			break;
		}
	}
	if(char_sent > 1000){
		char_remain = 1024 - char_sent;
		for(j = 0; j < char_remain; j++)
			remain[j] = buf[1000+j];
		for(j = 0; j < char_remain; j++)
			printf("%c", remain[j]);
		printf("\n\n");
	}

	mode = DATA;
	seq = htonl(seq);     // seq = last data length + last seq
	memcpy(buffer, &seq, 4);
	buffer[0] = buffer[0] | (mode << 4);
	for (i = 0; i < 4; i++){
		packet[i] = buffer[i];  // header
	}
	pthread_mutex_unlock(&info_mutex);
	pthread_cond_signal(&send_thread_sig);
	return j; //return body len
}


/* Close Function Call (mtcp Version) */
void mtcp_close(int socket_fd){
	sleep(2);  //!!!!!!PROBLEM
	pthread_mutex_lock(&info_mutex);
	mode = FIN;
	pthread_mutex_unlock(&info_mutex);
	pthread_cond_signal(&send_thread_sig);
	pthread_cond_wait(&app_thread_sig, &app_thread_sig_mutex);
	pthread_join(recv_thread_pid,NULL);
	pthread_join(send_thread_pid, NULL);
}

static void *send_thread(){
while(1){
	int i;
	pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex);

	pthread_mutex_lock(&info_mutex);

	if(mode == NULL){
		mode = SYN;
		seq = htonl(seq);     //seq = x
		memcpy(buffer, &seq, 4);
		buffer[0] = buffer[0] | (mode << 4);
		for (i = 0; i < 4; ++i)
		{
			packet[i] = buffer[i];
		}
		sendto(global_sock_fd,packet,sizeof(packet),0,(struct sockaddr*)&global_server_addr, addrLen);
		printf("send SYN\n" );
	}

	if(mode == SYNACK){
		seq = 1;  // seq = x + 1
		mode = ACK;
		seq = htonl(seq);
		memcpy(buffer, &seq, 4);
		buffer[0] = buffer[0] | (mode << 4);
			for (i = 0; i < 4; ++i)
		{
			packet[i] = buffer[i];
		}
		sendto(global_sock_fd, packet, sizeof(packet),0, (struct sockaddr*)&global_server_addr, addrLen);
		seq = 1; // change to int from nl
		printf("send ACK\n");
		pthread_mutex_unlock(&info_mutex);
		pthread_cond_signal(&app_thread_sig);
	}

	if(mode == DATA){
		int len = sendto(global_sock_fd, packet, sizeof(packet),0, (struct sockaddr*)&global_server_addr, addrLen);
		printf("send DATA \n");
	}

	if (mode == FIN)
	{
		seq = htonl(seq);
		memcpy(buffer, &seq, 4);
		buffer[0] = buffer[0] | (mode << 4);
		for (i = 0; i < 4; ++i)
		{
			packet[i] = buffer[i];
		}
		sendto(global_sock_fd, packet, sizeof(packet),0, (struct sockaddr*)&global_server_addr, addrLen);
		printf("send FIN\n");
	}

	if(mode == FINACK){
		mode = ACK;
		seq = htonl(seq);
		memcpy(buffer, &seq, 4);
		buffer[0] = buffer[0] | (mode << 4);
		for (i = 0; i < 4; ++i)
		{
			packet[i] = buffer[i];
		}
		sendto(global_sock_fd, packet, sizeof(packet),0, (struct sockaddr*)&global_server_addr, addrLen);
		printf("send FINAL ACK\n");
		pthread_mutex_unlock(&info_mutex);
		pthread_cond_signal(&app_thread_sig);
		exit(1);
		}
		pthread_mutex_unlock(&info_mutex);
		pthread_mutex_unlock(&send_thread_sig_mutex);
		
		
	}

}

static void *receive_thread(){
	while(1){
		int i;
		sleep(1);
	//	clock_t start = clock();
	int len = recvfrom(global_sock_fd, packet, sizeof(packet), 0, (struct sockaddr*)&global_server_addr, &addrLen);
/*		clock_t stop = clock();
		double elapsed = (double)(stop - start) * 1000.0 / CLOCKS_PER_SEC;
		if(elapsed >= 1){

		}
    printf("Time elapsed in ms: %f\n", elapsed); */
		pthread_mutex_lock(&info_mutex);
		for (i = 0; i < 4; ++i){
			buffer[i] = packet[i];
		} 
		mode = buffer[0] >> 4;
		buffer[0] = buffer[0] & 0x0F;
		memcpy(&seq, buffer, 4);
		seq = ntohl(seq);
		if(mode == SYNACK){
			printf("receive SYNACK\n" );
			pthread_mutex_unlock(&info_mutex);
			pthread_cond_signal(&send_thread_sig);
			}
		if(mode == ACK){
			pthread_mutex_unlock(&info_mutex);
			pthread_cond_signal(&send_thread_sig);
		}
		if(mode == FINACK){
			printf("receive FINACK\n");
			pthread_mutex_unlock(&info_mutex);
			pthread_cond_signal(&send_thread_sig);
		}
	}
}
