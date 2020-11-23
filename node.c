#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#include "print_lib.h"

#define EXIT_SUCCESS  0
#define ERROR_UDP    -1
#define ERROR_SERVER -2
#define ERROR_NODE1  -3

struct neighbour {
    int address;
    int weight;
};

int port;
int own_address;
struct neighbour* neighbours;
int neighbour_count;

int routing_table_len;
int** routing_table;

void read_routing_table(int socket);
void run_node(int socket);
void run_source_node();
void send_packet(short destination, unsigned char* packet, short packet_len);
int create_node_socket();
int create_server_socket(int node_socket);
void send_node_data();

// Used to write and exit when errors occur
void exit_error(char* message, int error_code) {
    fprintf(stderr, "%s", message);
    exit(error_code);
}

int main(int argc, char** argv) {

    if (argc < 3) {
        exit_error("Needs at least 2 arguments.\n", -1);
    }

    port = strtol(argv[1], NULL, 10);
    own_address = strtol(argv[2], NULL, 10);

    // Create neighbours from program arguments
    if (argc > 3) {
        neighbour_count = argc - 3;
        neighbours = malloc(sizeof(struct neighbour) * neighbour_count);
        int i;
        for (i = 0; i < neighbour_count; i++) {
            neighbours[i].address = strtol(strtok(argv[i + 3], ":"), NULL, 10);
            neighbours[i].weight = strtol(strtok(NULL, ":"), NULL, 10);
        }
    } else {
        neighbours = NULL;
        neighbour_count = 0;
    }

    int node_socket = create_node_socket();
    int server_socket = create_server_socket(node_socket);

    send_node_data(server_socket);

    free(neighbours);

    read_routing_table(server_socket);

    close(server_socket);

    // Run a seperate function for node 1
    if (own_address == 1) {
        run_source_node();
    } else {
        run_node(node_socket);
    }

    int i;
    for (i = 0; i < routing_table_len; i++) {
        free(routing_table[i]);
    }
    free(routing_table);

    close(node_socket);
    exit(EXIT_SUCCESS);
}

// Returns a UDP socket used to read packets to either receive or froward
int create_node_socket() {

    // Creating the UDP socket at port = base_port + own_address
    int node_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (node_socket == -1) {
        free(neighbours);
        exit_error("Failed to create node UDP socket.\n", ERROR_UDP);
    }

    struct sockaddr_in node_addr;
    node_addr.sin_family = AF_INET;
    node_addr.sin_port = htons(port + own_address);
    node_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int error = bind(node_socket, (struct sockaddr*) &node_addr, sizeof(struct sockaddr_in));
    if (error == -1) {
        free(neighbours);
        close(node_socket);
        exit_error("Failed to bind UDP socket.\n", ERROR_UDP);
    }

    return node_socket;

}

// Returns a TCP socket used to send data and receive a routing table from the server
int create_server_socket(int node_socket) {

    // Connecting to server at base port
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        free(neighbours);
        close(node_socket);
        exit_error("Failed to create client TCP socket.\n", ERROR_SERVER);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int error = connect(server_socket, (struct sockaddr*) &server_addr, sizeof (struct sockaddr_in));
    if (error == -1) {
        free(neighbours);
        close(node_socket);
        close(server_socket);
        exit_error("Failed to connect to server.\n", ERROR_SERVER);
    }

    return server_socket;

}

void send_node_data(int server_socket) {

    // Packets are formatted as such:
    // [own_address, neighbour_count, neighbour[0].address, neighbour[0].weight, ...]
    int packet_len = (sizeof(int) * (2 * neighbour_count + 2));
    int* packet = malloc(packet_len);

    packet[0] = own_address;
    packet[1] = neighbour_count;
    int i;
    for (i = 0; i < neighbour_count; i++) {
        packet[2 + i*2] = neighbours[i].address;
        packet[3 + i*2] = neighbours[i].weight;
    }

    write(server_socket, packet, packet_len);

    free(packet);

}

// Receive packets and forward packets until a QUIT message is received
void run_node(int socket) {

    // Buffer contains 1006 bytes for the 6 first bytes and a message up to 1000 character (inc. \0)
    unsigned char buffer[1006];
    short packet_len;

    while (1) {

        // Read UDP packet and get the packet length
        int bytes = read(socket, &buffer, 1006);
        short packet_len = ntohs(*(short*)&buffer[0]);

        if (bytes != packet_len) {
            // Did not receive entire packet, the packet is ignored
            fprintf(stderr, "ERROR: Data package was lossy.\n");
        } else {
            // Allocate a packet of the correct size and copy from the buffer
            unsigned char* packet = malloc(packet_len);
            memcpy(packet, buffer, packet_len);

            short source = ntohs(*(short*)&packet[2]);
            short destination = ntohs(*(short*)&packet[4]);
            char* message = &packet[6];

            // Check if this node is the destination for the packet
            if (destination == own_address) {
                print_received_pkt(own_address, packet);
                printf("%s\n", message);
                if (strcmp(message, "QUIT") == 0) {
                    free(packet);
                    break;
                }
            } else {
                print_forwarded_pkt(own_address, packet);
                send_packet(destination, packet, packet_len);
            }

            free(packet);
        }

    }

}

void run_source_node() {

    sleep(1);

    FILE* file = NULL;

    file = fopen("data.txt", "r");
    if (file == NULL) {
        exit_error("Failed to open data.txt\n", ERROR_NODE1);
    }

    unsigned char line[1000];
    
    while (fgets(line, 1000, file) != NULL) {

        short source = own_address;
        short destination = strtol(strtok(line, " "), NULL, 10);
        char* message = strtok(NULL, "\n");

        int string_len = strlen(message) + 1;

        // Convert the first 3 values to network byte order
        short packet_source = htons(source);
        short packet_destination = htons(destination);
        short packet_len = htons(6 + string_len);

        // Oppgaveteksten ga rekkefølgen:
        // [pakkelengde, destinasjonsadresse, kildeadresse, melding]
        // Men de vedlagte filene stokket om på destinasjons- og kildeadresse,
        // så jeg lot det stemme overens med de vedlagde filene.
        unsigned char* packet = malloc(packet_len);
        memcpy(&packet[0], &packet_len, 2);
        memcpy(&packet[2], &packet_source, 2);
        memcpy(&packet[4], &packet_destination, 2);
        strncpy(&packet[6], message, string_len);

        print_pkt(packet);

        // If the destination is this node, don't send the packet
        if (destination == own_address) {
            print_received_pkt(own_address, packet);
            free(packet);
            if (strcmp(message, "QUIT") == 0) {
                break;
            }
        } else {
            send_packet(destination, packet, ntohs(packet_len));
        }

        free(packet);

    }

    fclose(file);

}

// Uses to routing table to find what node to forward to next
int get_next_node(short destination) {

    int i;
    for (i = 0; i < routing_table_len; i++) {
        if (routing_table[i][0] == destination) {
            return routing_table[i][1];
        }
    }

    return -1;

}

void send_packet(short destination, unsigned char* packet, short packet_len) {

    // Create a socket and address to the next node
    int next_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (next_socket == -1) {
        exit_error("Failed to create sending UDP socket.\n", ERROR_UDP);
    }

    int next_address = get_next_node(destination);
    if (next_address == -1) {
        close(next_socket);
        fprintf(stderr, "ERROR: Could not find next node in path.\n");
        return;
    }

    struct sockaddr_in next_addr;
    next_addr.sin_family = AF_INET;
    next_addr.sin_port = htons(port + next_address);
    next_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Send the packet to the next node
    sendto(next_socket, packet, packet_len, 0,
        (struct sockaddr*) &next_addr, sizeof(struct sockaddr_in));

    close(next_socket);

}

// Function to receive and store the routing table from the server
void read_routing_table(int socket) {

    read(socket, &routing_table_len, sizeof(int));

    routing_table = malloc(sizeof(int*) * routing_table_len);

    int i;
    for (i = 0; i < routing_table_len; i++) {
        routing_table[i] = malloc(sizeof(int) * 2);
        read(socket, routing_table[i], sizeof(int) * 2);
    }

}