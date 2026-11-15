#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

#define PORT 8080
#define MAX_LINE 1024

typedef struct client_list {
    int client;
    struct client_list *next;
} client_list;

client_list *client_head = NULL;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

client_list *add_client(int client_socket)
{
    client_list *new_client = malloc(sizeof(*new_client));
    if (new_client == NULL)
        return NULL;

    new_client->client = client_socket;
    pthread_mutex_lock(&lock);
    new_client->next = client_head;
    client_head = new_client;
    pthread_mutex_unlock(&lock);
    return new_client;
}

void remove_client(int client_socket)
{
    pthread_mutex_lock(&lock);

    client_list *current = client_head;
    client_list *previous = NULL;
    while (current != NULL && current->client != client_socket) {
        previous = current;
        current = current->next;
    }

    if (current != NULL) {
        if (previous == NULL)
            client_head = current->next;
        else
            previous->next = current->next;
        free(current);
    }

    pthread_mutex_unlock(&lock);
}

// Gửi hết một dòng. Không chờ socket ghi được trong lúc giữ khóa.
int send_all(int client_socket, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(client_socket, data + sent, length - sent,
                         MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

void broadcast_message(int client_socket, const char *line, size_t length)
{
    char response[MAX_LINE + 64];
    int prefix = snprintf(response, sizeof(response), "Client %d: ", client_socket);
    if (prefix < 0 || (size_t)prefix + length + 1 > sizeof(response))
        return;

    memcpy(response + prefix, line, length);
    response[(size_t)prefix + length] = '\n';
    size_t response_length = (size_t)prefix + length + 1;

    // Giữ khóa khi duyệt để luồng khác không xóa nút đang dùng.
    pthread_mutex_lock(&lock);
    for (client_list *current = client_head; current != NULL; current = current->next) {
        if (current->client != client_socket &&
            send_all(current->client, response, response_length) == -1) {
            // Nếu không gửi được trọn dòng, ngắt kết nối. Luồng nhận sẽ dọn socket.
            shutdown(current->client, SHUT_RDWR);
        }
    }
    pthread_mutex_unlock(&lock);
}

void *handle_client(void *arg)
{
    client_list *client = arg;
    int client_socket = client->client;
    char buffer[1024];
    char line[MAX_LINE];
    size_t used = 0;

    printf("Client %d connected.\n", client_socket);

    while (1) {
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received < 0 && errno == EINTR)
            continue;
        if (bytes_received <= 0)
            break;

        // TCP có thể chia một dòng qua nhiều lần recv hoặc gộp nhiều dòng.
        for (ssize_t i = 0; i < bytes_received; i++) {
            if (buffer[i] == '\n') {
                size_t length = used;
                if (length > 0 && line[length - 1] == '\r')
                    length--;
                broadcast_message(client_socket, line, length);
                used = 0;
            } else if (used == sizeof(line)) {
                fprintf(stderr, "Client %d: line too long.\n", client_socket);
                goto disconnect;
            } else {
                line[used++] = buffer[i];
            }
        }
    }

disconnect:
    remove_client(client_socket);
    close(client_socket);
    printf("Client %d disconnected.\n", client_socket);
    return NULL;
}

int main(void)
{
    // Tạo socket TCP của server.
    int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int reuse_addr = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_addr, sizeof(reuse_addr)) == -1) {
        perror("setsockopt");
        close(server_socket);
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_socket);
        return EXIT_FAILURE;
    }
    if (listen(server_socket, 5) == -1) {
        perror("listen");
        close(server_socket);
        return EXIT_FAILURE;
    }

    pthread_attr_t thread_attr;
    int result = pthread_attr_init(&thread_attr);
    if (result != 0) {
        fprintf(stderr, "pthread_attr_init: %s\n", strerror(result));
        close(server_socket);
        return EXIT_FAILURE;
    }
    result = pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
    if (result != 0) {
        fprintf(stderr, "pthread_attr_setdetachstate: %s\n", strerror(result));
        pthread_attr_destroy(&thread_attr);
        close(server_socket);
        return EXIT_FAILURE;
    }

    printf("Test server listening on port %d.\n", PORT);
    fflush(stdout);

    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == -1) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }

        client_list *client = add_client(client_socket);
        if (client == NULL) {
            perror("malloc");
            close(client_socket);
            continue;
        }

        // Mỗi luồng nhận nút riêng, không nhận địa chỉ biến trong vòng lặp.
        pthread_t thread;
        result = pthread_create(&thread, &thread_attr, handle_client, client);
        if (result != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(result));
            remove_client(client_socket);
            close(client_socket);
        }
    }

    pthread_attr_destroy(&thread_attr);
    close(server_socket);
    return EXIT_FAILURE;
}
