#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#include "utils.h"

#define WINDOW_SIZE 4
#define TIMEOUT_SEC 1

int main(int argc, char *argv[]) {
    int send_sockfd;
    struct sockaddr_in server_addr_to;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval tv;
    struct packet pkt;
    struct packet ack_pkt;
    char buffer[PAYLOAD_SIZE];
    unsigned short seq_num = 0;

    // read filename from the command line argument
    if (argc != 2) {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Configure the server address structure to which we will send data
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Open file for reading
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(send_sockfd);
        return 1;
    }

    bool ACKed = 1;

    while (1) 
    {
        for (int i = 0; i < WINDOW_SIZE; ++i) 
        {
            size_t bytesRead = fread(buffer, 1, PAYLOAD_SIZE, fp);
            build_packet(&pkt, seq_num, 0, 0, 0, bytesRead, buffer);
            sendto(send_sockfd, &pkt, sizeof(struct packet), 0, (struct sockaddr *)&server_addr_to, addr_size);
            seq_num++;
        }

        tv.tv_sec = TIMEOUT_SEC;
        tv.tv_usec = 0;
        setsockopt(send_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(struct timeval));

        while (recvfrom(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, NULL, NULL) >= 0) 
        {
            if (ack_pkt.acknum == seq_num) {
                ACKed = 1;
                break;
            }
        }

        if (!ACKed) 
        {
            // ACK packet
            fseek(fp, -(WINDOW_SIZE * PAYLOAD_SIZE), SEEK_CUR);
            for (int i = 0; i < WINDOW_SIZE; ++i) 
            {
                fread(buffer, 1, PAYLOAD_SIZE, fp);
                build_packet(&pkt, seq_num, 0, 0, 0, PAYLOAD_SIZE, buffer);
                sendto(send_sockfd, &pkt, sizeof(struct packet), 0, (struct sockaddr *)&server_addr_to, addr_size);
                seq_num++;
            }
        }

        if (feof(fp) && ACKed) 
        {
            build_packet(&pkt, seq_num, 0, 1, 0, 0, buffer);
            sendto(send_sockfd, &pkt, sizeof(struct packet), 0, (struct sockaddr *)&server_addr_to, addr_size);
            break;
        }
    }
    fclose(fp);
    close(send_sockfd);
    return 0;
}