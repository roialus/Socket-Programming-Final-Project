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

void handle_server_interaction(int sock);
void *keep_alive(void *arg);

int main() {
    struct sockaddr_in server_addr; // Server address
    int sock;                 // Socket descriptor

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { // Create a socket for sending and receiving data
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;   // Set the address family to IPv4
    server_addr.sin_port = htons(SERVER_PORT);  // Set the port number

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {    // Convert the IP address from text to binary form
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {  // Connect to the server at the specified address
        perror("Connection Failed");
        exit(EXIT_FAILURE);
    }

    // Create keep-alive thread
    pthread_t keep_alive_thread;
    if (pthread_create(&keep_alive_thread, NULL, keep_alive, (void *)&sock) != 0) {   // Create a thread for sending keep-alive messages
        perror("Keep-alive thread creation failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    pthread_detach(keep_alive_thread);  // Detach the keep-alive thread to allow it to run independently

    // Handle interaction with server
    handle_server_interaction(sock);

    close(sock);    // Close the socket
    return 0;
}

void handle_server_interaction(int sock) {
    char buffer[BUFFER_SIZE];   // Buffer for receiving data
    int valread;            // Number of bytes read

    // Receive restaurant options from server
    valread = read(sock, buffer, BUFFER_SIZE);  // Read data from the server
    if (valread < 0) {  // Check if read failed
        perror("read failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    buffer[valread] = '\0'; // Null-terminate the received data
    printf("Available restaurants:\n%s\n", buffer); // Print the received data

    // Choose a restaurant
    printf("Enter the number of the restaurant you want to order from: ");  // Prompt the user to enter a choice
    fflush(stdout); // Flush the output buffer
    int choice; // User choice
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > 3) {    // Read the user choice
        printf("Invalid choice.\n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    char choice_str[BUFFER_SIZE];
    sprintf(choice_str, "%d", choice);
    if (send(sock, choice_str, strlen(choice_str), 0) < 0) {   // Send the chosen restaurant to the server
        perror("send failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Receive menu from server
    valread = read(sock, buffer, BUFFER_SIZE);  // Read data from the server
    if (valread < 0) {  // Check if read failed
        perror("read failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    buffer[valread] = '\0'; // Null-terminate the received data
    printf("Menu received:\n%s\n", buffer); // Print the received menu

    // Choose a meal
    printf("Enter the number of the meal you want to order: ");  // Prompt the user to enter a choice
    fflush(stdout); // Flush the output buffer
    int meal_choice; // User choice
    if (scanf("%d", &meal_choice) != 1 || meal_choice < 1 || meal_choice > 10) {    // Read the user choice
        printf("Invalid choice.\n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    char meal_choice_str[BUFFER_SIZE];
    sprintf(meal_choice_str, "ORDER: %d", meal_choice);
    if (send(sock, meal_choice_str, strlen(meal_choice_str), 0) < 0) {   // Send the meal choice to the server
        perror("send failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Receive time estimation from server
    valread = read(sock, buffer, BUFFER_SIZE);  // Read data from the server
    if (valread < 0) {  // Check if read failed
        perror("read failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    buffer[valread] = '\0'; // Null-terminate the received data
    printf("Estimated time for your order: %s\n", buffer); // Print the time estimation

    // The main thread can handle additional tasks or communication here
    // For simplicity, we are just sleeping indefinitely in this example
    while (1) { // Loop to keep the program running
        sleep(1);
    }
}

// Keep-alive thread function
void *keep_alive(void *arg) {
    int sock = *(int *)arg; // Socket descriptor
    char *keep_alive_message = "KEEP_ALIVE";    // Keep-alive message

    while (1) { // Loop to send keep-alive messages
        sleep(30); // Send keep-alive message every 30 seconds
        if (send(sock, keep_alive_message, strlen(keep_alive_message), 0) < 0) {    // Send the keep-alive message to the server
            perror("keep_alive send failed");
            close(sock);
            pthread_exit(NULL);
        }
        printf("\nKEEP_ALIVE sent\n");    // Print the keep-alive message
    }
    return NULL;    // Return from the thread
}