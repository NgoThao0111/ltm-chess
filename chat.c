#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>

typedef struct client_list {
    int client;
    struct client_list *next;
} client_list;

client_list *client_head = NULL;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

client_list *add_client(int client_socket) {
    client_list *new_client = malloc(sizeof(*new_client));
    if (new_client == NULL) return NULL;

    new_client->client = client_socket;
    pthread_mutex_lock(&lock);
    new_client->next = client_head;
    client_head = new_client;
    pthread_mutex_unlock(&lock);
    return new_client;
}

void remove_client(int client_socket) {
    pthread_mutex_lock(&lock);
    client_list *current = client_head;
    client_list *previous = NULL;
    while (current != NULL && current->client != client_socket) {
        previous = current;
        current = current->next;
    }
    if (current != NULL) {
        if (previous == NULL) client_head = current->next;
        else previous->next = current->next;
        free(current);
    }
    pthread_mutex_unlock(&lock);
}

void *handle_client(void *arg) {
    client_list *client = arg;
    int client_socket = client->client;
    char buffer[1024];
    char response[sizeof(buffer) + 32];

    printf("Client %d connected.\n", client_socket);
    while (1) {
        // Chừa một ô cho ký tự kết thúc chuỗi.
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0 && errno == EINTR) continue;
        if (bytes_received <= 0) break;
        buffer[bytes_received] = '\0';

        snprintf(response, sizeof(response), "Client %d: %s", client_socket, buffer);
        size_t length = strlen(response);

        // Giữ khóa để không có luồng khác xóa nút đang dùng.
        pthread_mutex_lock(&lock);
        for (client_list *current = client_head; current != NULL; current = current->next) {
            if (current->client == client_socket) continue;

            // Không đứng chờ một client chậm khi đang giữ khóa.
            ssize_t sent = send(current->client, response, length, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent != (ssize_t)length) shutdown(current->client, SHUT_RDWR);
        }
        pthread_mutex_unlock(&lock);
    }

    remove_client(client_socket);
    close(client_socket);
    printf("Client %d disconnected.\n", client_socket);
    return NULL;
}

int main(void) {
    int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        perror("socket");
        return 1;
    }

    int reuse_addr = 1;
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) == -1 ||
        bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1 ||
        listen(server_socket, 5) == -1) {
        perror("server setup");
        close(server_socket);
        return 1;
    }

    puts("Test server listening on port 8080.");
    fflush(stdout);
    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == -1) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        // Mỗi thread nhận nút riêng của client vừa kết nối.
        client_list *client = add_client(client_socket);
        if (client == NULL) {
            perror("malloc");
            close(client_socket);
            continue;
        }

        pthread_t thread;
        int result = pthread_create(&thread, NULL, handle_client, client);
        if (result != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(result));
            remove_client(client_socket);
            close(client_socket);
            continue;
        }
        pthread_detach(thread);
    }

    close(server_socket);
    return 1;
}
