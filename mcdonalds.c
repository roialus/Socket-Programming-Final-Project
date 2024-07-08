#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#define MULTICAST_GROUP "239.0.0.1" // Multicast group address
#define MULTICAST_PORT 5555         // Multicast port
#define SERVER_IP "127.0.0.1"
#define MCDONALDS_PORT 5556         // Unicast TCP port for communication with server
#define BUFFER_SIZE 1024            // Buffer size for receiving data

void *multicast_listener(void *arg);
void *tcp_communication_handler(void *arg);

int main() {
    pthread_t multicast_thread, tcp_thread;

    pthread_create(&multicast_thread, NULL, multicast_listener, NULL);
    pthread_create(&tcp_thread, NULL, tcp_communication_handler, NULL);

    pthread_join(multicast_thread, NULL);
    pthread_join(tcp_thread, NULL);

    return 0;
}

// Function to listen to the multicast channel
void *multicast_listener(void *arg) {
    struct sockaddr_in multicast_addr; // Multicast address
    struct ip_mreqn mreq;              // Multicast request structure
    int multicast_socket;              // Multicast socket
    socklen_t addr_len = sizeof(multicast_addr); // Address length for multicast address
    char buffer[BUFFER_SIZE];          // Buffer for receiving data

    // Create multicast socket
    if ((multicast_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { // Create a socket for sending and receiving datagrams
        perror("multicast socket creation failed");
        pthread_exit(NULL);
    }

    // Set socket options
    int reuse = 1;
    if (setsockopt(multicast_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0) {
        perror("Setting SO_REUSEADDR error");
        close(multicast_socket);
        pthread_exit(NULL);
    }

    // Set up multicast group information
    memset(&mreq, 0, sizeof(mreq));                // Clear the multicast request structure
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP); // Set the multicast group address
    mreq.imr_address.s_addr = htonl(INADDR_ANY);   // Set the local address
    mreq.imr_ifindex = 0;                          // Set the interface index to 0

    // Join the multicast group
    if (setsockopt(multicast_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) { // Join the multicast group
        perror("multicast join failed");
        close(multicast_socket); // Close the multicast socket
        pthread_exit(NULL);
    }

    // Set up multicast address to receive from any source
    memset(&multicast_addr, 0, sizeof(multicast_addr)); // Clear the multicast address structure
    multicast_addr.sin_family = AF_INET;                // Set the address family to IPv4
    multicast_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Set the address to receive from any source
    multicast_addr.sin_port = htons(MULTICAST_PORT);    // Set the port number

    // Bind to the multicast address
    if (bind(multicast_socket, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0) { // Bind the multicast socket to the multicast address
        perror("bind failed");
        close(multicast_socket);
        pthread_exit(NULL);
    }

    printf("McDonald's restaurant listening on multicast group %s:%d\n", MULTICAST_GROUP, MULTICAST_PORT); // Print the multicast group information

    while (1) { // Loop to keep receiving requests
        memset(buffer, 0, BUFFER_SIZE); // Clear the buffer

        // Receive multicast requests from server
        int bytes_received = recvfrom(multicast_socket, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&multicast_addr, &addr_len); // Receive data from the server
        if (bytes_received < 0) { // Check if receive failed
            perror("recvfrom failed");
            continue;
        }

        // Process the received data (here, assuming it's a menu request)
        if (strncmp(buffer, "REQUEST_MENU", strlen("REQUEST_MENU")) == 0) { // Check if the received data is a menu request
            printf("Multicast request received. Preparing to send menu data via TCP...\n"); // Debug print statement
            // Notify the TCP handler to send the menu data back to the server
            pthread_t tcp_thread;
            pthread_create(&tcp_thread, NULL, tcp_communication_handler, NULL);
            pthread_detach(tcp_thread);
        }
    }

    close(multicast_socket); // Close the multicast socket
    pthread_exit(NULL);
}

// Function to handle TCP communication with the server
void *tcp_communication_handler(void *arg) {
    int tcp_socket;
    struct sockaddr_in tcp_addr;
    char buffer[BUFFER_SIZE];

    if ((tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("TCP socket creation failed");
        pthread_exit(NULL);
    }

    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    tcp_addr.sin_port = htons(MCDONALDS_PORT);

    if (connect(tcp_socket, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("TCP connect failed");
        close(tcp_socket);
        pthread_exit(NULL);
    }

    // Register the restaurant with the server
    char *menu_data = "McDonalds 1. Big Mac Meal - $5.99\n2. Crispy Chicken Meal - $6.99\n3. Filet-O-Fish Meal - $5.49\n4. McChicken Meal - $4.99\n5. Quarter Pounder Meal - $6.49\n6. Chicken Nuggets Meal - $5.99\n7. Double Cheeseburger Meal - $4.99\n8. McDouble Meal - $4.49\n9. McRib Meal - $6.99\n10. Sausage McMuffin Meal - $3.99";   // Restaurant menu data example
    send(tcp_socket, menu_data, strlen(menu_data), 0);

    printf("McDonald's restaurant connected to server via TCP\n");

    while (1) {
        int valread = read(tcp_socket, buffer, BUFFER_SIZE);
        if (valread > 0) {
            buffer[valread] = '\0';
            printf("Received from server: %s\n", buffer);

            // Process order request and respond with estimated time
            if (strncmp(buffer, "ORDER:", strlen("ORDER:")) == 0) {
                srand(time(0));
                int estimated_time = rand() % 20 + 10; // Random estimated time between 10 and 30 minutes
                char response[BUFFER_SIZE];
                snprintf(response, BUFFER_SIZE, "Your order will be ready in %d minutes.", estimated_time);
                send(tcp_socket, response, strlen(response), 0);
            }
        }
    }

    close(tcp_socket);
    pthread_exit(NULL);
}
