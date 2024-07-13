#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"   // Server IP address
#define SERVER_PORT 8080    // Server port
#define BUFFER_SIZE 1024    // Buffer size for receiving data

typedef enum {
    ERROR,
    MSG_KEEP_ALIVE,
    MSG_REQUEST_MENU,
    MSG_MENU,
    MSG_ORDER,
    MSG_ESTIMATED_TIME,
    MSG_RESTAURANT_OPTIONS,
    REST_UNAVALIABLE,
} message_type_t;

typedef struct {
    message_type_t type;
    char data[BUFFER_SIZE];
} message_t;

void *server_communication(void *arg);
void *keep_alive(void *arg);

void send_message(int sock, message_t *msg) {
    message_t network_msg;
    memset(&network_msg, 0, sizeof(message_t));  // Ensure message is zeroed out
    network_msg.type = htonl(msg->type); // Convert message type to network byte order
    strncpy(network_msg.data, msg->data, BUFFER_SIZE); // Copy data without modification

    printf("Sending message type %d (network byte order: %d)\n", msg->type, network_msg.type);
    if (send(sock, &network_msg, sizeof(network_msg), 0) != sizeof(network_msg)) {
        perror("Failed to send the complete message");
        exit(EXIT_FAILURE);
    }
}

void receive_message(int sock, message_t *msg) {
    message_t network_msg;
    memset(&network_msg, 0, sizeof(message_t));  // Ensure message is zeroed out
    if (recv(sock, &network_msg, sizeof(network_msg), 0) <= 0) {
        perror("Failed to receive the message");
        exit(EXIT_FAILURE);
    }

    msg->type = ntohl(network_msg.type); // Convert message type back to host byte order
    strncpy(msg->data, network_msg.data, BUFFER_SIZE); // Copy data without modification

    printf("Received message type %d (network byte order: %d)\n", msg->type, network_msg.type);
}

int main() {
    struct sockaddr_in server_addr; // Server address
    int sock; // Socket descriptor

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { // Create a socket for sending and receiving data
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET; // Set the address family to IPv4
    server_addr.sin_port = htons(SERVER_PORT); // Set the port number

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) { // Convert the IP address from text to binary form
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) { // Connect to the server at the specified address
        perror("Connection Failed");
        exit(EXIT_FAILURE);
    }

    // Create keep-alive thread
    pthread_t keep_alive_thread;
    if (pthread_create(&keep_alive_thread, NULL, keep_alive, (void *)&sock) != 0) { // Create a thread for sending keep-alive messages
        perror("Keep-alive thread creation failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    pthread_detach(keep_alive_thread); // Detach the keep-alive thread to allow it to run independently

    // Create server communication thread
    pthread_t server_comm_thread;
    if (pthread_create(&server_comm_thread, NULL, server_communication, (void *)&sock) != 0) { // Create a thread for server communication
        perror("Server communication thread creation failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    pthread_join(server_comm_thread, NULL); // Wait for the server communication thread to finish

    close(sock); // Close the socket
    return 0;
}

void *server_communication(void *arg) {
    int sock = *(int *)arg; // Socket descriptor
    message_t msg;
    memset(&msg, 0, sizeof(message_t));  // Ensure message is zeroed out

    while (1) {
        // Send a message to request the list of available restaurants
        msg.type = MSG_REQUEST_MENU;
        strcpy(msg.data, "REQUEST_MENU");
        printf("Client requesting available restaurants\n");
        send_message(sock, &msg);
        // Receive restaurant options from server
        receive_message(sock, &msg);
        if(msg.type == 0){
            receive_message(sock,&msg);
        }
        printf("Client received message type %d\n", msg.type);

        if (msg.type == MSG_RESTAURANT_OPTIONS) {
            printf("Restaurants:\n%s\n", msg.data); // Print the received data

            // Choose a restaurant
            printf("Enter the number of the restaurant you want to order from: "); // Prompt the user to enter a choice
            fflush(stdout); // Flush the output buffer
            int choice; // User choice
            if (scanf("%d", &choice) != 1 || choice < 1 || choice > 3) { // Read the user choice
                printf("Invalid choice.\n");
                close(sock);
                exit(EXIT_FAILURE);
            }

            msg.type = MSG_ORDER;
            sprintf(msg.data, "%d", choice);
            send_message(sock, &msg);

            // Receive response from server
            receive_message(sock, &msg);
            if(msg.type == 0){
                receive_message(sock, &msg);
            }
            printf("Client received message type ->> %d\n", msg.type);
            // receive_message(sock, &msg);
            // printf("Client received message type ->> %d\n", msg.type);
            if (msg.type == REST_UNAVALIABLE) {
                printf("%s", msg.data);
                continue;
            } else if (msg.type == MSG_MENU) {
                printf("Menu received:\n%s\n", msg.data); // Print the received menu

                // Choose a meal
                printf("Enter the number of the meal you want to order: "); // Prompt the user to enter a choice
                fflush(stdout); // Flush the output buffer
                int meal_choice; // User choice
                if (scanf("%d", &meal_choice) != 1 || meal_choice < 1 || meal_choice > 10) { // Read the user choice
                    printf("Invalid choice.\n");
                    close(sock);
                    exit(EXIT_FAILURE);
                }

                msg.type = MSG_ORDER;
                sprintf(msg.data, "ORDER: %d", meal_choice);
                send_message(sock, &msg);

                // Receive time estimation from server
                receive_message(sock, &msg);
                if(msg.type == 0){
                    receive_message(sock, &msg);
                }
                if (msg.type != MSG_ESTIMATED_TIME) {
                    perror("Expected estimated time message");
                    close(sock);
                    exit(EXIT_FAILURE);
                }
                printf("Estimated time for your order: %s\n", msg.data); // Print the time estimation
            } else {
                perror("Unexpected message type");
                close(sock);
                exit(EXIT_FAILURE);
            }
        } else {
            perror("Expected restaurant options message");
            close(sock);
            exit(EXIT_FAILURE);
        }
        break; // Exit the loop once an order is successfully placed and time estimation is received
    }
    return NULL; // Return from the thread
}

// Keep-alive thread function
void *keep_alive(void *arg) {
    int sock = *(int *)arg; // Socket descriptor
    message_t keep_alive_msg;
    memset(&keep_alive_msg, 0, sizeof(message_t));  // Ensure message is zeroed out
    keep_alive_msg.type = MSG_KEEP_ALIVE;
    strcpy(keep_alive_msg.data, "KEEP_ALIVE");

    while (1) { // Loop to send keep-alive messages
        sleep(30); // Send keep-alive message every 30 seconds
        send_message(sock, &keep_alive_msg);
        printf("\nKEEP_ALIVE sent\n"); // Print the keep-alive message
    }
    return NULL; // Return from the thread
}
