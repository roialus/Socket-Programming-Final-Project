# Food Pick-Up Service Simulation

## Overview
This project is a server-client simulation of a food pick-up service, implemented in C using socket programming. The system allows a client to place orders from a server, which is connected to multiple restaurants (McDonald's, Taco Bell, and Domino's). The server manages communication between the client and the restaurants, ensuring that orders are handled efficiently in a multi-threaded environment.

## Features
- **Multi-threaded Server**: The server handles multiple clients and restaurant connections simultaneously.
- **Socket Programming**: Communication between the client, server, and restaurants is implemented using TCP sockets.
- **Modular Design**: The code is modular, with separate files for the server, client, and each restaurant.

## Files in the Repository
- `server.c`: Handles client connections, receives orders, and communicates with the restaurants.
- `client.c`: Sends orders to the server and receives responses.
- `mcdonalds.c`, `tacobell.c`, `dominos.c`: Restaurant modules that respond to the server with their menu and handle incoming orders.

## Getting Started

### Prerequisites
- GCC compiler
- Linux or UNIX-based system for socket programming
- Basic understanding of C and socket programming

### Compilation
To compile the project, run the following commands:

```bash
gcc -o server server.c -lpthread
gcc -o client client.c
gcc -o mcdonalds mcdonalds.c
gcc -o tacobell tacobell.c
gcc -o dominos dominos.c
