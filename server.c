#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#define PORT 8080
#define MAX_CLIENTS 100

int clients[MAX_CLIENTS];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int send_message(int client_socket, const char *message) {
    size_t length = strlen(message);
    size_t total_sent = 0;
    while(total_sent < length) {
        // MSG_NOSIGNAL: nếu client đã ngắt kết nối, hàm send sẽ không gửi tín hiệu SIGPIPE tới server
        // Bình thường khi hàm send gặp lỗi, hệ điều hành có thể phát tín hiệu SIGPIPE -> có thể khiến toàn bộ tiến trình server bị kết thúc, không chỉ client
        ssize_t sent = send(client_socket, message + total_sent, length - total_sent, MSG_NOSIGNAL);
        if(sent > 0) total_sent += sent;
        else if (sent < 0 && errno == EINTR) continue; // nếu quá trình gửi bị gián đoạn bởi tín hiệu thì tiếp tục gửi
        else return -1; // nếu gửi không thành công, trả về -1
    }
    return 0; // gửi thành công
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

void *handle_client(void *arg) {

    //close(client_socket);
    return NULL;
}

int main() {
    int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        perror("socket");
        return 1;
    }    

    // Sau khi ngắt server, thay vì ngắt kết nối luôn thì kết nối đang ở trong trạng thái TIME_WAIT nên sẽ không bind được
    // setsockopt cho SO_REUSEADDR cho phép tái sử dụng địa chỉ ip và cổng kể cả đang ở trong trạng thái TIME_WAIT 
    int reuse_addr = 1; // 1 là cho phép tái sử dụng
    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) == -1) {
        perror("setsockopt");
        close(server_socket);
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(server_socket, (struct socketadrr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_socket);
        return 1;
    }

    if(listen(server_socket, 5) == -1) {
        perror("listen");
        close(server_socket);
        return 1;
    }

    printf("Caro server listening on port %d.\n", PORT);
    fflush(stdout); // nội dung printf có thể vẫn được giữ tại stdout, dùng fflush để đẩy nội dung ra terminal

    for(int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = -1;
    }

    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == -1) {
            perror("accept");
            break;
        }
        pthread_mutex_lock(&lock);
        // Quản lý danh sách các client kết nối tới server bằng mảng, client mới sẽ được thêm vào ô trống (-1) đầu tiên của mảng
        int index = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] == -1) {
                clients[i] = client_socket;
                index = i;
                break;
            }
        }
        pthread_mutex_unlock(&lock);
        // Nếu index = -1 nghĩa là mảng clients đã đầy, không còn chỗ trống cho client mới
        if (index == -1) {
            char *message = "ERR server is full\n";
            // MSG_DONTWAIT: nếu client đã ngắt kết nối, hàm send sẽ không chặn server, server vẫn tiếp tục chạy bình thường
            send(client_socket, message, strlen(message), MSG_DONTWAIT | MSG_NOSIGNAL);
            close(client_socket);
        } else {
            pthread_t thread;
            int result = pthread_create(&thread, NULL, handle_client, &clients[index]);
            if (result != 0) {
                fprintf(stderr, "pthread_create: %s\n", strerror(result));
                pthread_mutex_lock(&lock);
                clients[index] = -1;
                pthread_mutex_unlock(&lock);
                close(client_socket);
            } else {
                pthread_detach(thread);
            }
        }
    }
    close(server_socket);
    return 1;
}
