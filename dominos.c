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
#define DOMINOS_PORT 5557           // Unicast TCP port for communication with server
#define BUFFER_SIZE 1024            // Buffer size for receiving data

void *multicast_listener(void *arg);
void *tcp_communication_handler(void *arg);
int sent_menu = 0;

int main() {
    pthread_t multicast_thread, tcp_thread;
    int tcp_socket;
    pthread_create(&tcp_thread, NULL, tcp_communication_handler, &tcp_socket);
    pthread_create(&multicast_thread, NULL, multicast_listener, &tcp_socket);
    
    pthread_join(multicast_thread, NULL);
    pthread_join(tcp_thread, NULL);

    return 0;
}

// Function to listen to the multicast channel
void *multicast_listener(void *arg) {
    int tcp_socket = *(int *)arg;       // tcp socket
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
    char *menu_data = "Dominos 1. Pepperoni Pizza - $12.99\n2. Margherita Pizza - $10.99\n3. BBQ Chicken Pizza - $13.99\n4. Veggie Pizza - $11.99\n5. Meat Lovers Pizza - $14.99\n6. Hawaiian Pizza - $12.99\n7. Buffalo Chicken Pizza - $13.99\n8. Cheese Pizza - $9.99\n9. Supreme Pizza - $14.99\n10. Bacon Cheeseburger Pizza - $13.99";
    printf("Domino's restaurant listening on multicast group %s:%d\n", MULTICAST_GROUP, MULTICAST_PORT); // Print the multicast group information

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
            if(sent_menu == 0){
                sent_menu = 1;
                printf("Multicast request received. Preparing to send menu data via TCP...\n"); // Debug print statement
                // Notify the TCP handler to send the menu data back to the server
                send(tcp_socket, menu_data, strlen(menu_data), 0);
            }
            else{
                printf("Menu Already Sent\n");
            }
        }
    }

    close(multicast_socket); // Close the multicast socket
    pthread_exit(NULL);
}

// Function to handle TCP communication with the server
void *tcp_communication_handler(void *arg) {
    int *tcp_socket = (int *)arg;
    struct sockaddr_in tcp_addr;
    char buffer[BUFFER_SIZE];

    if ((*tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("TCP socket creation failed");
        pthread_exit(NULL);
    }

    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    tcp_addr.sin_port = htons(DOMINOS_PORT);

    if (connect(*tcp_socket, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("TCP connect failed");
        close(*tcp_socket);
        pthread_exit(NULL);
    }

    // Register the restaurant with the server
    char *menu_data = "Dominos 1. Pepperoni Pizza - $12.99\n2. Margherita Pizza - $10.99\n3. BBQ Chicken Pizza - $13.99\n4. Veggie Pizza - $11.99\n5. Meat Lovers Pizza - $14.99\n6. Hawaiian Pizza - $12.99\n7. Buffalo Chicken Pizza - $13.99\n8. Cheese Pizza - $9.99\n9. Supreme Pizza - $14.99\n10. Bacon Cheeseburger Pizza - $13.99";   // Restaurant menu data example
    //send(tcp_socket, menu_data, strlen(menu_data), 0);

    printf("Domino's restaurant connected to server via TCP\n");

    while (1) {
        int valread = read(*tcp_socket, buffer, BUFFER_SIZE);
        if (valread > 0) {
            buffer[valread] = '\0';
            printf("Received from server: %s\n", buffer);

            // Process order request and respond with estimated time
            if (strncmp(buffer, "ORDER:", strlen("ORDER:")) == 0) {
                srand(time(0));
                int estimated_time = rand() % 20 + 10; // Random estimated time between 10 and 30 minutes
                char response[BUFFER_SIZE];
                snprintf(response, BUFFER_SIZE, "Your order will be ready in %d minutes.", estimated_time);
                send(*tcp_socket, response, strlen(response), 0);
            }
        }
    }

    close(*tcp_socket);
    pthread_exit(NULL);
}
