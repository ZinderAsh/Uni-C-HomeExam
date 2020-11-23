#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "print_lib.h"

// true and false values because they are more intuitive to use
#define true 1
#define false 0
#define INFINITY 99999

#define EXIT_SUCCESS 0
#define EXIT_ERROR  -1

struct neighbour {
    int address;
    int weight;
    struct node* node;
};

struct node {
    int fd;
    int port;
    int address;
    struct neighbour** neighbours;
    int neighbour_count;
    char visitted;
    int distance;
    int** routing_table;
    int routing_table_len;
    struct node* prev_node;
};
/* Node routing_table is in this format:
 * { {destination, next_node},
 *   ... }
 */

struct node* nodes;

int tcp_port;
int node_count;

void run_server(int socket);
void create_routing_tables();
void dijkstra(struct node* from);
void print_weighted_edges();
int create_server_socket();
void free_nodes();
int validate_neighbour(struct node* node_a, struct node* node_b);
void send_routing_tables();

// Used to write and exit when errors occur
void exit_error(char* message) {
    fprintf(stderr, "%s", message);
    exit(EXIT_ERROR);
}

int main(int argc, char** argv) {

    if (argc != 3) {
       exit_error("Needs exactly 2 arguments.\n");
    }

    tcp_port = strtol(argv[1], NULL, 10);
    node_count = strtol(argv[2], NULL, 10);

    // Allocate node array and set important initial values
    // Values are set mostly to avoid errors when freeing/closing after early errors
    nodes = malloc(sizeof(struct node) * node_count);
    int i;
    for (i = 0; i < node_count; i++) {
        nodes[i].fd = -1;
        nodes[i].port = 0;
        nodes[i].address = 0;
        nodes[i].neighbour_count = 0;
        nodes[i].neighbours = NULL;
        nodes[i].routing_table = NULL;
        nodes[i].routing_table_len = 0;
        nodes[i].prev_node = NULL;
    }

    int server_socket = create_server_socket();

    run_server(server_socket);

    free_nodes(nodes);
    close(server_socket);

    exit(EXIT_SUCCESS);
}

// Creates and returns a TCP socket used to receive data from the nodes
int create_server_socket() {

    // Create a TCP socket with the base port
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        free_nodes();
        exit_error("Failed to create server socket.\n");
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(tcp_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

    // Bind and listen to the socket, exit on fail
    int error = bind(server_socket, (struct sockaddr*) &server_addr, sizeof(server_addr));
    if (error == -1) {
        free_nodes();
        close(server_socket);
        exit_error("Failed to bind server socket.\n");
    }

    error = listen(server_socket, node_count);
    if (error == -1) {
        free_nodes();
        close(server_socket);
        exit_error("Failed to listen to server socket.\n");
    }

    return server_socket;

}

// Received data about nodes and sends routing tables back
void run_server(int socket) {

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(struct sockaddr_in);
    char ip[16];

    int accepted = 0;

    // Listen for node connections until node count is fulfilled
    while (accepted < node_count) {

        int con_socket = accept(socket, (struct sockaddr*) &client_addr, &client_len);
        if (con_socket == -1) {
            free_nodes();
            close(socket);
            exit_error("Failed to accept socket connection.\n");
        }

        if (!inet_ntop(client_addr.sin_family, &(client_addr.sin_addr), ip, client_len)) {
            free_nodes();
            close(socket);
            close(con_socket);
			exit_error("Failed to converting IP address to string\n");
		}

        // Store the nodes file descriptor and port
        nodes[accepted].fd = con_socket;
        nodes[accepted].port = client_addr.sin_port;

        accepted++;

    }

    // Read the data sent by each of the nodes
    int buffer[2];

    int i, j, k;
    for (i = 0; i < node_count; i++) {
        // Read address and neighbour count
        read(nodes[i].fd, buffer, sizeof(int) * 2);
        nodes[i].address = buffer[0];
        nodes[i].neighbour_count = buffer[1];
        // Allocates memory to fit the amount of neighbours
        nodes[i].neighbours = malloc(
            sizeof(struct neighbour*) * nodes[i].neighbour_count);
        // Reads one neighbour at a time until all are read
        for (j = 0; j < nodes[i].neighbour_count; j++) {
            read(nodes[i].fd, buffer, sizeof(int) * 2);
            nodes[i].neighbours[j] = malloc(sizeof(struct neighbour));
            nodes[i].neighbours[j]->address = buffer[0];
            nodes[i].neighbours[j]->weight = buffer[1];
        }
        // Create a routing table to fit all nodes except itself
        nodes[i].routing_table = malloc(sizeof(int*) * node_count - 1);
        for (j = 0; j < node_count - 1; j++) {
            nodes[i].routing_table[j] = NULL;
        }
        nodes[i].routing_table_len = 0;
        nodes[i].prev_node = NULL;
    }

    // Set node pointers variable for all neighbours
    for (i = 0; i < node_count; i++) {
        for (j = 0; j < nodes[i].neighbour_count; j++) {
            for (k = 0; k < node_count; k++) {
                if (nodes[k].address == nodes[i].neighbours[j]->address) {
                    nodes[i].neighbours[j]->node = &nodes[k];
                    break;
                }
            }

        }
    }

    // Remove neighbours that are not mutual or have differing weights
    for (i = 0; i < node_count; i++) {
        for (j = nodes[i].neighbour_count - 1; j >= 0; j--) {

            int valid = validate_neighbour(&nodes[i], nodes[i].neighbours[j]->node);
            
            if (valid == -1) {
                fprintf(stderr, "Neighbour connection between %d and %d had differing weights.\n",
                    nodes[i].address, nodes[i].neighbours[j]->address);
            } else if (valid == -2) {
                fprintf(stderr, "Neighbour connection between %d and %d was not mutual.\n",
                    nodes[i].address, nodes[i].neighbours[j]->address);
            }
            if (valid != 0) {
                // Neighbour is invalid, remove it from the array and shift the indexes after it.
                free(nodes[i].neighbours[j]);
                nodes[i].neighbour_count--;
                for (k = j; k < nodes[i].neighbour_count; k++) {
                    nodes[i].neighbours[k] = nodes[i].neighbours[k + 1];
                }
                nodes[i].neighbours[nodes[i].neighbour_count] = NULL;
                // No need to realloc the array as the lowered neighbour_count will ignore the removed
                // indexes at the end of the array
            }
        }
    }

    create_routing_tables();

    print_weighted_edges();

    send_routing_tables();

}

// Used to send packets of routing tables to all the connected nodes
void send_routing_tables() {

    int i, j;
    for (i = 0; i < node_count; i++) {

        struct node node = nodes[i];
        int packet_size = 1 + (node.routing_table_len * 2);
        int packet[packet_size];

        // Create a packet starting with the size of the routing table
        // followed by alternating destination and next-nodes.
        packet[0] = node.routing_table_len;
        for (j = 0; j < node.routing_table_len; j++) {
            packet[j*2+1] = node.routing_table[j][0];
            packet[j*2+2] = node.routing_table[j][1];
        }

        write(node.fd, packet, sizeof(int) * packet_size);

    }

}

// Gets the node that is used to send all data packets
struct node* get_first_node() {

    int i;
    for (i = 0; i < node_count; i++) {
        if (nodes[i].address == 1) {
            return &nodes[i];
        }
    }

    free_nodes();
    exit_error("Could not find first node\n");

}

void create_routing_tables() {

    struct node* first_node = get_first_node();

    // Reset all node states to make them ready for dijkstra's algorithm
    int i, j;
    for (i = 0; i < node_count; i++) {
        nodes[i].visitted = false;
        if (nodes[i].address == 1) {
            nodes[i].distance = 0;
        } else {
            nodes[i].distance = INFINITY;
        }
    }

    // Use dijkstra to set all the nodes' prev_node variables
    dijkstra(first_node);

    // Fill routing tables with paths to all nodes except node 1.
    for (i = 0; i < node_count; i++) {
        if (nodes[i].address != 1) {
            struct node* to_node = &nodes[i];
            struct node* next_node = to_node;
            struct node* from_node = to_node->prev_node;
            // fill routing tables with next-nodes heading towards to_node
            while (from_node != NULL) {
                for (j = 0; j < node_count - 1; j++) {
                    if (from_node->routing_table[j] == NULL) {
                        from_node->routing_table[j] = malloc(sizeof(int) * 2);
                        from_node->routing_table[j][0] = to_node->address;
                        from_node->routing_table[j][1] = next_node->address;
                        from_node->routing_table_len++;
                        break;
                    }
                }
                next_node = from_node;
                from_node = from_node->prev_node;
            }
        }
    }
    
}

// Prints the distance to nodes that are on the path to other nodes
void print_weighted_edges() {

    // Run for all combinations of two nodes
    int i, j;
    for (i = 0; i < node_count; i++) {

        struct node* from_node = &nodes[i];

        for (j = 0; j < node_count; j++) {

            struct node* to_node = &nodes[j];
            struct node* prev_node = to_node;

            int distance = -1;

            // Go backwards until it reaches node 1, and see if from_node is on the path
            while (prev_node != NULL) {

                if (prev_node == from_node) {
                    distance = from_node->distance;
                    break;
                }

                prev_node = prev_node->prev_node;

            }

            print_weighted_edge(
                from_node->address, to_node->address, distance);

        }
    }

}

// Checks if a neighbour is both mutual and has the same weight from both nodes
// Return 0 if valid, -1 if the weights differ, and -2 if it's not mutual
int validate_neighbour(struct node* node_a, struct node* node_b) {

    int weight_a = -1;
    int weight_b = -1;

    // Find the weight of node_a -> node_b
    int i;
    for (i = 0; i < node_a->neighbour_count; i++) {
        if (node_a->neighbours[i]->node == node_b) {
            weight_a = node_a->neighbours[i]->weight;
        }
    }

    // Find the wight of node_b -> node_a
    for (i = 0; i < node_b->neighbour_count; i++) {
        if (node_b->neighbours[i]->node == node_a) {
            weight_b = node_b->neighbours[i]->weight;
        }
    }

    if (weight_a == -1 || weight_b == -1) {
        return -2;
    } else if (weight_a == weight_b) {
        return 0;
    } else {
        return -1;
    }
}

// Sets all nodes' prev_node pointer with dijkstra's algorithm
void dijkstra(struct node* from) {

    int i;
    for (i = 0; i < from->neighbour_count; i++) {

        struct node* neighbour = from->neighbours[i]->node;

        // Nodes new distance equals the current node's distance + the neighbour weight
        int new_distance = from->distance + from->neighbours[i]->weight;

        // Update the distance if the new path is shorter
        if (neighbour->distance > new_distance) {
            neighbour->distance = new_distance;
            neighbour->prev_node = from;
            // Set node as unvisitted to make sure it is checked again with new distance
            neighbour->visitted = false;
        }
    }

    from->visitted = true;

    // Check recursively through all unvisited neighbours
    for (i = 0; i < from->neighbour_count; i++) {
        if (!from->neighbours[i]->node->visitted) {
            dijkstra(from->neighbours[i]->node);
        }
    }

}

void free_nodes() {

    int i, j;
    for (i = 0; i < node_count; i++) {
        if (nodes[i].fd != -1) {
            close(nodes[i].fd);
        }
    }

    for (i = 0; i < node_count; i++) {
        for (j = 0; j < nodes[i].neighbour_count; j++) {
            free(nodes[i].neighbours[j]);
        }
        free(nodes[i].neighbours);
        if (nodes[i].routing_table != NULL) {
            for (j = 0; j < node_count - 1; j++) {
                if (nodes[i].routing_table[j] != NULL) {
                    free(nodes[i].routing_table[j]);
                }
            }
        }
        free(nodes[i].routing_table);
    }

    free(nodes);

}















