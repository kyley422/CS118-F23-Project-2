#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "utils.h"

#define WINDOW_SIZE 5
#define TIMEOUT_SEC 1

struct packet window[WINDOW_SIZE];
time_t sent_times[WINDOW_SIZE];

// Function to send a packet to the server
void send_packet(int send_sockfd, struct packet *pkt, struct sockaddr_in *server_addr_to, socklen_t addr_size) {
    sendto(send_sockfd, pkt, sizeof(struct packet), 0, (struct sockaddr *)server_addr_to, addr_size);
}

// Function to retransmit unacknowledged packets in the window
void retransmit_packets(int send_sockfd, struct sockaddr_in *server_addr_to, socklen_t addr_size, int *seq_num) {
    for (int i = 0; i < WINDOW_SIZE; ++i) {
        if (window[i].seqnum != 0 && time(NULL) - sent_times[i] >= TIMEOUT_SEC) {
            // Retransmit the packet if not acknowledged within the timeout
            send_packet(send_sockfd, &window[i], server_addr_to, addr_size);
            sent_times[i] = time(NULL);  // Update the sent time
        }
    }
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
    char last = 0;
    char ack = 0;

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

    // Initialize the sliding window and sent times
    memset(window, 0, sizeof(window));
    memset(sent_times, 0, sizeof(sent_times));

    while (1) {
        for (int i = 0; i < WINDOW_SIZE; ++i) {
            if (window[i].seqnum == 0) {
                size_t bytesRead = fread(buffer, 1, PAYLOAD_SIZE, fp);
                
                build_packet(&pkt, seq_num, ack_num, 0, 0, bytesRead, buffer);
                send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);

                if (bytesRead == 0) {
                    build_packet(&pkt, seq_num, ack_num, 1, 0, bytesRead, buffer);
                    send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);
                    break;
                }

                window[i] = pkt;
                sent_times[i] = time(NULL);

                seq_num++;
            }
        }
        // ACK Timeout
        tv.tv_sec = TIMEOUT_SEC;
        tv.tv_usec = 0;
        setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(struct timeval));

        // Receive acknowledgments
        while (recvfrom(listen_sockfd, &ack_pkt, sizeof(ack_pkt), 0, NULL, NULL) >= 0) {
            // Check if the acknowledgment is for a packet in the window
            printf("Received ACK# %u\n", ack_pkt.acknum);
            for (int i = 0; i < WINDOW_SIZE; ++i) {
                if (window[i].seqnum == ack_pkt.acknum) {
                    // Remove the acknowledged packet from the window
                    window[i].seqnum = 0;
                    window[i].acknum = 0;
                    memset(window[i].payload, 0, PAYLOAD_SIZE);
                }
            }
        }

        // Check if all packets in the window are acknowledged
        int allAcknowledged = 1;
        for (int i = 0; i < WINDOW_SIZE; ++i) {
            if (window[i].seqnum != 0) {
                allAcknowledged = 0;
                break;
            }
        }

        if (!allAcknowledged) {
            retransmit_packets(send_sockfd, &server_addr_to, addr_size, &seq_num);
        }

        if (allAcknowledged && feof(fp)) {
            break;
        }
    }


    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

