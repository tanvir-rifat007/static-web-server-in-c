
#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <errno.h>

#ifdef __linux__
#include <sys/sendfile.h>
#endif

#define PORT 8080
#define MAX_REQUEST_BYTES 32768

int respond_error(int socket_fd, int fd, const char *message) {
    if (fd != -1) {
        close(fd);
    }

    char response[256]; // These error responses are small, e.g. "500 Internal Server Error"
    snprintf(response, sizeof(response), "HTTP/1.1 %s\r\n\r\n", message);

    write(socket_fd, response, strlen(response));

    return -1;
}

int respond_500(int socket_fd, int fd) {
    return respond_error(socket_fd, fd, "500 Internal Server Error");
}



const char *DEFAULT_FILE = "index.html";


char *to_path(char *req) {
    char *start;

    for(start = req; start[0] != ' '; start++) {
        if (start[0] == '\0') {
            return NULL;
        }
    }

     

    start++;

    char *end = start;
    // Find the end of the path, which is either a space or the end of the string
    char *last_slash = NULL;
    // Keep track of the last slash and dot we see in the path
    char *last_dot = NULL;

    while(end[0] != ' ') {
        switch (end[0]) {
            case '/':
                last_slash = end;
                break;
            case '.':
                last_dot = end;
                break;
            case '\0':
                return NULL;
        }

        end++;
    }

    // OPTIONS requests (and requests via proxies) may not start with '/'
    // For now, we don't support these requests. (We could always add support, though!)
    if (last_slash == NULL) {
        return NULL;
    }

    // If there's no file extension, default to index.html as the filename.
    if (last_dot == NULL || last_slash > last_dot) {
        end[0] = '/';

        if (end > last_slash + 1) {
            end++;
        }

        // If there isn't enough room to copy in "index.html" then return NULL.
        // (This only happens if the request has no headers, which should only
        // come up in practice if the request is malformed or something.)
        if (end + strlen(DEFAULT_FILE) > req + strlen(req)) {
            return NULL;
        }

        memcpy(end, DEFAULT_FILE, strlen(DEFAULT_FILE) + 1);
    } else {
        end[0] = '\0';
    }

    // Skip the leading '/'
    return start + 1;
}

// Macro to define a 4-char constant integer
#define FOURCHAR(a, b, c, d) a | (b << 8) | (c << 16) | (d << 24)

// Define constants for HTML and JPEG tags
#define HTML FOURCHAR('h', 't', 'm', 'l')
#define WASM FOURCHAR('w', 'a', 's', 'm')
#define WEBP FOURCHAR('w', 'e', 'b', 'p')
#define JPEG FOURCHAR('j', 'p', 'e', 'g')
#define JPG FOURCHAR('j', 'p', 'g', 0)
#define CSS FOURCHAR('c', 's', 's', 0)
#define PNG FOURCHAR('p', 'n', 'g', 0)
#define GIF FOURCHAR('g', 'i', 'f', 0)
#define JS FOURCHAR('j', 's', 0, 0)

size_t write_response_header(char ext[4], char *resp_str) {
    char *content_type;

    switch (*(int *)ext) {
        case HTML:
            content_type = "text/html";
            break;
        case WASM:
            content_type = "application/wasm";
            break;
        case CSS:
            content_type = "text/css";
            break;
        case JS:
            content_type = "application/javascript";
            break;
        case PNG:
            content_type = "image/png";
            break;
        case JPG:
            content_type = "image/jpeg";
            break;
        case JPEG:
            content_type = "image/jpeg";
            break;
        case WEBP:
            content_type = "image/webp";
            break;
        case GIF:
            content_type = "image/gif";
            break;
        default:
            content_type = "application/octet-stream";
            break;
    }

    return sprintf(resp_str, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", content_type);
}

int handle_req(char *request, int socket_fd) {
    char *path = to_path(request);

    if (path == NULL) {
        return respond_error(socket_fd, -1, "400 Bad Request");
    }

    int fd = open(path, O_RDONLY);

    if (fd == -1) {
        if (errno == ENOENT) {
            return respond_error(socket_fd, fd, "404 Not Found");
        } else {
            return respond_500(socket_fd, fd);
        }
    }

    struct stat stats;

    // Populate the `stats` struct with the file's metadata
    // If it fails (even though the file was open), respond with a 500 error.
    if (fstat(fd, &stats) == -1) {
        return respond_500(socket_fd, fd);
    }

    // Write the header to the socket ("HTTP/1.1 200 OK" followed by a Content-Type header)
    {
        char ext[4] = {0};

        {
            char *path_ext = strrchr(path, '.'); // Could skip the strrchr() if done in to_path
            size_t ext_len = strlen(path_ext);

            // If the extension is less than 4 bytes (5, counting the '.'), set it to NULL.
            if (path_ext != NULL && ext_len <= 5) {
                strncpy(ext, path_ext + 1, ext_len - 1);
            }
        }

        char resp_str[1024]; // Our responses are short, e.g. "200 OK\n\nContent-Type: text/css"
        size_t resp_length = write_response_header(ext, resp_str);
        ssize_t bytes_to_write = resp_length;
        ssize_t bytes_written = 0;

        while (bytes_to_write) {
            bytes_written = write(socket_fd, resp_str + bytes_written, bytes_to_write);

            if (bytes_written == -1) {
                // If sending the 200 didn't succeed, the odds of 500 succeeding aren't great!
                return respond_500(socket_fd, fd);
            }

            bytes_to_write -= bytes_written;
        }
    }

    {
        // Send the file's contents to the socket as the response body
        ssize_t bytes_to_send = stats.st_size;

        while (bytes_to_send > 0) {
            #ifdef __linux__
                ssize_t bytes_sent = sendfile(socket_fd, fd, NULL, stats.st_size);
                bool send_failed = bytes_sent == -1;
            #elif defined(__APPLE__)
                off_t bytes_sent = stats.st_size;
                bool send_failed = sendfile(fd, socket_fd, 0, &bytes_sent, NULL, 0) == -1;
            #else
                #error "Unsupported operating system"
            #endif

            if (send_failed) {
                // We already sent a 200 OK response, so it's too late to send a 500.
                break;
            }

            bytes_to_send -= bytes_sent;
        }
    }

    close(fd);


    return 0;
}


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

    char req[MAX_REQUEST_BYTES+1];

    // 5. Accept loop
    while (1) {
        // Block until we get a connection on the socket
        int req_socket_fd = accept(socket_fd, (struct sockaddr *)&client_address, (socklen_t*)&client_address_length);

        if (req_socket_fd >= 0) {
            // Read all the bytes from the socket into the buffer
            ssize_t bytes_read = read(req_socket_fd, req, MAX_REQUEST_BYTES);

            if (bytes_read < MAX_REQUEST_BYTES) {
                req[bytes_read] = '\0'; // Null-terminate
                
                printf("Received request:%s\n", req);


                // Parse the URL and method out of the HTTP request
                handle_req(req, req_socket_fd);
            } else {
                // The request was larger than the maximum size we support!

                // 👉 Change this to send an the actual response to the socket.
                printf("HTTP/1.1 413 Content Too Large\n\n");
            }

            close(req_socket_fd);
        } else {
            // Continue listening for other connections even if accept fails
        }
    }
    close(socket_fd);
    return 0;
}

