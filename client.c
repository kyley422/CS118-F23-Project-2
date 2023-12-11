#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <time.h>

#include "utils.h"

#define DEFAULT_WINDOW_SIZE 1
#define FRAMES_SIZE 1024
// #define TIMEOUT 0.0005
#define TIMEOUT 0.005


typedef struct {
    struct packet pkt;
    double sent_time;
} Frame;

void send_packet(int send_sockfd, struct packet *pkt, struct sockaddr_in *server_addr_to, socklen_t addr_size) {
    sendto(send_sockfd, pkt, sizeof(struct packet), 0, (struct sockaddr *)server_addr_to, addr_size);
}

int compare_seq(const void *a, const void *b) {
    if (((const Frame*)a)->pkt.seqnum < ((const Frame*)b)->pkt.seqnum) {
        return -1;
    }
    else if (((const Frame*)a)->pkt.seqnum == ((const Frame*)b)->pkt.seqnum) {
        return 0;
    }
    else {
        return 1;
    }
}

int shrink_window(Frame frames[], int old_cwnd, int new_cwnd) {
    int new_seq;
    printf("SHRINK WINDOW\n");
    printf("=====OLD WINDOW, size=%d=====\n", old_cwnd);
    for (int i = 0; i < old_cwnd; ++i) {
        printf("%d,", frames[i].pkt.seqnum);
    }

    Frame frames_copy[FRAMES_SIZE];
    int num_written = 0;
    for (int i = 0; i < old_cwnd; ++i) {
        frames_copy[i] = frames[i];
    }

    qsort(frames_copy, old_cwnd, sizeof(Frame), compare_seq);

    for (int i = 0; i < new_cwnd; ++i) {
        frames[i].pkt.seqnum = -1;
    }
    for (int i = 0; i < old_cwnd; ++i) {
        if ((frames_copy[i].pkt.seqnum != -1) && (num_written < new_cwnd)) {
            frames[frames_copy[i].pkt.seqnum % new_cwnd] = frames_copy[i];
            num_written++;
        }
    }
    printf("\n=====NEW WINDOW, size=%d=====\n", new_cwnd);
    for (int i = 0; i < new_cwnd; ++i) {
        printf("%d,", frames[i].pkt.seqnum);
    }
    printf("\n====================\n");

    // Reset sequence number, drop excess reads
    // Find max seqnum in frames
    int i = 0;
    while (i < new_cwnd && frames[i].pkt.seqnum != -1) {
        new_seq = frames[i].pkt.seqnum + 1;
        i++;
    }
    printf("NEW SEQ #: %d\n", new_seq);
    return new_seq;
}

void increment_window(Frame frames[], int cwnd) {
    printf("Increment window before:\n");
    for (int i=0; i<cwnd; ++i) {
        printf("%d,", frames[i].pkt.seqnum);
    }
    printf("\n");

    Frame frames_copy[FRAMES_SIZE];
    for (int i = 0; i < cwnd; ++i) {
        frames_copy[i] = frames[i];
    }

    // Zero out frames to properly place the new empty slot
    for (int i = 0; i < cwnd + 1; ++i) {
        frames[i].pkt.seqnum = -1;
    }

    // Use new modulo arithmetic to place in new cwnd frames
    for (int i = 0; i < cwnd; ++i) {
        if (frames_copy[i].pkt.seqnum != -1) {
            frames[frames_copy[i].pkt.seqnum % (cwnd + 1)] = frames_copy[i];
        }
    }
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
    int cwnd = DEFAULT_WINDOW_SIZE;
    int fast_retransmit_count = 0;
    int last_successful_ack;

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

    Frame frames[FRAMES_SIZE];
    int base = 0;
    int expected_ack_num = 0;
    bool timeout_occured;
    bool fast_recovery = false;
    int ssthresh = 40;
    int final_seq = -1;

    bool final = false;

    // Initialize array to empty
    for (int i = 0; i < FRAMES_SIZE; i++) {
        frames[i].pkt.seqnum = -1;
        frames[i].sent_time = -1;
    }

    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(struct timeval));

    // Begin system clock
    clock_t begin = clock();
    while (1) {
        printf("=====Begin RTT=====\n");
        printf("cwnd: %d\n", cwnd); 
        timeout_occured = false;
        clock_t end = clock();
        for (int i=0; i<cwnd; ++i) {
            // printf("RTT: %f\n", ((double)(end - begin) / CLOCKS_PER_SEC) - frames[i].sent_time);
            if (frames[i].sent_time != -1 && ((double)(end - begin) / CLOCKS_PER_SEC) - frames[i].sent_time >= TIMEOUT) {
                // Timeout detected
                printf("==========Timeout detected=========%d \n", frames[i].pkt.seqnum);
                build_packet(&pkt, frames[i].pkt.seqnum, ack_num, 0, 0, frames[i].pkt.length, frames[i].pkt.payload);
                send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);

                frames[i].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;
                timeout_occured = true;
            }
        }
        // Reorganize windows
        // shrink_window(frames, cwnd, cwnd);        

        

        if (seq_num != 0) {
            // printf("Recieved Seq#: %d\n", seq_num);
        // Receive acknowledgments
          while (recvfrom(listen_sockfd, &ack_pkt, sizeof(ack_pkt), 0, NULL, NULL) >= 0) {
            // Check if the acknowledgment is for a packet in the window
                // printf("Received ACK#: %u\n", ack_pkt.acknum);
                // if (final && ack_pkt.acknum >= (final_seq)) {
                //     printf("ack#: %d seq#: %d\n", ack_pkt.acknum, final_seq);
                //     fclose(fp);
                //     close(listen_sockfd);
                //     close(send_sockfd);
                //     return 0;
                // }

                if (ack_pkt.acknum == expected_ack_num && frames[(ack_pkt.acknum) % cwnd].pkt.seqnum == expected_ack_num) {
                printf("Recieved ACK:%d same as Expected ACK:%d\n", ack_pkt.acknum, expected_ack_num);
                // Successful ACK advance by window by 1
                frames[(ack_pkt.acknum) % cwnd].pkt.seqnum = -1;
                frames[(ack_pkt.acknum) % cwnd].sent_time = -1;
                expected_ack_num++;

                // Congestion control: successful ACK, reset fast transmit counter
                fast_retransmit_count = 0;
                // Exit fast recovery, if we're in it
                if  (fast_recovery) {
                    printf("EXIT FAST RECOVERY\n");
                    fast_recovery = false;
                    seq_num = shrink_window(frames, cwnd, ssthresh);
                    cwnd = ssthresh;
                }

                last_successful_ack = ack_pkt.acknum;
                break;

              } else if (ack_pkt.acknum >= expected_ack_num) {
                printf("Recieved ACK:%d > Expected ACK:%d\n", ack_pkt.acknum, expected_ack_num);
                for (int i=0; i<cwnd; ++i) {
                    if (frames[i].pkt.seqnum <= ack_pkt.acknum) {
                        frames[i].pkt.seqnum = -1;
                        frames[i].sent_time = -1;
                    }
                }
                expected_ack_num = ack_pkt.acknum+1;

                // Exit fast recovery, if we're in it
                fast_retransmit_count = 0;
                if  (fast_recovery) {
                    printf("EXIT FAST RECOVERY\n");
                    fast_recovery = false;
                    seq_num = shrink_window(frames, cwnd, ssthresh);
                    cwnd = ssthresh;
                }

                last_successful_ack = ack_pkt.acknum;
                
                break;
              }
              else {
                // Duplicate packet
                printf("Recieved ACK:%d <  Expected ACK%d\n", ack_pkt.acknum, expected_ack_num);
                build_packet(&pkt, ack_pkt.acknum+1, ack_num, 0, 0, frames[(ack_pkt.acknum+1 - base) % cwnd].pkt.length, frames[(ack_pkt.acknum+1 - base) % cwnd].pkt.payload);
                send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);

                expected_ack_num = ack_pkt.acknum;
                
                clock_t end = clock();
                frames[(ack_pkt.acknum + 1 - base) % cwnd].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;
                
                fast_retransmit_count++;
                break;
              }
            }
            printf("Frame after ACK:\n");
            for (int i=0; i<cwnd; ++i) {
                printf("%d,", frames[i].pkt.seqnum);
            }
            printf("\n");
        }

        // if (!final || seq_num < final_seq) {
        for (int i = 0; i < cwnd; ++i) {
            if (frames[i % cwnd].pkt.seqnum == -1) { 
                fseek(fp, (seq_num* (PAYLOAD_SIZE)), SEEK_SET);
                size_t bytesRead = fread(buffer, 1, PAYLOAD_SIZE, fp);
                printf("Sending seq#: %d\n", seq_num);

                build_packet(&pkt, seq_num, ack_num, 0, 0, bytesRead, buffer);
                frames[(seq_num - base) % cwnd].pkt = pkt;
                
                send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);
                clock_t end = clock();
                frames[(seq_num - base) % cwnd].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;


                // if (bytesRead == 0) {
                //     build_packet(&pkt, seq_num, ack_num, 1, 0, bytesRead, buffer);
                //     frames[(seq_num - base) % cwnd].pkt = pkt;

                //     send_packet(send_sockfd, &pkt, &server_addr_to, addr_size);
                //     clock_t end = clock();
                //     frames[(seq_num - base) % cwnd].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;

                //     break;
                // }
                seq_num++;
            }
        }
        // }
        // ACK Timeout
        // tv.tv_sec = TIMEOUT_SEC;
        // tv.tv_usec = 0;
        // setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(struct timeval));
        if (feof(fp)) { //&& !final
            final_seq = seq_num;
            printf("Built FIN packet, seq #%d", seq_num);
            build_packet(&pkt, seq_num, 0, 1, 0, 0, buffer);
            increment_window(frames, cwnd);
            cwnd++;
            frames[(seq_num - base) % cwnd].pkt = pkt;

            sendto(send_sockfd, &pkt, sizeof(struct packet), 0, (struct sockaddr *)&server_addr_to, addr_size);
            clock_t end = clock();
            frames[(seq_num - base) % cwnd].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;

            while (1) {
                if (((double)(end - begin) / CLOCKS_PER_SEC) - frames[(seq_num - base) % cwnd].sent_time >= TIMEOUT) {
                    // printf("IM IN FIRST\n");
                    sendto(send_sockfd, &pkt, sizeof(struct packet), 0, (struct sockaddr *)&server_addr_to, addr_size);
                    clock_t end = clock();
                    frames[(seq_num - base) % cwnd].sent_time = (double)(end - begin) / CLOCKS_PER_SEC;
                }
                if (recvfrom(listen_sockfd, &ack_pkt, sizeof(ack_pkt), 0, NULL, NULL) >= 0) {
                    // printf("Im in second: ack%d, seq%d\n", ack_pkt.acknum, seq_num);
                    fclose(fp);
                    close(listen_sockfd);
                    close(send_sockfd);
                    return 0;
                    
                }
            }
            final = true;
            
        }
        // Successful window transfer, AD cwnd
        if (cwnd <= ssthresh || fast_recovery) {
            increment_window(frames, cwnd);
            cwnd++;
        }
        
        // if (all_acked) {
        //     // Successful window transfer, AD cwnd
        //     cwnd++;
        // }
        if (fast_retransmit_count >= 3 && !fast_recovery) {
            // Engage fast recovery
            fast_recovery = true;
            printf("Engaged fast recovery\n");

            int old_cwnd = cwnd;
            ssthresh = (2 > cwnd/2) ? 2 : cwnd/2;
            cwnd = ssthresh + 3;
            seq_num = shrink_window(frames, old_cwnd, cwnd);
            fast_retransmit_count = 0;

            build_packet(&pkt, last_successful_ack + 1, ack_num, 0, 0, 0, buffer);
            sendto(send_sockfd, &pkt, sizeof(struct packet), 0, (struct sockaddr *)&server_addr_to, addr_size);
            printf("FR: Retransmitting pkt #%d\n", pkt.seqnum);
        }
        else if (timeout_occured) {
            // Timeout occurred
            seq_num = shrink_window(frames, cwnd, DEFAULT_WINDOW_SIZE);
            cwnd = DEFAULT_WINDOW_SIZE;

            // Reset sequence number, drop excess reads
            seq_num = seq_num - cwnd;
        }
        printf("Frame after reorganization:\n");
        for (int i=0; i<cwnd; ++i) {
            printf("%d,", frames[i].pkt.seqnum);
        }
        printf("\n");
        printf("=====END RTT=====\n");
    }
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

