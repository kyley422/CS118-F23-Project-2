#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#include "utils.h"

#define WINDOW_SIZE 4


typedef struct {
    struct packet pkt;
    bool ack_received;
    bool is_read;
} Frame;

void send_packet(int send_sockfd, struct packet *pkt, struct sockaddr_in *server_addr_to, socklen_t addr_size) {
    sendto(send_sockfd, pkt, sizeof(struct packet), 0, (struct sockaddr *)server_addr_to, addr_size);
}

int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
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
    }

    while (1) {
        if (seq_num != 0) {
        // Receive acknowledgments
          while (recvfrom(listen_sockfd, &ack_pkt, sizeof(ack_pkt), 0, NULL, NULL) >= 0) {
            // Check if the acknowledgment is for a packet in the window
            // printf("Received ACK#: %u\n", ack_pkt.acknum);
              if (ack_pkt.acknum == expected_ack_num) {
                printf("Recieved ACK:%d same as Expected ACK:%d\n", ack_pkt.acknum, expected_ack_num);
                // Successful ACK advance by window by 1
                frames[(ack_pkt.acknum) % WINDOW_SIZE].pkt.seqnum = -1;
                expected_ack_num++;

                for (int i=0; i<WINDOW_SIZE; ++i) {
                    printf("%d,", frames[i].pkt.seqnum);
                }
                printf("\n");
                break;

              } else if (ack_pkt.acknum >= expected_ack_num) {
                printf("Recieved ACK:%d > Expected ACK:%d\n", ack_pkt.acknum, expected_ack_num);
                for (int i=0; i<WINDOW_SIZE; ++i) {
                    if (frames[i].pkt.seqnum <= ack_pkt.acknum)
                        frames[i].pkt.seqnum = -1;
                }
                expected_ack_num = ack_pkt.acknum+1;

                for (int i=0; i<WINDOW_SIZE; ++i) {
                    printf("%d,", frames[i].pkt.seqnum);
                }
                printf("\n");
                break;
              }
              else {
                printf("Recieved ACK:%d <  Expected ACK%d\n", ack_pkt.acknum, expected_ack_num);
                build_packet(&pkt, ack_pkt.acknum+1, ack_num, 0, 0, frames[(ack_pkt.acknum+1 - base) % WINDOW_SIZE].pkt.length, frames[(ack_pkt.acknum+1 - base) % WINDOW_SIZE].pkt.payload);
                send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);
                for (int i=0; i<WINDOW_SIZE; ++i) {
                    printf("%d,", frames[i].pkt.seqnum);
                }
                printf("\n");
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
                frames[(seq_num - base) % WINDOW_SIZE].ack_received = false;
                frames[(seq_num - base) % WINDOW_SIZE].is_read = true;
                
                send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);

                if (bytesRead == 0) {
                    build_packet(&pkt, seq_num, ack_num, 1, 0, bytesRead, buffer);
                    send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);
                    break;
                }
                seq_num++;
            }
        }
        // ACK Timeout
        // tv.tv_sec = TIMEOUT_SEC;
        // tv.tv_usec = 0;
        // setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(struct timeval));
        if (feof(fp)) {
            break;
        }
    }
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

