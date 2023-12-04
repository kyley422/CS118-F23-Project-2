#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

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

    // TODO: Receive file from the client and save it as output.txt
    // ssize_t bytes_read;
    // while ((bytes_read = recv(send_sockfd, buffer.payload, sizeof(buffer.payload), 0)) > 0) {
    //     printf("%s\n", buffer.payload);
    // }

    // if (recvfrom(listen_sockfd, buffer.payload, sizeof(buffer.payload), 0, (struct sockaddr*)&client_addr_from, addr_size) < 0) {
    //     // printf("Did not recieve\n");
    //     // return -1;
    // }
    

    // printf("%s\n", buffer.payload);

    // // New implementation
    // while (recvfrom(listen_sockfd, buffer.payload, sizeof(buffer.payload), 0, (struct sockaddr*)&client_addr_from, &addr_size) > 0) {
    //     // printf("%s\n", buffer.payload);
    //     if (fputs(buffer.payload, fp) < 0) {
    //         perror("Could not write to file");
    //         return -1;
    //     }
    // }
    
    while(1) {
        // recv_len = recvfrom(listen_sockfd, &buffer, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_from, &addr_size);
        if ((recv_len = recvfrom(listen_sockfd, (char *)&buffer, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_from, &addr_size)) < 0) {
            printf("Unable to recieve client message\n");
            return -1;
        }

        // printf("%d\n", recv_len);
        // printf("%s\0", buffer.payload);

        // printf("%s\n", buffer.payload);
        // printf("ENDD_________ENDDD_________________________END_____________________END\n");

        // if (recv_len < 0) {
        //     perror("Error receiving packet");
        //     break;
        // }

        // printf("%s%d\n", "This is the seqnum:", buffer.seqnum);

        // ack_pkt.seqnum = buffer.seqnum;
        // ack_pkt.acknum = buffer.seqnum;
        // sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_to, sizeof(client_addr_to));

        fwrite(buffer.payload, 1, recv_len - 12, fp);
        // for (int i = 0; i < recv_len - 12; i++) {
        //     fprintf(fp, "%c", buffer.payload[i]);
        // }

        // printf("%s\n", buffer.payload);

        if (buffer.last == 1) {
            fwrite(buffer.payload, 1, recv_len - 12, fp);
            return;
        }

        // if (buffer.last) {
        //     break;
        // }

    }

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
