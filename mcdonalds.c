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

typedef enum {
    ERROR,
    MSG_KEEP_ALIVE,
    MSG_REQUEST_MENU,
    MSG_MENU,
    MSG_ORDER,
    MSG_ESTIMATED_TIME,
    MSG_RESTAURANT_OPTIONS,
    REST_UNAVALIABLE
} message_type_t;

typedef struct {
    message_type_t type;
    char data[BUFFER_SIZE];
} message_t;

void *multicast_listener(void *arg);
void *tcp_communication_handler(void *arg);
void *keep_alive_handler(void *arg);
int sent_menu = 0;
pthread_mutex_t tcp_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for TCP socket

void send_message(int sock, message_t *msg) {
    msg->type = htonl(msg->type);
    if (send(sock, msg, sizeof(*msg), 0) != sizeof(*msg)) {
        perror("Failed to send the complete message");
        exit(EXIT_FAILURE);
    }
}

void receive_message(int sock, message_t *msg) {
    if (recv(sock, msg, sizeof(*msg), 0) <= 0) {
        perror("Failed to receive the message");
        exit(EXIT_FAILURE);
    }
    msg->type = ntohl(msg->type);
}

int main() {
    pthread_t multicast_thread, tcp_thread, keep_alive_thread;
    int tcp_socket;
    pthread_create(&tcp_thread, NULL, tcp_communication_handler, &tcp_socket);
    pthread_create(&multicast_thread, NULL, multicast_listener, &tcp_socket);
    pthread_create(&keep_alive_thread, NULL, keep_alive_handler, &tcp_socket);
    
    pthread_join(multicast_thread, NULL);
    pthread_join(tcp_thread, NULL);
    pthread_join(keep_alive_thread, NULL);

    return 0;
}

// Function to listen to the multicast channel
void *multicast_listener(void *arg) {
    int tcp_socket = *(int *)arg;       // tcp socket
    struct sockaddr_in multicast_addr; // Multicast address
    struct ip_mreqn mreq;              // Multicast request structure
    int multicast_socket;              // Multicast socket
    socklen_t addr_len = sizeof(multicast_addr); // Address length for multicast address
    message_t msg;

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
        int bytes_received = recvfrom(multicast_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&multicast_addr, &addr_len);
        if (bytes_received < 0) {
            perror("recvfrom failed");
            continue;
        }

        // Process the received message
        printf("%d <--- message type! ",msg.type);
        fflush(stdout);
        if (msg.type == MSG_REQUEST_MENU) {
            if (sent_menu == 0) {
                sent_menu = 1;
                printf("Multicast request received. Preparing to send menu data via TCP...\n"); // Debug print statement

                // Send menu data back to the server via TCP
                message_t menu_msg;
                menu_msg.type = MSG_MENU;
                strcpy(menu_msg.data, "McDonalds 1. Big Mac Meal - $5.99\n2. Crispy Chicken Meal - $6.99\n3. Filet-O-Fish Meal - $5.49\n4. McChicken Meal - $4.99\n5. Quarter Pounder Meal - $6.49\n6. Chicken Nuggets Meal - $5.99\n7. Double Cheeseburger Meal - $4.99\n8. McDouble Meal - $4.49\n9. McRib Meal - $6.99\n10. Sausage McMuffin Meal - $3.99");
                pthread_mutex_lock(&tcp_mutex);
                send_message(tcp_socket, &menu_msg);
                pthread_mutex_unlock(&tcp_mutex);
            } else {
                printf("Menu Already Sent\n");
                fflush(stdout);
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
    message_t msg;

    if ((*tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("TCP socket creation failed");
        pthread_exit(NULL);
    }

    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    tcp_addr.sin_port = htons(MCDONALDS_PORT);

    if (connect(*tcp_socket, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("TCP connect failed");
        close(*tcp_socket);
        pthread_exit(NULL);
    }

    // Register the restaurant with the server
    message_t menu_msg;
    menu_msg.type = MSG_MENU;
    strcpy(menu_msg.data, "McDonalds 1. Big Mac Meal - $5.99\n2. Crispy Chicken Meal - $6.99\n3. Filet-O-Fish Meal - $5.49\n4. McChicken Meal - $4.99\n5. Quarter Pounder Meal - $6.49\n6. Chicken Nuggets Meal - $5.99\n7. Double Cheeseburger Meal - $4.99\n8. McDouble Meal - $4.49\n9. McRib Meal - $6.99\n10. Sausage McMuffin Meal - $3.99");   // Restaurant menu data example

    printf("McDonald's restaurant connected to server via TCP\n");

    while (1) {
        receive_message(*tcp_socket, &msg);

        switch (msg.type) {
            case MSG_ORDER:
                srand(time(0));
                int estimated_time = rand() % 20 + 10; // Random estimated time between 10 and 30 minutes
                message_t response;
                response.type = MSG_ESTIMATED_TIME;
                snprintf(response.data, BUFFER_SIZE, "Your order will be ready in %d minutes.", estimated_time);
                pthread_mutex_lock(&tcp_mutex);
                send_message(*tcp_socket, &response);
                pthread_mutex_unlock(&tcp_mutex);
                break;
            default:
                printf("Unknown message type received from server: %d\n", msg.type);
                break;
        }
    }

    close(*tcp_socket);
    pthread_exit(NULL);
}

// Function to send keep-alive messages to the server
void *keep_alive_handler(void *arg) {
    int *tcp_socket = (int *)arg;
    while (1) {
        sleep(60); // Send keep-alive every 60 seconds
        message_t keep_alive_msg;
        keep_alive_msg.type = MSG_KEEP_ALIVE;
        strcpy(keep_alive_msg.data, "KEEP_ALIVE");
        pthread_mutex_lock(&tcp_mutex);
        send_message(*tcp_socket, &keep_alive_msg);
        pthread_mutex_unlock(&tcp_mutex);
        printf("\nKeep-alive sent to server\n");
    }
    pthread_exit(NULL);
}
