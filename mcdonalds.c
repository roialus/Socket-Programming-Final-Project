#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>

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

typedef struct {
    int client_socket;          // Socket for client connection
    char token[BUFFER_SIZE];    // Token for client identification
    time_t last_keep_alive;     // Last keep-alive time for the client
    pthread_t thread_id;        // Thread ID for client handling
} client_info_t;                // Structure to store client information

typedef struct {
    int restaurant_socket;      // Socket for restaurant connection
    char name[BUFFER_SIZE];     // Restaurant name
    struct sockaddr_in address; // Address structure for restaurant
    char menu[BUFFER_SIZE];     // Restaurant menu
    time_t last_keep_alive;     // Last keep-alive time for the restaurant
    int active;                 // Active status of the restaurant
} restaurant_info_t;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex for clients array
pthread_mutex_t restaurants_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for restaurants array

client_info_t clients[MAX_CLIENTS]; // Array to store client information
restaurant_info_t restaurants[MAX_RESTAURANTS]; // Array to store restaurant information

void *handle_client(void *arg);
void *token_manager(void *arg);
void *menu_update_manager(void *arg);
void *active_restaurants_manager(void *arg);
char *generate_token();
void send_restaurant_options(client_info_t *client);
void send_menu_to_client(client_info_t *client, const char *restaurant);
void send_order_to_restaurant(client_info_t *client, const char *order, const char *restaurant);
void *restaurant_tcp_handler_mcdonalds(void *arg);
void *restaurant_tcp_handler_dominos(void *arg);
void send_message(int sock, message_t *msg);
void receive_message(int sock, message_t *msg);

int main() {
    int mcdonalds_socket;
    int dominos_socket;
    int welcome_socket; // Socket for clients to connect
    struct sockaddr_in address; // Address structure for server
    int addrlen = sizeof(address);  // Length of address structure

    memset(clients, 0, sizeof(clients));    // Initialize clients array to 0
    memset(restaurants, 0, sizeof(restaurants)); // Initialize restaurants array to 0

    if ((welcome_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {  // Create socket for clients to connect
        perror("socket failed");    // Print error message if socket creation fails
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;   // Set address family to IPv4
    address.sin_addr.s_addr = INADDR_ANY;   // Set address to accept connections from any IP
    address.sin_port = htons(CLIENT_PORT);  // Set port for clients to connect

    if (bind(welcome_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {   // Bind socket to address
        perror("bind failed");  // Print error message if bind fails
        close(welcome_socket);  // Close welcome socket
        exit(EXIT_FAILURE);
    }

    if (listen(welcome_socket, 3) < 0) {    // Listen for incoming connections
        perror("listen failed");    // Print error message if listen fails
        close(welcome_socket);  // Close welcome socket
        exit(EXIT_FAILURE);
    }

    printf("Server listening for clients on port %d\n", CLIENT_PORT);   // For debug

    pthread_t manager_thread, menu_thread, active_thread;   // Threads for token manager, menu updater, and active restaurants manager
    pthread_create(&manager_thread, NULL, token_manager, NULL);   // Create token manager thread
    pthread_detach(manager_thread); // Detach token manager thread to run in the background

    pthread_create(&menu_thread, NULL, menu_update_manager, NULL);   // Create menu update manager thread
    pthread_detach(menu_thread); // Detach menu update manager thread to run in the background

    pthread_create(&active_thread, NULL, active_restaurants_manager, NULL);   // Create active restaurants manager thread
    pthread_detach(active_thread); // Detach active restaurants manager thread to run in the background

    // Start threads to handle TCP communication with restaurants
    pthread_t tcp_thread_mcdonalds, tcp_thread_dominos;
    pthread_create(&tcp_thread_mcdonalds, NULL, restaurant_tcp_handler_mcdonalds, &mcdonalds_socket);
    pthread_create(&tcp_thread_dominos, NULL, restaurant_tcp_handler_dominos, NULL);
    pthread_detach(tcp_thread_mcdonalds);
    pthread_detach(tcp_thread_dominos);

    while (1) { // Loop to accept incoming connections
        int client_socket;  // Socket for client connection
        if ((client_socket = accept(welcome_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {  // Accept incoming connection from client
            perror("accept failed");
            continue;
        }

        pthread_mutex_lock(&clients_mutex); // Lock clients array to prevent from multiple threads accessing it simultaneously
        int i;  // Loop variable
        for (i = 0; i < MAX_CLIENTS; i++) { // Loop through clients array to find empty slot
            if (clients[i].client_socket == 0) {    // Check if client slot is empty
                clients[i].client_socket = client_socket;   // Assign client socket to client slot
                strcpy(clients[i].token, generate_token()); // Generate token for client
                clients[i].last_keep_alive = time(NULL);    // Set last keep-alive time to current time
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);   // Unlock clients array

        if (i == MAX_CLIENTS) { // Check if maximum client limit is reached
            printf("Maximum client limit reached. Rejecting new connection.\n");    // Print message if maximum client limit is reached
            close(client_socket);   // Close client socket if maximum client limit is reached
        } else {    // If client slot is available
            pthread_create(&clients[i].thread_id, NULL, handle_client, (void *)&clients[i]);    // Create thread to handle client
            pthread_detach(clients[i].thread_id);   // Detach client handling thread to run in the background
        }
    }

    close(welcome_socket);  // Close welcome socket
    return 0;
}

void *handle_client(void *arg) {
    client_info_t *client = (client_info_t *)arg;   // Cast argument to client_info_t pointer
    message_t msg;
    memset(&msg, 0, sizeof(message_t));  // Ensure message is zeroed out

    printf("Client connected with token: %s\n", client->token);   // Print message when client connects

    while (1) {
        // Receive message from client
        receive_message(client->client_socket, &msg);
        printf("Server received message type: %d\n", msg.type);

        switch (msg.type) {
            case MSG_KEEP_ALIVE:
                printf("received keep alive drom client\n");
                fflush(stdout);
                break;
            case MSG_REQUEST_MENU:
                // Send restaurant options to client
                printf("Server got a restaurant options request, now showing the client.\n");
                send_restaurant_options(client);
                break;
            case MSG_ORDER:
                // Handle client's restaurant choice
                printf("Server got client choice\n");
                int choice = atoi(msg.data);
                const char *restaurant = NULL;

                switch (choice) {
                    case 1:
                        restaurant = "McDonalds";
                        printf("Server chose McDonalds\n");
                        break;
                    case 2:
                        restaurant = "Dominos";
                        break;
                    case 3:
                        restaurant = "Taco Bell";
                        break;
                    default:
                        printf("Invalid restaurant choice\n");
                        close(client->client_socket);
                        pthread_exit(NULL);
                }

                pthread_mutex_lock(&restaurants_mutex);
                int found = 0;
                for (int i = 0; i < MAX_RESTAURANTS; i++) {
                    if (strcmp(restaurants[i].name, restaurant) == 0 && restaurants[i].active) {
                        found = 1;
                        strncpy(msg.data, restaurants[i].menu, BUFFER_SIZE);
                        msg.type = MSG_MENU;
                        send_message(client->client_socket, &msg);
                        break;
                    }
                }
                pthread_mutex_unlock(&restaurants_mutex);

                if (!found) {
                    printf("Restaurant %s is not available\n", restaurant);
                    message_t A_response;
                    memset(&A_response, 0, sizeof(message_t)); // Ensure message is zeroed out
                    A_response.type = REST_UNAVALIABLE;
                    snprintf(A_response.data, BUFFER_SIZE, "not available");
                    printf("Sending message type %d\n", A_response.type);
                    send_message(client->client_socket, &A_response);
                } else {
                    // Wait for the client to order a meal
                    receive_message(client->client_socket, &msg);
                    if (msg.type == MSG_ORDER) {
                        // Forward the order to the restaurant
                        send_order_to_restaurant(client, msg.data, restaurant);
                    } else {
                        printf("in client: expected to get order, instead got %d\n", msg.type);
                        close(client->client_socket);
                        pthread_exit(NULL);
                    }
                }
                break;

            default:
                printf("In client: Unexpected message type: %d\n", msg.type);
                close(client->client_socket);
                pthread_exit(NULL);
        }
    }

    close(client->client_socket);   // Close client socket after client disconnects

    pthread_mutex_lock(&clients_mutex); // Lock clients array to prevent from multiple threads accessing it simultaneously
    memset(client, 0, sizeof(client_info_t));   // Clear client information after client disconnects
    pthread_mutex_unlock(&clients_mutex);   // Unlock clients array

    return NULL;    // Return NULL to exit thread
}

// Function to manage client tokens
void *token_manager(void *arg) {
    while (1) { // Loop to check for expired tokens
        sleep(1);
        time_t current_time = time(NULL);   // Get current time

        pthread_mutex_lock(&clients_mutex); // Lock clients array to prevent from multiple threads accessing it simultaneously
        for (int i = 0; i < MAX_CLIENTS; i++) {  // Loop through clients array
            if (clients[i].client_socket != 0 && difftime(current_time, clients[i].last_keep_alive) > TOKEN_TIMEOUT) {  // Check if token is expired
                printf("Token expired for client: %s\n", clients[i].token);   // Print message for expired token
                close(clients[i].client_socket);    // Close client socket
                memset(&clients[i], 0, sizeof(client_info_t));  // Clear client information after token expires
            }
        }
        pthread_mutex_unlock(&clients_mutex);   // Unlock clients array
    }
    return NULL;
}

// Function to manage restaurant active status
void *active_restaurants_manager(void *arg) {
    while (1) { // Loop to check for expired keep-alive signals
        sleep(5);
        time_t current_time = time(NULL);   // Get current time

        pthread_mutex_lock(&restaurants_mutex); // Lock restaurants array to prevent from multiple threads accessing it simultaneously
        for (int i = 0; i < MAX_RESTAURANTS; i++) {  // Loop through restaurants array
            if (restaurants[i].restaurant_socket != 0 && difftime(current_time, restaurants[i].last_keep_alive) > RESTAURANT_TIMEOUT && restaurants[i].active == 1 ) {  // Check if keep-alive is expired
                printf("Keep-alive expired for restaurant: %s\n", restaurants[i].name);   // Print message for expired keep-alive
                restaurants[i].active = 0;    // Set restaurant as inactive
            }
        }
        pthread_mutex_unlock(&restaurants_mutex);   // Unlock restaurants array
    }
    return NULL;
}

// Function to generate a random token
char *generate_token() {
    static char token[BUFFER_SIZE]; // Static buffer to store token
    sprintf(token, "USER_%ld", time(NULL) + rand() % 10000);    // Generate token based on current time and random number
    return token;   // Return generated token
}

// Function to send restaurant options to client
void send_restaurant_options(client_info_t *client) {
    message_t msg;
    memset(&msg, 0, sizeof(message_t));  // Ensure message is zeroed out
    msg.type = MSG_RESTAURANT_OPTIONS;
    strcpy(msg.data, "Choose a restaurant:\n1. McDonalds\n2. Dominos\n3. Taco Bell\n");
    send_message(client->client_socket, &msg);
}

// Function to send menu to client from database
void send_menu_to_client(client_info_t *client, const char *restaurant) {
    message_t msg;
    memset(&msg, 0, sizeof(message_t));  // Ensure message is zeroed out
    msg.type = MSG_MENU;

    pthread_mutex_lock(&restaurants_mutex); // Lock restaurants array to prevent from multiple threads accessing it simultaneously
    for (int i = 0; i < MAX_RESTAURANTS; i++) {
        if (strcmp(restaurants[i].name, restaurant) == 0) {
            strncpy(msg.data, restaurants[i].menu, BUFFER_SIZE);
            break;
        }
    }
    pthread_mutex_unlock(&restaurants_mutex);   // Unlock restaurants array

    send_message(client->client_socket, &msg);
}

// Function to forward order to restaurant
void send_order_to_restaurant(client_info_t *client, const char *order, const char *restaurant) {
    int restaurant_socket = -1;
    restaurant_info_t *restaurant_info = NULL;

    // Find the restaurant socket based on the name
    pthread_mutex_lock(&restaurants_mutex);
    for (int i = 0; i < MAX_RESTAURANTS; i++) {
        if (strcmp(restaurants[i].name, restaurant) == 0) {
            restaurant_socket = restaurants[i].restaurant_socket;
            restaurant_info = &restaurants[i];
            break;
        }
    }
    pthread_mutex_unlock(&restaurants_mutex);

    if (restaurant_socket < 0) {
        message_t msg;
        memset(&msg, 0, sizeof(message_t));  // Ensure message is zeroed out
        msg.type = MSG_ESTIMATED_TIME;
        snprintf(msg.data, BUFFER_SIZE, "Restaurant %s is not available.\n", restaurant);
        send_message(client->client_socket, &msg);
        return;
    }

    // Send the order to the restaurant
    message_t msg;
    memset(&msg, 0, sizeof(message_t));  // Ensure message is zeroed out
    msg.type = MSG_ORDER;
    strncpy(msg.data, order, BUFFER_SIZE);
    send_message(restaurant_socket, &msg);

    // Receive estimated time from restaurant
    receive_message(restaurant_socket, &msg);

    if (msg.type == MSG_ESTIMATED_TIME) {
        send_message(client->client_socket, &msg);
    } else {
        snprintf(msg.data, BUFFER_SIZE, "Failed to get the estimated time from %s.\n", restaurant);
        send_message(client->client_socket, &msg);
    }
}

// Function to handle TCP communication with McDonald's
void *restaurant_tcp_handler_mcdonalds(void *arg) {
    int *tcp_socket = (int *)arg;
    struct sockaddr_in tcp_addr;

    if ((*tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("TCP socket creation failed");
        pthread_exit(NULL);
    }

    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(MCDONALDS_PORT);

    if (bind(*tcp_socket, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("TCP bind failed");
        close(*tcp_socket);
        pthread_exit(NULL);
    }

    if (listen(*tcp_socket, 3) < 0) {
        perror("TCP listen failed");
        close(*tcp_socket);
        pthread_exit(NULL);
    }

    printf("Server listening for McDonald's TCP connections on port %d\n", MCDONALDS_PORT);

    while (1) {
        int restaurant_socket;
        struct sockaddr_in restaurant_addr;
        socklen_t addrlen = sizeof(restaurant_addr);

        if ((restaurant_socket = accept(*tcp_socket, (struct sockaddr *)&restaurant_addr, &addrlen)) < 0) {
            perror("TCP accept failed");
            continue;
        }
        printf("McDonald's connected to TCP\n");

        message_t msg;
        memset(&msg, 0, sizeof(message_t));  // Ensure message is zeroed out
        while (1) {
            receive_message(restaurant_socket, &msg);
            printf("Received message type: %d from McDonald's\n", msg.type);

            switch (msg.type) {
                case MSG_MENU:
                    pthread_mutex_lock(&restaurants_mutex);
                    for (int i = 0; i < MAX_RESTAURANTS; i++) {
                        if (restaurants[i].restaurant_socket == 0) {
                            restaurants[i].restaurant_socket = restaurant_socket;
                            strncpy(restaurants[i].name, "McDonalds", BUFFER_SIZE);
                            strncpy(restaurants[i].menu, msg.data, BUFFER_SIZE);
                            restaurants[i].address = restaurant_addr;
                            restaurants[i].last_keep_alive = time(NULL);
                            restaurants[i].active = 1; // Set restaurant as active
                            break;
                        }
                        if (restaurants[i].restaurant_socket == restaurant_socket) {
                            strncpy(restaurants[i].menu, msg.data, BUFFER_SIZE);
                            restaurants[i].last_keep_alive = time(NULL);
                            restaurants[i].active = 1; // Set restaurant as active
                        }
                    }
                    pthread_mutex_unlock(&restaurants_mutex);
                    break;
                case MSG_KEEP_ALIVE:
                    pthread_mutex_lock(&restaurants_mutex);
                    for (int i = 0; i < MAX_RESTAURANTS; i++) {
                        if (restaurants[i].restaurant_socket == restaurant_socket) {
                            restaurants[i].last_keep_alive = time(NULL);
                            printf("Keep-alive received from %s\n", restaurants[i].name);
                            break;
                        }
                    }
                    pthread_mutex_unlock(&restaurants_mutex);
                    break;
                default:
                    printf("In mcdonalds: Unexpected message type: %d\n", msg.type);
                    close(restaurant_socket);
                    break;
            }
        }
    }

    close(*tcp_socket);
    pthread_exit(NULL);
}

// Function to handle TCP communication with Domino's
void *restaurant_tcp_handler_dominos(void *arg) {
    int tcp_socket;
    struct sockaddr_in tcp_addr;

    if ((tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("TCP socket creation failed");
        pthread_exit(NULL);
    }

    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(DOMINOS_PORT);

    if (bind(tcp_socket, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("TCP bind failed");
        close(tcp_socket);
        pthread_exit(NULL);
    }

    if (listen(tcp_socket, 3) < 0) {
        perror("TCP listen failed");
        close(tcp_socket);
        pthread_exit(NULL);
    }

    printf("Server listening for Domino's TCP connections on port %d\n", DOMINOS_PORT);

    while (1) {
        int restaurant_socket;
        struct sockaddr_in restaurant_addr;
        socklen_t addrlen = sizeof(restaurant_addr);

        if ((restaurant_socket = accept(tcp_socket, (struct sockaddr *)&restaurant_addr, &addrlen)) < 0) {
            perror("TCP accept failed");
            continue;
        }

        message_t msg;
        memset(&msg, 0, sizeof(message_t));  // Ensure message is zeroed out
        while (1) {
            receive_message(restaurant_socket, &msg);
            printf("Received message type: %d from Domino's\n", msg.type);

            switch (msg.type) {
                case MSG_MENU:
                    pthread_mutex_lock(&restaurants_mutex);
                    for (int i = 0; i < MAX_RESTAURANTS; i++) {
                        if (restaurants[i].restaurant_socket == 0) {
                            restaurants[i].restaurant_socket = restaurant_socket;
                            strncpy(restaurants[i].name, "Dominos", BUFFER_SIZE);
                            strncpy(restaurants[i].menu, msg.data, BUFFER_SIZE);
                            restaurants[i].address = restaurant_addr;
                            restaurants[i].last_keep_alive = time(NULL);
                            restaurants[i].active = 1; // Set restaurant as active
                            break;
                        }
                        if (restaurants[i].restaurant_socket == restaurant_socket) {
                            strncpy(restaurants[i].menu, msg.data, BUFFER_SIZE);
                            restaurants[i].last_keep_alive = time(NULL);
                            restaurants[i].active = 1; // Set restaurant as active
                        }
                    }
                    pthread_mutex_unlock(&restaurants_mutex);
                    break;
                case MSG_KEEP_ALIVE:
                    pthread_mutex_lock(&restaurants_mutex);
                    for (int i = 0; i < MAX_RESTAURANTS; i++) {
                        if (restaurants[i].restaurant_socket == restaurant_socket) {
                            restaurants[i].last_keep_alive = time(NULL);
                            printf("Keep-alive received from %s\n", restaurants[i].name);
                            break;
                        }
                    }
                    pthread_mutex_unlock(&restaurants_mutex);
                    break;
                default:
                    printf("Unexpected message type: %d\n", msg.type);
                    close(restaurant_socket);
                    break;
            }
        }
    }

    close(tcp_socket);
    pthread_exit(NULL);
}

// Function to periodically update menus from restaurants
void *menu_update_manager(void *arg) {
    int multicast_socket;
    struct sockaddr_in multicast_addr;
    message_t msg;
    memset(&msg, 0, sizeof(message_t));  // Ensure message is zeroed out

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

    printf("Server listening on multicast group %s:%d\n", MULTICAST_GROUP, MULTICAST_PORT); // Print the multicast group information

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
    ssize_t received_bytes = recv(sock, &network_msg, sizeof(network_msg), 0);
    if (received_bytes <= 0) {
        perror("Failed to receive the message");
        exit(EXIT_FAILURE);
    }

    msg->type = ntohl(network_msg.type); // Convert message type back to host byte order
    strncpy(msg->data, network_msg.data, BUFFER_SIZE); // Copy data without modification

    printf("Received message type %d (network byte order: %d)\n", msg->type, network_msg.type);
}
