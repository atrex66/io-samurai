#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include "jump_table.h"
#include <arpa/inet.h>

#define NUM_CYCLES 100000
#define SEND_PACKET_SIZE 3
#define RECV_PACKET_SIZE 5
#define TIMEOUT_SEC 2  // Increased timeout

static int sockfd;
static struct sockaddr_in local_addr, remote_addr;
char *ip_addr = "192.168.0.178";
uint16_t port = 8888;
uint8_t counter = 0;
char send_buffer[SEND_PACKET_SIZE] = {0x00,};

long long get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

static void init_socket(void) {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = INADDR_ANY;
    bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr));

    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip_addr, &remote_addr.sin_addr);
}

void jump_table_checksum_in() {
    checksum_index_in += (uint8_t)(send_buffer[0] + send_buffer[1] + 1);
    send_buffer[2] = jump_table[checksum_index_in];
}

int main() {
    char recv_buffer[RECV_PACKET_SIZE];
    long long start_time, end_time, elapsed_time;
    int cycles_completed = 0;
    long long total_latency = 0;

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    init_socket();
    printf("UDP socket created (%s:%d), connected to target.\n", ip_addr, port);

    // Benchmark loop
    start_time = get_time_ms();
    printf("Benchmark started. Running %d cycles...\n", NUM_CYCLES);
    long long cycle_start = get_time_ms();

    for (int i = 0; i < NUM_CYCLES; i++) {
        long long cycle_start = get_time_ms();
        counter ++;
        // DO NOT MODIFY THE DATA IF THE CARD IS CONNECTED TO YOUR MACHINE
        // DO NOT MODIFY THE DATA IF THE CARD IS CONNECTED TO YOUR MACHINE
        send_buffer[0] = 0;
        send_buffer[1] = 0;
        // DO NOT MODIFY THE DATA IF THE CARD IS CONNECTED TO YOUR MACHINE
        // DO NOT MODIFY THE DATA IF THE CARD IS CONNECTED TO YOUR MACHINE
        jump_table_checksum_in();
        // Send packet
        if (sendto(sockfd, send_buffer, SEND_PACKET_SIZE, 0, (struct sockaddr*)&remote_addr, sizeof(remote_addr)) < 0) {
            perror("Send failed");
            break;
        }

        // Receive response
        socklen_t addr_len = sizeof(ip_addr);
        int recv_len = -1;
        while (recv_len < 0) {recv_len = recvfrom(sockfd, recv_buffer, RECV_PACKET_SIZE, 0, NULL, NULL);}
        long long cycle_end = get_time_ms();
        total_latency += (cycle_end - cycle_start);
        cycles_completed++;
    }

    // Results
    end_time = get_time_ms();
    elapsed_time = end_time - start_time;

    printf("\nBenchmark results:\n");
    printf("Cycles attempted: %d\n", NUM_CYCLES);
    printf("Cycles completed: %d\n", cycles_completed);
    printf("Total time: %lld ms\n", elapsed_time);
    printf("Average cycle time (write + read): %.2f ms\n", (double)total_latency / cycles_completed);
    printf("Throughput: %.2f cycles/s\n", (cycles_completed / (elapsed_time / 1000.0)));

    close(sockfd);
    printf("Socket closed. Exiting...\n");
    return 0;
}
