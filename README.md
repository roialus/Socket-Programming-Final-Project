# 🍽️ Food Pick-Up Service Simulation

## 📝 Overview
This project is a server-client simulation of a food pick-up service, implemented in C using socket programming. The system allows a client to place orders from a server, which is connected to multiple restaurants (McDonald's, Taco Bell, and Domino's). The server manages communication between the client and the restaurants, ensuring that orders are handled efficiently in a multi-threaded environment.

## 🛠️ Additional Infrastructure
- **💻 Virtual Machines**: Each host and server runs on a dedicated virtual machine to simulate a real-world network environment.
- **🌐 GNS3 Network Topology**: The project also includes a GNS3 topology featuring routers and switches configured to run OSPF (Open Shortest Path First) and PIM-SM (Protocol Independent Multicast - Sparse Mode), providing a robust network infrastructure for the simulation.

## ✨ Features
- **🧵 Multi-threaded Server**: The server handles multiple clients and restaurant connections simultaneously.
- **🔌 Socket Programming**: Communication between the client, server, and restaurants is implemented using TCP sockets.
- **🔄 Modular Design**: The code is modular, with separate files for the server, client, and each restaurant.
- **📡 Network Simulation**: Integration with a GNS3 topology to simulate complex network scenarios.

## 📂 Files in the Repository
- `server.c`: Handles client connections, receives orders, and communicates with the restaurants.
- `client.c`: Sends orders to the server and receives responses.
- `mcdonalds.c`, `tacobell.c`, `dominos.c`: Restaurant modules that respond to the server with their menu and handle incoming orders.
- `GNS3_topology.gns3`: The GNS3 project file containing the network topology with routers and switches running OSPF and PIM-SM.

## 🚀 Getting Started

### 🛠️ Prerequisites
- GCC compiler
- Linux or UNIX-based system for socket programming
- GNS3 installed for network simulation
- Basic understanding of C, socket programming, and network protocols

### 🔧 Compilation
To compile the project, run the following commands:

```bash
gcc -o server server.c -lpthread
gcc -o client client.c
gcc -o mcdonalds mcdonalds.c
gcc -o tacobell tacobell.c
gcc -o dominos dominos.c
