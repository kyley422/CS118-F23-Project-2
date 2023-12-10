#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <time.h>

#include "utils.h"

#define WINDOW_SIZE 4
#define TIMEOUT 0.0005

typedef struct {
    struct packet pkt;
    double sent_time;
} Frame;

void send_packet(int send_sockfd, struct packet *pkt, struct sockaddr_in *server_addr_to, socklen_t addr_size) {
    sendto(send_sockfd, pkt, sizeof(struct packet), 0, (struct sockaddr *)server_addr_to, addr_size);
}

int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to;
    // server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval tv;
    struct packet pkt;
    struct packet ack_pkt;
    char buffer[PAYLOAD_SIZE];
    unsigned short seq_num = 0;
    unsigned short ack_num = 0;

    // read filename from command line argument
    if (argc != 2) {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT_TO);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the client address
    if (bind(listen_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }

    Frame frames[WINDOW_SIZE];
    int base = 0;
    int expected_ack_num = 0;

    // Initialize array to empty
    for (int i = 0; i < WINDOW_SIZE; i++) {
        frames[i].pkt.seqnum = -1;
        frames[i].sent_time = -1;
    }

    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(struct timeval));

    // Begin system clock
    clock_t begin = clock();
    while (1) {

        clock_t end = clock();
        for (int i=0; i<WINDOW_SIZE; ++i) {
            // printf("RTT: %f\n", ((double)(end - begin) / CLOCKS_PER_SEC) - frames[i].sent_time);
            if (frames[i].sent_time != -1 && ((double)(end - begin) / CLOCKS_PER_SEC) - frames[i].sent_time >= TIMEOUT) {
                // printf("==========Timeout detected=========\n");
                build_packet(&pkt, frames[i].pkt.seqnum, ack_num, 0, 0, frames[i].pkt.length, frames[i].pkt.payload);
                send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);

                frames[i].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;
            }
        }

        

        if (seq_num != 0) {
            // printf("Recieved Seq#: %d\n", seq_num);
        // Receive acknowledgments
          while (recvfrom(listen_sockfd, &ack_pkt, sizeof(ack_pkt), 0, NULL, NULL) >= 0) {
            // Check if the acknowledgment is for a packet in the window
                // printf("Received ACK#: %u\n", ack_pkt.acknum);
                if (ack_pkt.acknum == expected_ack_num) {
                // printf("Recieved ACK:%d same as Expected ACK:%d\n", ack_pkt.acknum, expected_ack_num);
                // Successful ACK advance by window by 1
                frames[(ack_pkt.acknum) % WINDOW_SIZE].pkt.seqnum = -1;
                frames[(ack_pkt.acknum) % WINDOW_SIZE].sent_time = -1;
                expected_ack_num++;

                // for (int i=0; i<WINDOW_SIZE; ++i) {
                //     printf("%d,", frames[i].pkt.seqnum);
                // }
                // printf("\n");
                // for (int i=0; i<WINDOW_SIZE; ++i) {
                //     printf("%f,", frames[i].sent_time);
                // }
                // printf("\n");
                break;

              } else if (ack_pkt.acknum >= expected_ack_num) {
                // printf("Recieved ACK:%d > Expected ACK:%d\n", ack_pkt.acknum, expected_ack_num);
                for (int i=0; i<WINDOW_SIZE; ++i) {
                    if (frames[i].pkt.seqnum <= ack_pkt.acknum) {
                        frames[i].pkt.seqnum = -1;
                        frames[i].sent_time = -1;
                    }
                }
                expected_ack_num = ack_pkt.acknum+1;

                // for (int i=0; i<WINDOW_SIZE; ++i) {
                //     printf("%d,", frames[i].pkt.seqnum);
                // }
                // printf("\n");
                // for (int i=0; i<WINDOW_SIZE; ++i) {
                //     printf("%f,", frames[i].sent_time);
                // }
                // printf("\n");
                break;
              }
              else {
                // printf("Recieved ACK:%d <  Expected ACK%d\n", ack_pkt.acknum, expected_ack_num);
                build_packet(&pkt, ack_pkt.acknum+1, ack_num, 0, 0, frames[(ack_pkt.acknum+1 - base) % WINDOW_SIZE].pkt.length, frames[(ack_pkt.acknum+1 - base) % WINDOW_SIZE].pkt.payload);
                send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);
                
                clock_t end = clock();
                frames[(ack_pkt.acknum + 1 - base) % WINDOW_SIZE].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;

                // for (int i=0; i<WINDOW_SIZE; ++i) {
                //     printf("%d,", frames[i].pkt.seqnum);
                // }
                // printf("\n");
                // for (int i=0; i<WINDOW_SIZE; ++i) {
                //     printf("%f,", frames[i].sent_time);
                // }
                // printf("\n");
                break;
              }
            }
        }

        
        for (int i = 0; i < WINDOW_SIZE; ++i) {
            if (frames[i % WINDOW_SIZE].pkt.seqnum == -1) { 
                size_t bytesRead = fread(buffer, 1, PAYLOAD_SIZE, fp);
                // printf("sending seq#: %d, entry:%d\n", seq_num, frames[(seq_num - base) % WINDOW_SIZE].pkt.seqnum);

                build_packet(&pkt, seq_num, ack_num, 0, 0, bytesRead, buffer);
                frames[(seq_num - base) % WINDOW_SIZE].pkt = pkt;
                
                send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);
                clock_t end = clock();
                frames[(seq_num - base) % WINDOW_SIZE].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;


                // if (bytesRead == 0) {
                //     build_packet(&pkt, seq_num, ack_num, 1, 0, bytesRead, buffer);
                //     frames[(seq_num - base) % WINDOW_SIZE].pkt = pkt;

                //     send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);
                //     clock_t end = clock();
                //     frames[(seq_num - base) % WINDOW_SIZE].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;

                //     break;
                // }
                seq_num++;
            }
        }
        // ACK Timeout
        // tv.tv_sec = TIMEOUT_SEC;
        // tv.tv_usec = 0;
        // setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(struct timeval));
        if (feof(fp)) {
            build_packet(&pkt, seq_num, 0, 1, 0, 0, buffer);
            frames[(seq_num - base) % WINDOW_SIZE].pkt = pkt;

            sendto(send_sockfd, &pkt, sizeof(struct packet), 0, (struct sockaddr *)&server_addr_to, addr_size);
            clock_t end = clock();
            frames[(seq_num - base) % WINDOW_SIZE].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;

            while (1) {
                if (((double)(end - begin) / CLOCKS_PER_SEC) - frames[(seq_num - base) % WINDOW_SIZE].sent_time >= TIMEOUT) {
                    // printf("IM IN FIRST\n");
                    sendto(send_sockfd, &pkt, sizeof(struct packet), 0, (struct sockaddr *)&server_addr_to, addr_size);
                    clock_t end = clock();
                    frames[(seq_num - base) % WINDOW_SIZE].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;
                }
                if (recvfrom(listen_sockfd, &ack_pkt, sizeof(ack_pkt), 0, NULL, NULL) >= 0) {
                    // printf("Im in second: ack%d, seq%d\n", ack_pkt.acknum, seq_num);
                    fclose(fp);
                    close(listen_sockfd);
                    close(send_sockfd);
                    return 0;
                    
                }
            }
            
        }

    }
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

