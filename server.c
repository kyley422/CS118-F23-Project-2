#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "utils.h"

typedef struct {
    struct packet pkt;
    bool ack_received;
    bool written_to_file;
} Frame;

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    socklen_t addr_size = sizeof(client_addr_from);

    // Create an array of frames to manage the window
    Frame frames[WINDOW_SIZE];
    int base = 0;
    int next_seq_num = 0;

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
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST); // Replace with actual client IP address
    client_addr_to.sin_port = htons(CLIENT_PORT_TO); // Replace with actual client port

    int expected_seq_num = 0;

    // Set up other parts of the code as follows:

    bool transmission_complete = false;
    int recv_len;

    // Open the target file for writing (always write to output.txt)
    FILE *fp = fopen("output.txt", "wb");
    if (fp == NULL) {
        perror("Error opening file");
        return 1;
    }

    while (!transmission_complete) {
        // Receive packets for the current window
        while (next_seq_num - base < WINDOW_SIZE) {
            // ... (Receive packets and manage ACKs as in the previous code) ...

            struct packet received_pkt;
            if ((recvfrom(listen_sockfd, (char *)&received_pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_from, &addr_size)) < 0) {
                printf("Unable to receive client message\n");
                return -1;
            }

            frames[next_seq_num % WINDOW_SIZE].pkt = received_pkt;
            frames[next_seq_num % WINDOW_SIZE].ack_received = true;
            next_seq_num++;
        }

        // Send ACKs for received packets
        for (int i = base; i < next_seq_num; ++i) {
            build_packet(&frames[i % WINDOW_SIZE].pkt, 0, frames[i % WINDOW_SIZE].pkt.seqnum, 0, 1, 1, "0");
            if (sendto(send_sockfd, &frames[i % WINDOW_SIZE].pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_to, addr_size) < 0) {
                perror("Error sending ACK\n");
                return -1;
            }

            printf("Sent ACK for pkt #%d\n", frames[i % WINDOW_SIZE].pkt.seqnum);
        }

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

/* START OF OLD CODE */

// #include <arpa/inet.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>

// #include "utils.h"

// int main() {
//     int listen_sockfd, send_sockfd;
//     struct sockaddr_in server_addr, client_addr_from, client_addr_to;
//     struct packet buffer;
//     socklen_t addr_size = sizeof(client_addr_from);
//     int expected_seq_num = 0;
//     int recv_len;
//     struct packet ack_pkt;

//     // Create a UDP socket for sending
//     send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
//     if (send_sockfd < 0) {
//         perror("Could not create send socket");
//         return 1;
//     }

//     // Create a UDP socket for listening
//     listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
//     if (listen_sockfd < 0) {
//         perror("Could not create listen socket");
//         return 1;
//     }

//     // Configure the server address structure
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(SERVER_PORT);
//     server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

//     // Bind the listen socket to the server address
//     if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         perror("Bind failed");
//         close(listen_sockfd);
//         return 1;
//     }

//     // Configure the client address structure to which we will send ACKs
//     memset(&client_addr_to, 0, sizeof(client_addr_to));
//     client_addr_to.sin_family = AF_INET;
//     client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
//     client_addr_to.sin_port = htons(CLIENT_PORT_TO);

//     // Open the target file for writing (always write to output.txt)
//     FILE *fp = fopen("output.txt", "wb");

//     // TODO: Receive file from the client and save it as output.txt
//     while(1) {
//         // recv_len = recvfrom(listen_sockfd, &buffer, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_from, &addr_size);
//         if ((recv_len = recvfrom(listen_sockfd, (char *)&buffer, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_from, &addr_size)) < 0) {
//             printf("Unable to recieve client message\n");
//             return -1;
//         }
//         fwrite(buffer.payload, 1, buffer.length, fp);
//         printf("Pkt #%d received\n", buffer.seqnum);
//         if (buffer.last == 1) {
//             break;
//         }

//         // Send ACK
//         build_packet(&buffer, 0, buffer.seqnum, 0, 1, 1, "0");
//         if (sendto(send_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_to, addr_size) < 0) {
//             perror("Error sending ACK\n");
//             return -1;
//         }
//     }

//     fclose(fp);
//     close(listen_sockfd);
//     close(send_sockfd);
//     return 0;
// }