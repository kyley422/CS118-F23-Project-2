#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "utils.h"


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

    // Configure the server address structure to which we will send data
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

    // Open file for reading
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }

    // TODO: Read from file, and initiate reliable data transfer to the server

    // // Read from file, send to server
    // size_t bytes_read;
    // while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
    //     memcpy(pkt.payload, buffer, bytes_read);
    //     // printf("%s\n",pkt.payload);
    //     // send(send_sockfd, pkt.payload, sizeof(pkt.payload), 0);

    //     if(sendto(send_sockfd, pkt.payload, strlen(pkt.payload), 0, (struct sockaddr*)&server_addr_to, addr_size) < 0) {
    //         printf("Unable to send client message\n");
    //         return -1;
    //     }
    // }
    int file_pos = 0;
    size_t bytesRead;
    while(1) {
        if (fseek(fp, file_pos, SEEK_SET) != 0) {
            close(listen_sockfd);
            close(send_sockfd);
            return -1;
        }
        file_pos += PAYLOAD_SIZE;
        bytesRead = fread(buffer, 1, sizeof(buffer), fp);
        
        // printf("%lu\n", bytesRead);

        // pkt.seqnum = seq_num;
        // pkt.acknum = ack_num;
        // pkt.ack = 0;
        // pkt.length = 0;
        // pkt.last = (bytesRead < PAYLOAD_SIZE) ? 1 : 0;

        // printf("%.*s\0", (int)bytesRead, buffer);
        
        memcpy(pkt.payload, buffer, bytesRead);
        build_packet(&pkt, 0, 0, 0, 0, bytesRead, buffer);
        printf("%.*s\0", (int)bytesRead, pkt.payload);
        // printf("AAbRnCFJ3e5CgtJBPLqYuk7hMMDMi5GNxsmWCoLh7Lqy50wR2wAMYRwmQuD9VAG6FcO9d6CfzjjNLuCApt9sIlBrLroherBmIMwpraVbyKn7zniBveVQUFKqENFrz2YwsNRAiPNlQMNZNZYAQQVykWc5EnsjO8rk4QGsB9VtHWFo8btQm0SFX8JO1lGgX0nXEXF4XLw5mdc9qc2j18o28bcTxNPp7lOPRFrDNGBXrMmJ8AgN0Yy0ZEZh1i3ccBqlWRiaoSWDSAUxFeGsPiNC29J70RHf7wMsdmMT3InUSH1Q6KrDK9hvWHY9whdmCZhUMbVzew7u6NEn2olQXONfDxdX35ME05GC26ipZb29RhNJZRSfB9m3HmuduFcQxLnyHQTAy3OwtUM26JSRbkFgsmlCr54Ygwo2nXjh0yw57U5e26DlAnPolSiuzInSj2RFKh6HGE2BFKdM9ql2o1xtJV6lbdkoGQUsfEo0HIIxy6cKIVfPo12u6gU6N5ZChxzaKJqWwbMxfx7exXNmugWPFfArL5w85qW3YP6CvjP4UMzVo5XYa4llvZCrjrmLjKcFZEDUTqupVcYbSNSgNH5U8tsNm2HtccgSwkByHJTcAdQ0eXBLkEpCpAo8kSBReA5n10lEY9bVOtA6ArXqRQ5jsKhsJHwL62PFiwPfBKKbf8EJEFvy1764jqScqCa823WXWjif3iUaDbOsHe5A6MAMscYxmu2PeOdPWBVQnTGQCAT1WW71W9P78ZOagVUJe5qbGVhtsTqZkSdRYYqVjR8BrBxtqv3bf2KQbu8xvH2E37ytSckllS0vQHZaxRXvL8W7JFOCRAGWT6f67DnS67Do1qRWNMrvckqClXgyg1XAZVvzbl6w2W0elLsYYs3IllfuReKCtOOi4WXRsbN4uhnOboytbtvKEBy57axJ9ozo9ePg5gpXL1R2DJcxoL0iMqOxHB4kc1v8nNJ5IwH2Tblp1fWpLHpG4ZovbTAiPA6xuqqUFotJ2NOwim9wK6B2UaQO");        
        if (bytesRead == 0) {
            pkt.last = 1;
        }
        

        if (sendto(send_sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&server_addr_to, addr_size) < 0) {
            printf("Unable to send client message\n");
            return -1;
        }


        // if (bytesRead == 0) {
        //     pkt.last = 1;
        //     sendto(send_sockfd, &pkt, sizeof(struct packet), 0, (struct sockaddr *)&server_addr_to, addr_size);
        //     break;
        // }

        // memset(pkt.payload, 0, bytesRead);
        // seq_num++;

    }


    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

