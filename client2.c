
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>


#define SERVER_IP "127.0.0.1"   // Server IP address
#define SERVER_PORT 8080    // Server port
#define BUFFER_SIZE 1024    // Buffer size for receiving data
#define KEEP_ALIVE_INTERVAL 5    // Interval for sending keep-alive messages

typedef enum {    // Message types
    ERROR,
    MSG_KEEP_ALIVE,
    MSG_REQUEST_MENU,
    MSG_MENU,
    MSG_ORDER,
    MSG_ESTIMATED_TIME,
    MSG_RESTAURANT_OPTIONS,
    REST_UNAVALIABLE,
    MSG_CHOOSE_RESTAURANT,
} message_type_t;

// Define the message structure
typedef struct {
    message_type_t type;  // Message type
    char data[BUFFER_SIZE];  // Message data
} message;

typedef struct {
    int socket;
    message msg;
} messageThreadArgs;


int order_status = 0;

void* handle_message(void* args);
void *server_communication(void *arg);
void *keep_alive(void *arg);



void send_message(int sock, message *msg) {
    message network_msg;
    memset(&network_msg, 0, sizeof(message));  // Ensure message is zeroed out
    network_msg.type = htonl(msg->type); // Convert message type to network byte order
    strncpy(network_msg.data, msg->data, BUFFER_SIZE); // Copy data without modification

    printf("Sending message type %d (network byte order: %d)\n", msg->type, network_msg.type);
    if (send(sock, &network_msg, sizeof(network_msg), 0) != sizeof(network_msg)) {    // Send the message
        perror("Failed to send the complete message");
        //exit(EXIT_FAILURE);
    }
}

int establish_connection(){
    // printf("GOT HERE\n");
    int sock = 0;    // Socket file descriptor
    struct sockaddr_in serv_addr;    // Server address structure
    char *hello_msg = "Ready";    // Message to send to the server
    char buffer[BUFFER_SIZE] = {0};    // Buffer for receiving data
    char server_ip[16];    // Server IP address
    int server_port;    // Server port
    int address_correctness = 0;    // Flag to check if the address is correct

    while((sock = socket(AF_INET, SOCK_STREAM, 0)) >= 0) {    // Create a socket
        fflush(stdin);    // Flush the input buffer
        if (!address_correctness) {    // *****Check if can be different*****
            printf("Enter the IP address of the server: ");
            scanf("%s", server_ip);    // Get the IP address from the user
            printf("Enter the port number of the server: ");
            scanf("%d", &server_port);    // Get the port number from the user
        }
        serv_addr.sin_family = AF_INET;    // Set the address family
        serv_addr.sin_port = htons(server_port);    // Set the port number
        if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {    // Convert the IP address to binary form
            printf("\n**Invalid address**\n");
            close(sock);
            continue;
        }
        else {
            address_correctness = 1;    // Set the flag to indicate that the address is correct
        }

        if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {    // Connect to the server
            if(errno == ECONNREFUSED) {    // Check if the connection was refused
                printf("Wrong IP or Port. Try again...\n");
                address_correctness = 0;
                continue;
            }
            else {    // Connection failed
                perror("Connection failed");
                address_correctness = 0;
                continue;
            }
        }
        else {    // Connection successful
            return sock;
        }
    }
    return -1;    // Return -1 if the connection failed
}

void* listen_to_server(void* args){ // Handles first connections with the server and authentication
    int bytes_receive_unicast;    // Number of bytes received
    message msg_unicast;    // Message structure
    int sock = *(int*)args;    // Socket file descriptor
    while(1){ // Unicast
        memset(msg_unicast.data, 0, sizeof(msg_unicast.data));    // Clear the message buffer
        bytes_receive_unicast = recv(sock, &msg_unicast, sizeof(msg_unicast), 0); // Receive the message
        if (bytes_receive_unicast > 0) {    // Message received
            pthread_t handle_unicast_msg;    // Thread to handle the message
            messageThreadArgs* thread_args = malloc(sizeof(messageThreadArgs));    // Allocate memory for the thread arguments
            thread_args->socket = sock;    // Set the socket file descriptor
            thread_args->msg = msg_unicast;    // Set the message
            // printf("Message received: %s\n", msg_unicast.data);
            pthread_create(&handle_unicast_msg, NULL, handle_message, (void*)thread_args);    // Create the thread
            pthread_join(handle_unicast_msg, NULL);    // Wait for the thread to finish
            continue; // ! REMOVE ALL CONTINUES WHEN MULTICAST IS IMPLEMENTED
        }
        else if (bytes_receive_unicast == 0) { // Socket closed
            printf("Server disconnected\n");
            sock = establish_connection();    // Reconnect to the server
            //send_authentication_code(sock);
            continue;
        }
        else if (errno == EWOULDBLOCK || errno == EAGAIN) {
            // No message received, do something else
            continue;
        }
        else {
            perror("Error in recv function");
            printf("Message received: %s\n", msg_unicast.data);
            break;
        }
    }
    return NULL;
}

void* handle_message(void* args) {
    messageThreadArgs* thread_args = (messageThreadArgs*)args;    // Get the thread arguments
    message msg = thread_args->msg;    // Get the message
    int client_socket = thread_args->socket;    // Get the socket file descriptor
    //int name_flag = 1;    // Flag to check if the name is correct
    if (order_status == 0) {
        msg.type = MSG_REQUEST_MENU;
        strcpy(msg.data, "REQUEST_MENU");    // Copy the message data
        printf("Client requesting available restaurants\n");
        send_message(client_socket, &msg);    // Send the message to the server
    }
    switch (msg.type) {
        case MSG_RESTAURANT_OPTIONS:
            printf("Restaurants available: \n%s\n", msg.data);
            printf("Enter the number of the restaurant you want to order from: "); // Prompt the user to enter a choice
            fflush(stdout); // Flush the output buffer
            int choice; // User choice
            msg.type = MSG_CHOOSE_RESTAURANT;
            sprintf(msg.data, "%d", choice);    // Copy the user choice to the message data
            send_message(client_socket, &msg);    // Send the message to the server
            order_status = 1;
            break;

        case MSG_MENU:    // Menu message
            printf("Menu received: %s\n", msg.data);    // Print the menu
            // Choose a meal
            printf("Enter the number of the meal you want to order: "); // Prompt the user to enter a choice
            fflush(stdout); // Flush the output buffer
            int meal_choice; // User choice
            if (scanf("%d", &meal_choice) != 1 || meal_choice < 1 || meal_choice > 10) { // Read the user choice
                printf("Invalid choice.\n");
            }
            msg.type = MSG_ORDER;
            sprintf(msg.data, "ORDER: %d", meal_choice);    // Copy the user choice to the message data
            send_message(client_socket, &msg);    // Send the message to the server
            order_status = 2;
            break;

        case MSG_ESTIMATED_TIME:    // Estimated time message
            printf("Estimated time: %s\n", msg.data);    // Print the estimated time
            break;

        default:
            printf("Unknown message type: %d\n", msg.type);
            break;
    }
    free(thread_args);
    return NULL;
}


void* keep_alive(void *arg) {
    int sock = *(int*)arg;    // Socket file descriptor
    message keep_alive_msg;    // Keep alive message
    keep_alive_msg.type = MSG_KEEP_ALIVE;    // Set the message type
    while(1) {    // Keep alive loop
        send_message(sock, &keep_alive_msg);    // Send the keep alive message
        sleep(KEEP_ALIVE_INTERVAL);    // Sleep for 5 seconds
    }
    return NULL;
}

int main() {
    int sock;
    sock = establish_connection(); // DONE When IP and Port are correct
    //send_authentication_code(sock);

    pthread_t handle_unicast_thread;
    pthread_create(&handle_unicast_thread, NULL, listen_to_server, (void*)&sock);
    pthread_detach(handle_unicast_thread);

    pthread_t keep_alive_thread;
    pthread_create(&keep_alive_thread, NULL, keep_alive, (void*)&sock);
    pthread_detach(keep_alive_thread);

    while(1);
    printf("Exiting...\n");
    return 0;
}
