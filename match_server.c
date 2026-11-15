#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define MAX_MOVES 225

typedef struct {
    int row;
    int col;
} Move;

// Hai vị trí dành cho hai người chơi. -1 nghĩa là chưa có kết nối.
int players[2] = {-1, -1};
Move moves[MAX_MOVES];
int move_count = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void send_message(int client_socket, const char *message) {
    size_t length = strlen(message);
    ssize_t sent = send(client_socket, message, length, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent != (ssize_t)length) shutdown(client_socket, SHUT_RDWR);
}

int read_command(int client_socket, char *command, size_t size) {
    size_t i = 0;
    while (i + 1 < size) {
        char c;
        ssize_t received = recv(client_socket, &c, 1, 0);
        if (received <= 0 || c == '\0') return 0;
        command[i++] = c;
        if (c == '\n') {
            command[i] = '\0';
            return 1;
        }
    }
    return 0;  
}

// Lưu tọa độ đã gửi của cả hai người. Hàm này chạy khi đang giữ khóa.
void forward_move(int client_socket, int opponent, const char *command) {
    int row, col;
    char separator, extra;
    // Bỏ tiền tố MOVE, đọc hai số có dấu cách hoặc tab ở giữa.
    if (sscanf(command + 5, "%6d%c%6d %c", &row, &separator, &col, &extra) != 3 ||
        (separator != ' ' && separator != '\t')) {
        send_message(client_socket, "ERR use MOVE row col\n");
        return;
    }

    for (int i = 0; i < move_count; i++) {
        if (moves[i].row == row && moves[i].col == col) {
            send_message(client_socket, "ERR position already used\n");
            return;
        }
    }

    if (move_count == MAX_MOVES) {
        send_message(client_socket, "ERR move list is full\n");
        return;
    }

    moves[move_count].row = row;
    moves[move_count].col = col;
    move_count++;
    send_message(opponent, command);
}

void *handle_client(void *arg) {
    int seat = arg == &players[0] ? 0 : 1;
    int client_socket = players[seat];
    char command[64];

    while (read_command(client_socket, command, sizeof(command))) {
        pthread_mutex_lock(&lock);
        int opponent = players[1 - seat];
        if (strncmp(command, "MOVE ", 5) != 0) {
            send_message(client_socket, "ERR use MOVE row col\n");
        } else if (opponent == -1) {
            send_message(client_socket, "WAIT for opponent\n");
        } else {
            forward_move(client_socket, opponent, command);
        }
        pthread_mutex_unlock(&lock);
    }

    pthread_mutex_lock(&lock);
    players[seat] = -1;
    move_count = 0;
    int opponent = players[1 - seat];
    if (opponent != -1) send_message(opponent, "WAIT opponent disconnected\n");
    pthread_mutex_unlock(&lock);

    close(client_socket);
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
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) == -1 ||
        bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1 ||
        listen(server_socket, 5) == -1) {
        perror("server setup");
        close(server_socket);
        return 1;
    }

    printf("Caro server listening on port %d.\n", PORT);
    fflush(stdout);
    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == -1) {
            perror("accept");
            break;
        }

        pthread_mutex_lock(&lock);
        int seat = players[0] == -1 ? 0 : 1;
        if (players[seat] != -1) {
            send_message(client_socket, "ERR game is full\n");
            close(client_socket);
        } else {
            players[seat] = client_socket;
            pthread_t thread;
            int result = pthread_create(&thread, NULL, handle_client, &players[seat]);
            if (result != 0) {
                fprintf(stderr, "pthread_create: %s\n", strerror(result));
                players[seat] = -1;
                close(client_socket);
            } else {
                pthread_detach(thread);
                if (players[1 - seat] == -1) {
                    send_message(client_socket, "WAIT for opponent\n");
                } else {
                    send_message(players[0], "READY\n");
                    send_message(players[1], "READY\n");
                }
            }
        }
        pthread_mutex_unlock(&lock);
    }

    close(server_socket);
    return 1;
}
