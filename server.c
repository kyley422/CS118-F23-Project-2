#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/param.h>

#include "utils.h"

#define WINDOW_SIZE 4

typedef struct {
    struct packet pkt;
    bool ack_sent;
    bool written_to_file;
} Frame;

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    socklen_t addr_size = sizeof(client_addr_from);
    struct packet ack_pkt;

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the server address
    if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Configure the client address structure to which we will send ACKs
    memset(&client_addr_to, 0, sizeof(client_addr_to));
    client_addr_to.sin_family = AF_INET;
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    client_addr_to.sin_port = htons(CLIENT_PORT_TO);

    // Open the target file for writing (always write to output.txt)
    FILE *fp = fopen("output.txt", "wb");
    if (fp == NULL) {
        perror("Error opening file");
        return 1;
    }

    Frame frames[WINDOW_SIZE];
    int base = 0;
    // Most recently ACKed packet
    int last_successful_ack = 0;
    int next_seq_num = 0;
    bool transmission_began = false;
    bool transmission_complete = false;

    // Initialize array to empty
    for (int i = 0; i < WINDOW_SIZE; i++) {
        frames[i].pkt.seqnum = -1;
    }

    while (!transmission_complete) {
        // Receive packets for the current window
        while (1) {
            struct timeval timeout = {1, 0}; // Set timeout

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(listen_sockfd, &readfds);

            int activity = select(listen_sockfd + 1, &readfds, NULL, NULL, &timeout);

            if (activity == -1) {
                perror("Error in select");
                return -1;
            } 
            else if (activity == 0) {
                // if (transmission_began) {
                //     printf("Timeout occurred. Resending ACKs...\n");
                //     // Send last successful ACK
                //     build_packet(&ack_pkt, 0, last_successful_ack, 0, 1, 1, "0");
                //     if (sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_to, addr_size) < 0) {
                //         perror("Error sending ACK\n");
                //         return -1;
                //     }
                //     printf("Re:ACK pkt #%d\n", last_successful_ack);
                // }
            } 
            else {
                struct packet received_pkt;
                if ((recvfrom(listen_sockfd, (char *)&received_pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_from, &addr_size)) < 0) {
                    printf("Unable to receive client message\n");
                    return -1;
                }
                transmission_began = true;
                
                // Don't add packet to window if duplicate
                // printf("BASE: %u\n", base);
                // printf("RECEIVED PKT #%d\n", received_pkt.seqnum);
                // Check if packet is last packet
                if(received_pkt.last == 1) {
                    // printf("TRANSMISSION COMPLETE\n");
                    fclose(fp);
                    close(listen_sockfd);
                    close(send_sockfd);
                    return 0;
                }
                // Check if packet is outside of window
                if (received_pkt.seqnum >= base && received_pkt.seqnum < base + WINDOW_SIZE) {
                    // Add packet to empty spot in window
                    if (1) {
                        for (int i = 0; i < WINDOW_SIZE; ++i) {
                            if (frames[i].pkt.seqnum == -1) {
                                // printf("PKT #%d added @ pos %d\n", received_pkt.seqnum, i);
                                frames[i].pkt = received_pkt;
                                frames[i].ack_sent = true;
                                frames[i].written_to_file = false;
                                break;
                            }
                        }

                        // Write newly if possible: if newly arrived packet arrives at index base
                        bool has_written = false;
                        do {
                            for (int i = 0; i < WINDOW_SIZE; ++i) {
                                if (frames[i].pkt.seqnum == next_seq_num) {
                                    // fprintf(fp, "=====Begin Packet %d=====\n", frames[i].pkt.seqnum);
                                    fwrite(frames[i].pkt.payload, 1, frames[i].pkt.length, fp);
                                    // fprintf(fp, "\n=====End Packet %d=====\n", frames[i].pkt.seqnum);
                                    // printf("Writing to file: Pkt #%d\n", frames[i].pkt.seqnum);
                                    next_seq_num++;
                                    frames[i].pkt.seqnum = -1;
                                    has_written = true;
                                    break;
                                }
                                has_written = false;
                            }
                        } while (has_written);

                        // ACK Packet
                        build_packet(&ack_pkt, 0, next_seq_num-1, 0, 1, 1, "0");
                        if (sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_to, addr_size) < 0) {
                            perror("Error sending ACK\n");
                            return -1;
                        }
                        // printf("ACK pkt #%d\n", ack_pkt.acknum);
                        last_successful_ack = ack_pkt.acknum;
                        next_seq_num = last_successful_ack + 1;
                        base = next_seq_num;
                        
                    }
                }
                else if (received_pkt.seqnum < base) {
                    // printf("Dropped duplicate packet %d\n", received_pkt.seqnum);
                    // Send ACK for out of bounds receive: ACK last successful
                    build_packet(&ack_pkt, 0, last_successful_ack, 0, 1, 1, "0");
                    if (sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_to, addr_size) < 0) {
                        perror("Error sending ACK\n");
                        return -1;
                    }
                    // printf("ACK pkt #%d\n", last_successful_ack);
                }
                else {
                    // Send ACK for out of bounds receive: ACK last successful
                    build_packet(&ack_pkt, 0, last_successful_ack, 0, 1, 1, "0");
                    if (sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_to, addr_size) < 0) {
                        perror("Error sending ACK\n");
                        return -1;
                    }
                    // printf("ACK pkt #%d\n", last_successful_ack);
                }
                // printf("NEXT EXPECTED SEQ #:%d\n", next_seq_num);
                // printf("==========Window==========\n");
                // printf("Frame:\n");
                // for (int i = 0; i < WINDOW_SIZE; i++) {
                //     printf("%d,", frames[i].pkt.seqnum);
                // }
                // printf("\n");
            }
        }
    }
}
