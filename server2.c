// Missing: token manager, generate_token, restaurant connection



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <asm-generic/socket.h>

#define CLIENT_PORT 8080        // Port for clients to connect
#define MULTICAST_GROUP "239.0.0.1" // Multicast group for restaurants to listen
#define MULTICAST_PORT 5555     // Port for multicast communication
#define MCDONALDS_PORT 5556     // TCP Port for McDonald's
#define DOMINOS_PORT 5557       // TCP Port for Domino's
#define BUFFER_SIZE 2048        // Buffer size for messages
#define TOKEN_TIMEOUT 180       // 3 minutes
#define RESTAURANT_TIMEOUT 180  // 3 minutes
#define MAX_CLIENTS 5           // Maximum number of clients that can connect
#define MAX_RESTAURANTS 5       // Maximum number of restaurants that can connect
#define KEEP_ALIVE_INTERVAL 5    // Interval for sending keep-alive messages
#define KEEP_ALIVE_TIMEOUT 15    // Timeout for keep-alive messages
#define INTERFACE_NAME "enp0s3"    // Interface name for binding the socket

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

typedef struct {
    int restaurant_socket;      // Socket for restaurant connection
    char name[BUFFER_SIZE];     // Restaurant name
    struct sockaddr_in address; // Address structure for restaurant
    char menu[BUFFER_SIZE];     // Restaurant menu
    time_t last_keep_alive;     // Last keep-alive time for the restaurant
    int active;                 // Active status of the restaurant
} restaurant;

typedef struct {
    int client_socket;    // Socket for client connection
    struct sockaddr_in address;    // Address structure for client
    time_t last_keep_alive;    // Last keep-alive time for the client
    char token[BUFFER_SIZE];    // Authentication token for the client
    int state;    // State of the client
    restaurant chosen_restaurant;    // Chosen restaurant by the client
    //pthread_t thread_id; 
} client;

typedef struct {
    int socket_fd;    // Socket file descriptor
    struct sockaddr_in address;    // Address structure
    int addrlen;    // Length of the address structure
} SocketInfo;

typedef struct {
    message_type_t type;    // Message type
    char data[BUFFER_SIZE];    // Message data
} message;

typedef struct {
    int socket;    // Socket for client
    message msg;    // Message structure
} clientMsg;

typedef struct {
    int socket;    // Socket for restaurant
    message msg;    // Message structure
} restaurantMsg;

pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for client
pthread_mutex_t restaurant_mutex = PTHREAD_MUTEX_INITIALIZER;     // Mutex for restaurant

client clients[MAX_CLIENTS]; // Array to store client information
restaurant restaurants[MAX_RESTAURANTS]; // Array to store restaurant information

void send_message(int sock, int msg_type, const char *msg_data) {
    message msg;    // Message structure
    msg.type = msg_type;    // Set message type
    strncpy(msg.data, msg_data, sizeof(msg.data) - 1);    // Copy message data
    msg.data[sizeof(msg.data) - 1] = '\0';  // Ensure null-termination

    // Send the message
    send(sock, &msg, sizeof(msg), 0);
}

void send_multicast_message(int sock, struct sockaddr_in addr, int msg_type, const char *msg_data) {
    message msg;    // Message structure
    msg.type = msg_type;    // Set message type
    strncpy(msg.data, msg_data, sizeof(msg.data) - 1);    // Copy message data
    msg.data[sizeof(msg.data) - 1] = '\0';  // Ensure null-termination

    // Send the message
    sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&addr, sizeof(addr));
}

void handle_client_keep_alive(int client_sock) {
    pthread_mutex_lock(&client_mutex);    // Lock the client mutex to prevent race conditions
    for (int i = 0; i < MAX_CLIENTS; i++) {    // Iterate through the clients
        if (clients[i].client_socket == client_sock) {    // Find the client with the matching socket
            clients[i].last_keep_alive = time(NULL);    // Update the last keep-alive time
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);    // Unlock the client mutex after updating the information
}

void handle_restaurant_keep_alive(int restaurant_sock) {
    pthread_mutex_lock(&restaurant_mutex);    // Lock the restaurant mutex to prevent race conditions
    for (int i = 0; i < MAX_RESTAURANTS; i++) {    // Iterate through the restaurants
        if (restaurants[i].restaurant_socket == restaurant_sock) {    // Find the restaurant with the matching socket
            restaurants[i].last_keep_alive = time(NULL);    // Update the last keep-alive time
            break;
        }
    }
    pthread_mutex_unlock(&restaurant_mutex);    // Unlock the restaurant mutex after updating the information
}

void *active_restaurants_manager(void *arg) {
    while (1) { // Loop to check for expired keep-alive signals
        sleep(KEEP_ALIVE_INTERVAL);    // Sleep for the keep-alive interval duration (5 seconds)
        time_t current_time = time(NULL);   // Get current time

        pthread_mutex_lock(&restaurant_mutex); // Lock restaurants array to prevent from multiple threads accessing it simultaneously
        for (int i = 0; i < MAX_RESTAURANTS; i++) {  // Loop through restaurants array
            if (restaurants[i].restaurant_socket != 0 && difftime(current_time, restaurants[i].last_keep_alive) > RESTAURANT_TIMEOUT && restaurants[i].active == 1 ) {  // Check if keep-alive is expired
                printf("Keep-alive expired for restaurant: %s\n", restaurants[i].name);   // Print message for expired keep-alive
                restaurants[i].active = 0;    // Set restaurant as inactive
            }
        }
        pthread_mutex_unlock(&restaurant_mutex);   // Unlock restaurants array
    }
    return NULL;
}

void* keep_alive_check_client(void* arg) { // If client didnt send keep alive
    while (1) {
        // printf("Sent keep alive message\n");
        sleep(KEEP_ALIVE_INTERVAL);
        // Check if specific clients did not send keep alive messages
        pthread_mutex_lock(&client_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            time_t current_time = time(NULL);    // Get the current time
            if (current_time - clients[i].last_keep_alive > KEEP_ALIVE_TIMEOUT) {    // Check if the client has not sent a keep-alive message
                printf("%s did not send keep alive messages. remove his process from the server...\n", clients[i].token);
                close(clients[i].client_socket); // Maybe with unicast
                for (int j = i; j < MAX_CLIENTS - 1; j++) {    // Remove the client from the array
                    clients[j] = clients[j + 1];    // Shift the elements to the left
                }
            }
        }
        pthread_mutex_unlock(&client_mutex);    // Unlock the client mutex after checking the clients
    }
}

void* handle_client_msg(void* arg){
    clientMsg* client_msg = (clientMsg*)arg;
    message* msg = &client_msg->msg;
    client *client;
    int sock = client_msg->socket;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].client_socket == sock) {
            printf("Client %s sent message type %d: %s\n", clients[i].token, msg->type, msg->data);
            *client = clients[i];
            break;
        }
    }
    switch (msg->type)
    {
        case MSG_REQUEST_MENU:
            // Send restaurant options to client
            printf("Server got a restaurant options request, now showing the client.\n");
            msg->type = MSG_RESTAURANT_OPTIONS;
            strcpy(msg->data, "Choose a restaurant:\n1. McDonalds\n2. Dominos\n3. Taco Bell\n");
            send_message(sock, msg->type, msg->data);
            break;
        case MSG_CHOOSE_RESTAURANT:
            // Handle client's restaurant choice
            int choice = atoi(msg->data);
            const char *rest = NULL;
            switch (choice) {
                case 1:
                    rest = "McDonalds";
                    printf("Server chose McDonalds\n");
                    client->state = 1;    // Symbolize that the client chose a restaurant
                    break;
                case 2:
                    rest = "Dominos";
                    client->state = 1;    // Symbolize that the client chose a restaurant
                    break;
                case 3:
                    rest = "Taco Bell";
                    client->state = 1;    // Symbolize that the client chose a restaurant
                break;
                default:
                    printf("Invalid restaurant choice\n");
                    close(sock);
                    //pthread_exit(NULL);
                pthread_mutex_lock(&restaurant_mutex);
                int found = 0;
                for (int i = 0; i < MAX_RESTAURANTS; i++) {
                    if (strcmp(restaurants[i].name, rest) == 0 && restaurants[i].active) {
                        client->chosen_restaurant = restaurants[i];
                        found = 1;
                        strncpy(msg->data, restaurants[i].menu, BUFFER_SIZE);
                        msg->type = MSG_MENU;
                        send_message(sock, msg->type, msg->data);
                        client->state = 2;    // Symbolize that the client received the menu
                        break;
                    }
                }
                pthread_mutex_unlock(&restaurant_mutex);
                if (!found) {
                    client->state = 0;    // Symbolize that the client did not choose a valid restaurant, back to the initial state
                    msg->type = REST_UNAVALIABLE;
                    snprintf(msg->data, BUFFER_SIZE, "not available");
                    send_message(sock, msg->type, msg->data);
                    printf("Sending message type %d\n", msg->type);
                    close(sock);
                    //pthread_exit(NULL);
                }
            }
            break;
        case MSG_ORDER:
            // Handle client's order
            printf("Server got client order\n");
            if (client->state != 2) {
                printf("Client did not choose a restaurant or did not receive the menu\n");
                close(sock);
                //pthread_exit(NULL);
            }
            restaurant restaurant = client->chosen_restaurant;
            pthread_mutex_lock(&restaurant_mutex);
            for (int i = 0; i < MAX_RESTAURANTS; i++) {
                if (restaurants[i].restaurant_socket == restaurant.restaurant_socket) {
                    strncpy(msg->data, clients[i].token, BUFFER_SIZE);
                    msg->type = MSG_ORDER;
                    send_message(restaurant.restaurant_socket, msg->type, msg->data);
                    printf("Sending message type %d, to %s", msg->type, restaurant.name);
                    client->state = 3;    // Symbolize that the client sent the order and waits for the estimated time
                    break;
                }
            }
            break;

        case MSG_KEEP_ALIVE:
            handle_client_keep_alive(sock);
            break;

        
        default:
            printf("Invalid message type %d\n", msg->type);
            break;
    }
    return NULL;
}

void* listen_for_client_messages(void* args){
    // Every Unicast message coming for a specific client 
    // will be handled in another thread
    int sock = *(int*)args;
    message msg;
    msg.type = -1;
    clientMsg client_msg;
    int bytes_receive_unicast = 0;
    while(1){
        memset(&msg, 0, sizeof(msg));
        bytes_receive_unicast = recv(sock, &msg, sizeof(msg), 0);
        printf("Bytes received: %d\n", bytes_receive_unicast);
        if (bytes_receive_unicast == 0) { // Closed socket
            close(sock);
            pthread_mutex_lock(&client_mutex);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].client_socket == sock) {
                    printf("%s disconnected\n", clients[i].token);
                    for (int j = i; j < MAX_CLIENTS - 1; j++) {
                        clients[j] = clients[j + 1];
                    }
                    break;
                }
            }
            pthread_mutex_unlock(&client_mutex);
            return NULL;
        }
        else if (bytes_receive_unicast < 0) {
            perror("Error receiving message");
            close(sock);
            return NULL;
        }
        // Check if a message is empty, if so - continue
        if (msg.type == 0 && strlen(msg.data) == 0) {
            printf("Empty message received\n");
            continue;
        }
        client_msg.msg = msg;
        client_msg.socket = sock;
        printf("Client sent message %d: %s\n", msg.type, msg.data);
        pthread_t handle_message_thread;
        pthread_create(&handle_message_thread, NULL, handle_client_msg, (void*)&client_msg);
        pthread_detach(handle_message_thread);
    }
    return NULL;
}

void* handle_restaurant_msg(void* arg){
    restaurantMsg* rest_msg = (restaurantMsg*)arg;
    message* msg = &rest_msg->msg;
    int sock = rest_msg->socket;
    restaurant *restaurant;
    for (int i = 0; i < MAX_RESTAURANTS; i++) {
        if (restaurants[i].restaurant_socket == sock) {
            printf("Restaurant %s sent message type %d: %s\n", restaurants[i].name, msg->type, msg->data);
            *restaurant = restaurants[i];
            break;
        }
    }
    switch (msg->type)
    {
        case MSG_MENU:
            // Update the menu for the restaurant
            pthread_mutex_lock(&restaurant_mutex);
            for (int i = 0; i < MAX_RESTAURANTS; i++) {
                if (restaurants[i].restaurant_socket == sock) {
                    strncpy(restaurants[i].menu, msg->data, BUFFER_SIZE);
                    break;
                }
            }
            restaurant->active = 1;    // Set the restaurant as active
            restaurant->last_keep_alive = time(NULL);    // Update the last keep-alive time
            pthread_mutex_unlock(&restaurant_mutex);
            break;
            
        case MSG_KEEP_ALIVE:
            handle_restaurant_keep_alive(sock);
            break;
        
        case MSG_ESTIMATED_TIME:
            // Send estimated time to the client
            printf("Server got estimated time from restaurant\n");
            pthread_mutex_lock(&client_mutex);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].state == 3 && clients[i].chosen_restaurant.restaurant_socket == sock) {    // Check if the client is waiting for the estimated time
                    msg->type = MSG_ESTIMATED_TIME;    // Set the message type
                    strncpy(msg->data, msg->data, BUFFER_SIZE);    // Copy the estimated time to the message data
                    send_message(clients[i].client_socket, msg->type, msg->data);    // Send the estimated time to the client
                    printf("Sending message type %d to client %s\n", msg->type, clients[i].token);
                    break;
                }
                else {
                    printf("Client is not waiting for estimated time\n");
                }
            }
            pthread_mutex_unlock(&client_mutex);
            break;
            
        default:
            printf("Invalid message type %d\n", msg->type);
            break;
    }
    return NULL;
}

void* listen_for_restaurant_messages(void* args){
    // Every Unicast message coming for a specific client
    // will be handled in another thread
    int sock = *(int*)args;
    message msg;
    msg.type = -1;
    restaurantMsg rest_msg;
    int bytes_receive_unicast = 0;
    while(1){
        memset(&msg, 0, sizeof(msg));
        bytes_receive_unicast = recv(sock, &msg, sizeof(msg), 0);
        printf("Bytes received: %d\n", bytes_receive_unicast);
        if (bytes_receive_unicast == 0) { // Closed socket
            close(sock);
            pthread_mutex_lock(&restaurant_mutex);
            for (int i = 0; i < MAX_RESTAURANTS; i++) {
                if (restaurants[i].restaurant_socket == sock) {
                    printf("%s disconnected\n", restaurants[i].name);
                    for (int j = i; j < MAX_RESTAURANTS - 1; j++) {
                        restaurants[j] = restaurants[j + 1];
                    }
                    break;
                }
            }
            pthread_mutex_unlock(&client_mutex);
            return NULL;
        }
        else if (bytes_receive_unicast < 0) {
            perror("Error receiving message");
            close(sock);
            return NULL;
        }
        // Check if a message is empty, if so - continue
        if (msg.type == 0 && strlen(msg.data) == 0) {
            printf("Empty message received\n");
            continue;
        }
        rest_msg.msg = msg;
        rest_msg.socket = sock;
        printf("Client sent message %d: %s\n", msg.type, msg.data);
        pthread_t handle_message_thread;
        pthread_create(&handle_message_thread, NULL, handle_restaurant_msg, (void*)&rest_msg);
        pthread_detach(handle_message_thread);
    }
    return NULL;
}

void *menu_update_manager(void *arg) {
    int multicast_socket;
    struct sockaddr_in multicast_addr;
    message msg;
    memset(&msg, 0, sizeof(message));  // Ensure message is zeroed out

    if ((multicast_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Multicast socket creation failed");
        pthread_exit(NULL);
    }

    int reuse = 1;
    if (setsockopt(multicast_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0) {
        perror("Setting SO_REUSEADDR error");
        close(multicast_socket);
        pthread_exit(NULL);
    }

    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    printf("Server casting on multicast group %s:%d\n", MULTICAST_GROUP, MULTICAST_PORT); // Print the multicast group information

    while (1) {
        msg.type = MSG_REQUEST_MENU;
        strcpy(msg.data, "REQUEST_MENU");
        if (sendto(multicast_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0) {
            perror("Multicast sendto failed");
            close(multicast_socket);
            continue;
        }
        printf("Server sent a request menu message to the multicast group %s:%d\n", MULTICAST_GROUP, MULTICAST_PORT); // Print the multicast group information
        sleep(30); // Wait 30 seconds before the next update
    }

    close(multicast_socket);
    return NULL;
}




int main() {
    int server_fd_client, new_socket;    // Server socket file descriptor and new socket file descriptor
    struct sockaddr_in address;    // Address structure for server
    int addrlen = sizeof(address);    // Length of the address structure
    pthread_t thread_id;    // Thread ID for the menu update manager

    // Create TCP socket
    if ((server_fd_client = socket(AF_INET, SOCK_STREAM, 0)) == 0) {    // Create a socket for TCP connection
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd_client, SOL_SOCKET, SO_BINDTODEVICE, INTERFACE_NAME, strlen(INTERFACE_NAME)) < 0) {    // Bind the socket to a specific interface
        perror("setsockopt(SO_BINDTODEVICE) failed");
        close(server_fd_client);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;    // Set address family to IPv4
    address.sin_addr.s_addr = INADDR_ANY;    // Set server IP address
    address.sin_port = htons(CLIENT_PORT);    // Set server port


    if (bind(server_fd_client, (struct sockaddr *)&address, sizeof(address)) < 0) {    // Bind the socket to the address and port
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd_client, MAX_CLIENTS) < 0) {    // Listen for incoming connections
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    struct ifaddrs *ifaddr, *ifa;    // Interface address structure
    char ip[INET_ADDRSTRLEN];    // IP address string
    if (getifaddrs(&ifaddr) == -1) {    // Get the interface address
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {    // Iterate through the interface address
        if (ifa->ifa_addr == NULL)    // Check if the address is NULL
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {    // Check if the address family is IPv4
            if (strcmp(ifa->ifa_name, INTERFACE_NAME) == 0) {    // Check if the interface name matches
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;    // Get the address structure
                inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));    // Convert the address to a string
            }
        }
    }
    printf("Connect to server with IP: %s and port: %d\n", ip, ntohs(address.sin_port));
    //printf("Server started with authentication code %s\nWaiting for connections...\n", auth_code);

    SocketInfo info;    // Socket information structure
    info.socket_fd = server_fd_client;    // Set the socket file descriptor
    info.address = address;    // Set the address structure
    info.addrlen = addrlen;    // Set the address length


    int multicast_sock;    // Multicast socket
    struct sockaddr_in multicast_addr;    // Multicast address
    struct ip_mreq multicast_request;    // Multicast request structure

    multicast_sock = socket(AF_INET, SOCK_DGRAM, 0); // UDP socket
    memset(&multicast_addr, 0, sizeof(multicast_addr));    // Clear the multicast address structure
    multicast_addr.sin_family = AF_INET;    // Set the address family to IPv4
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);    // Set the multicast group address
    multicast_addr.sin_port = htons(MULTICAST_PORT);    // Set the port number


    SocketInfo multicast_info;    // Multicast socket information structure
    multicast_info.socket_fd = multicast_sock;    // Set the socket file descriptor
    multicast_info.address = multicast_addr;      // Set the address structure
    multicast_info.addrlen = sizeof(multicast_addr);    // Set the address length

    for(int i = 0; i < MAX_CLIENTS; i++){
        clients[i].last_keep_alive = time(NULL);    // Set the last keep-alive time to the current time
    }
    pthread_t client_keep_alive_thread;    // Thread ID for the client keep-alive thread
    pthread_create(&client_keep_alive_thread, NULL, keep_alive_check_client, NULL); // Check for clients keep-alive
    pthread_detach(client_keep_alive_thread);    // Detach the client keep-alive thread
    pthread_t active_restaurants_manager_thread;    // Thread ID for the active restaurants manager thread
    pthread_create(&active_restaurants_manager_thread, NULL, active_restaurants_manager, (void*)&multicast_info); // Check for active restaurants
    pthread_detach(active_restaurants_manager_thread);    // Detach the active restaurants manager thread

    for(int i = 0; i < MAX_CLIENTS; i++){
        pthread_t listen_for_messages_thread;    // Thread ID for the listen for messages thread
        pthread_create(&listen_for_messages_thread, NULL, listen_for_client_messages, (void*)&clients[i].client_socket);    // Listen for messages
        pthread_detach(listen_for_messages_thread);    // Detach the listen for messages thread
    }
    
    // Kill the threads
    close(server_fd_client);
    close(multicast_sock);
    return 0;
}
