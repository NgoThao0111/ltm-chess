#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <ctype.h> 

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER 1024

int clients[MAX_CLIENTS];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int client_socket;
    int array_index;
} ClientSession;

#define USER_FILE "users.txt"   // file lưu tài khoản
#define NAME_LEN 32             // độ dài tối đa của username/password

typedef struct {
    char username[NAME_LEN];
    char password[NAME_LEN];
    int score;
} Account;

#define SEND_OK 0
#define SEND_ERROR -1
int send_message(int client_socket, const char *message) {
    int length = strlen(message);
    int total_sent = 0;
    while(total_sent < length) {
        // MSG_NOSIGNAL: nếu client đã ngắt kết nối, hàm send sẽ không gửi tín hiệu SIGPIPE tới server
        // Bình thường khi hàm send gặp lỗi, hệ điều hành có thể phát tín hiệu SIGPIPE -> có thể khiến toàn bộ tiến trình server bị kết thúc, không chỉ client
        int sent = send(client_socket, message + total_sent, length - total_sent, MSG_NOSIGNAL);
        if(sent > 0) total_sent += sent;
        else if (sent < 0 && errno == EINTR) continue; // nếu quá trình gửi bị gián đoạn bởi tín hiệu thì error = EINTR, khi đó tiếp tục gửi
        else return -1; // nếu gửi không thành công, trả về -1
    }
    return 0; // gửi thành công
}

/*
Ý nghĩa giá trị trả về
1: đọc thành công 1 lệnh
0: client ngắt kết nối
-1: hàm recv gặp lỗi
-2: lệnh quá dài vượt quá buffer
-3: lệnh chứa kí tự \0 ko hợp lệ
-4: lệnh rỗng
*/
#define READ_OK 1
#define READ_DISCONNECTED 0
#define READ_ERROR -1
#define READ_TOO_LONG -2
#define READ_INVALID_COMMAND -3
int read_command(int client_socket, char *command, int size) {
    if (command == NULL || size < 1) return READ_ERROR;
    int i = 0;
    int too_long = 0;
    int invalid = 0;
    while (1) {
        char c;
        int received = recv(client_socket, &c, 1, 0);
        if (received == 0) return READ_DISCONNECTED; // client đã ngắt kết nối
        if (received < 0) {
            if (errno == EINTR) continue; 
            return READ_ERROR;
        }
        if (c == '\n') {
            if(invalid) return READ_INVALID_COMMAND;
            if(too_long) return READ_TOO_LONG;
            command[i] = '\0';
            return READ_OK;
        }

        if (invalid || too_long) continue; // nếu đã có flag vấn đề thì skip đến cuối

        if (c == '\0') {
            invalid = 1;
            continue;
        }

        if (i < size - 1) command[i++] = c;
        else too_long = 1;
    }
}

int valid_username(const char *name){
    int len = strlen(name);
    if(len ==0 || len>30) {
        return 0;
    }
    for(int i = 0; i< len; i++) {
        if(!isalnum(name[i])) {  //chữ cái hoặc chữ số
            return 0;
        }
    }
    return 1;
}

int valid_password(const char *password) {
    int len = strlen(password);
    return len >=1 && len <=30;
}

int find_account(const char *username, Account *result) {
    FILE *f = fopen(USER_FILE, "r");
    if(f == NULL) {
        return 0;   // chưa có file
    }
    Account acc;
    while (fscanf(f, "%31s %31s %d", acc.username, acc.password, &acc.score) == 3) {
        if(strcmp(acc.username, username) == 0) {
            if(result != NULL) *result = acc;
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

int add_account(const Account *acc) {
    FILE *f = fopen(USER_FILE, "a");  // ghi vào cuối file
    if(f == NULL) {
        return -1;
    }
    fprintf(f, "%s %s %d\n", acc->username, acc->password, acc->score);
    fclose(f);
    return 0;
}

void handle_register(int client_socket, const char *command) {
    Account acc;
    if (sscanf(command, "REGISTER %31s %31s", acc.username, acc.password) != 2) {
        send_message(client_socket, "203 SYNTAX ERROR\n");
        return;
    }
    if (!valid_username(acc.username)) {
        send_message(client_socket, "201 INVALID USERNAME\n");
        return;
    }
    if (!valid_password(acc.password)) {
        send_message(client_socket, "202 INVALID PASSWORD\n");
        return;
    }
    acc.score = 0;  // mặc định 0 điểm

    const char *reply;
    pthread_mutex_lock(&lock);
    if (find_account(acc.username, NULL)) {
        reply = "200 USERNAME IN USE\n";
    } else if (add_account(&acc) == -1) {
        reply = "999 UNKNOWN ERROR\n";
    } else {
        reply = "100 OK\n";
        printf("New account: %s\n", acc.username);
        fflush(stdout);
    }
    pthread_mutex_unlock(&lock);

    send_message(client_socket, reply);  // gửi sau khi mở khóa
}

void handle_command(int client_socket, const char *command) {
    if (strncmp(command, "REGISTER ", 9) == 0) {
        handle_register(client_socket, command);
    } else {
        send_message(client_socket, "204 UNKNOWN COMMAND\n");
    }
}

void *handle_client(void *arg);

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

    if(bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
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
    // fflush(stdout); // nội dung printf có thể vẫn được giữ tại stdout, dùng fflush để đẩy nội dung ra terminal

    for(int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = -1;
    }

    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == -1) {
            if (errno == EINTR) {
                // EINIR là lỗi 1 hàm blocking bị gián đoạn bởi signal, nên không cần kết thúc luôn chương trình
                continue;
            }
            if (errno == ECONNABORTED) {
                // ECONNABORTED là lỗi khi kết nối mạng đã thiết lập bị ngắt đột ngột do lỗi phần mềm ở phía máy local hoặc do tầng giao thức mạng nội bộ, chứ không phải do lỗi máy chủ
                // Có thể xảy ra khi client hủy kết nối trước khi accept() hoàn thành.
                continue;
            }
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
        // Nếu index = -1 nghĩa là mảng clients đã đầy, không còn chỗ trống cho client mới
        if (index == -1) {
            pthread_mutex_unlock(&lock);
            char *message = "ERR server is full\n";
            // MSG_DONTWAIT: hàm send sẽ không chặn server, server vẫn tiếp tục chạy bình thường
            send(client_socket, message, strlen(message), MSG_DONTWAIT | MSG_NOSIGNAL);
            close(client_socket);
            continue;
        }
        ClientSession *session = malloc(sizeof(ClientSession));
        if(session == NULL) {
            // malloc failed
            clients[index] = -1;
            pthread_mutex_unlock(&lock);
            // stderr là luồng xuất lỗi, dùng để in ra các thông báo lỗi, vẫn hiển thị trên terminal nhưng có thể lưu riêng các log gồm các thông báo lỗi ra 1 file riêng
            fprintf(stderr, "malloc failed: %s\n", strerror(errno));
            char *message = "ERR server cannot allocate memory for client\n";
            send(client_socket, message, strlen(message), MSG_DONTWAIT | MSG_NOSIGNAL);
            close(client_socket);
            continue;
        }
        session->client_socket = client_socket;
        session->array_index = index;
        pthread_t thread;
        int result = pthread_create(&thread, NULL, handle_client, session);
        if (result != 0) {
            // can't create thread
            clients[index] = -1;
            pthread_mutex_unlock(&lock);
            free(session);
            fprintf(stderr, "pthread_create: %s\n", strerror(result));
            char *message = "ERR server cannot create thread for client\n";
            send(client_socket, message, strlen(message), MSG_DONTWAIT | MSG_NOSIGNAL);
            close(client_socket);
            continue;
        }
        pthread_mutex_unlock(&lock);
        // pthread_detach() giúp hệ thống tự động giải phóng tài nguyên của thread đó ngay khi nó kết thúc, mà không cần thread khác gọi pthread_join()
        pthread_detach(thread);
    }
    close(server_socket);
    return 1;
}

void* handle_client(void *arg) {
    ClientSession *session = (ClientSession *) arg;
    char command[BUFFER];
    while(1) {
        int result = read_command(session->client_socket, command, sizeof(command));
        if(result == READ_OK) {
            printf("Client %d sent: %s\n", session->client_socket, command);
            fflush(stdout);
            handle_command(session->client_socket, command);
        }
        else if(result == READ_TOO_LONG) {
            if(send_message(session->client_socket, "ERR command too long, max length 1023\n") == -1) break;
        }
        else if(result == READ_INVALID_COMMAND) {
            if(send_message(session->client_socket, "ERR command had invalid \\0\n") == -1) break;
        }
        else break;
    }
    pthread_mutex_lock(&lock);
    clients[session->array_index] = -1;
    pthread_mutex_unlock(&lock);
    close(session->client_socket);
    free(session);
    return NULL;
}
