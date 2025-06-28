#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
    int socket_fd;
    struct sockaddr_in server_address, client_address;
    socklen_t client_address_length = sizeof(client_address);

    // 1. Create socket
    printf("Creating socket...\n");
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. Bind socket
    server_address.sin_family = AF_INET; //ipv4
    server_address.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0
    server_address.sin_port = htons(8080);        // port 8080

    printf("Binding to port 8080...\n");
    if (bind(socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("bind");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    // 4. Listen
    printf("Listening for incoming connections...\n");
    if (listen(socket_fd, 4) == -1) {
        perror("listen");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    // 5. Accept loop
    while (1) {
        printf("Waiting for a client to connect...\n");

        int client_fd = accept(socket_fd, (struct sockaddr *)&client_address, &client_address_length);
        if (client_fd == -1) {
            perror("accept");
            printf("errno = %d\n", errno);
            continue; 
        }

        printf("Client connected from %s:%d\n",
               inet_ntoa(client_address.sin_addr),
               ntohs(client_address.sin_port));

        //send a response
        const char *msg = "Hello from C server!\n";
        send(client_fd, msg, strlen(msg), 0);

        close(client_fd);
        printf("Client disconnected.\n");
    }

    close(socket_fd);
    return 0;
}

