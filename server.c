#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/param.h>

#include "utils.h"

#define WINDOW_SIZE 4 // Adjust the window size as needed

typedef struct {
    struct packet pkt;
    bool ack_received;
    bool written_to_file;
} Frame;

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    struct packet buffer;
    socklen_t addr_size = sizeof(client_addr_from);
    int expected_seq_num = 0;
    int recv_len;
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
    int next_seq_num = 0;
    bool transmission_complete = false;

    while (!transmission_complete) {
        // Receive packets for the current window
        while (next_seq_num - base < WINDOW_SIZE) {
            struct timeval timeout = {5, 0}; // Set timeout for 5 seconds

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(listen_sockfd, &readfds);

            int activity = select(listen_sockfd + 1, &readfds, NULL, NULL, &timeout);

            if (activity == -1) {
                perror("Error in select");
                return -1;
            } else if (activity == 0) {
                printf("Timeout occurred. Resending ACKs...\n");

                // Resend ACKs for all packets in the window
                for (int i = base; i < next_seq_num; ++i) {
                    if (!frames[i % WINDOW_SIZE].ack_received) {
                        build_packet(&frames[i % WINDOW_SIZE].pkt, 0, frames[i % WINDOW_SIZE].pkt.seqnum, 0, 1, 1, "0");
                        if (sendto(send_sockfd, &frames[i % WINDOW_SIZE].pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_to, addr_size) < 0) {
                            perror("Error sending ACK\n");
                            return -1;
                        }

                        printf("Resent ACK for pkt #%d\n", frames[i % WINDOW_SIZE].pkt.seqnum);
                    }
                }
            } else {
                struct packet received_pkt;
                if ((recvfrom(listen_sockfd, (char *)&received_pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_from, &addr_size)) < 0) {
                    printf("Unable to receive client message\n");
                    return -1;
                }
                
                // Don't add packet to window if duplicate
                printf("RECEIVED PKT #:%d\n", received_pkt.seqnum);
                printf("NEXT EXPECTED SEQ #:%d\n", next_seq_num);
                frames[received_pkt.seqnum % WINDOW_SIZE].pkt = received_pkt;
                frames[received_pkt.seqnum % WINDOW_SIZE].ack_received = true;
                frames[received_pkt.seqnum % WINDOW_SIZE].written_to_file = false;
                next_seq_num++;
            }
        }
        
        Frame frames_copy[WINDOW_SIZE] = {0};
        memcpy(frames_copy, frames, sizeof(frames));

        // Send ACKs for received packets
        for (int i = base; i < next_seq_num; ++i) {
            build_packet(&frames_copy[i % WINDOW_SIZE].pkt, 0, MIN(frames_copy[i % WINDOW_SIZE].pkt.seqnum, next_seq_num), 0, 1, 1, "0");
            if (sendto(send_sockfd, &frames_copy[i % WINDOW_SIZE].pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_to, addr_size) < 0) {
                perror("Error sending ACK\n");
                return -1;
            }
            printf("ACK pkt #%d\n", frames[i % WINDOW_SIZE].pkt.seqnum);
        }
        printf("==========Window==========\n");
        printf("Frame:\n");
        for (int j = base; j < next_seq_num; ++j) {
            printf("%d,", frames[j % WINDOW_SIZE].pkt.seqnum);
            // printf("Payload: %s\n", frames[i].pkt.payload);
        }
        printf("\n");

        // Check if all packets in the window have been received and write to file
        bool all_received = true;
        for (int i = base; i < next_seq_num; ++i) {
            if (!frames[i % WINDOW_SIZE].ack_received) {
                all_received = false;
                break;
            }
        }

        if (all_received) {
            for (int i = base; i < next_seq_num; ++i) {
                if (!frames[i % WINDOW_SIZE].written_to_file) {
                    fwrite(frames[i % WINDOW_SIZE].pkt.payload, 1, frames[i % WINDOW_SIZE].pkt.length, fp);
                    frames[i % WINDOW_SIZE].written_to_file = true;
                    printf("Writing to file: Pkt #%d\n", frames[i % WINDOW_SIZE].pkt.seqnum);
                }
            }
            base = next_seq_num;
        }

        // Check for completion
        if (frames[(next_seq_num - 1) % WINDOW_SIZE].pkt.last == 1) {
            transmission_complete = true;
        }
    }

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
