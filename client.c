#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER 1024

int send_message(int sock, const char *message) {
    int length = strlen(message);
    int total = 0;
    while (total < length) {
        int sent = send(sock, message + total, length - total, MSG_NOSIGNAL);
        if (sent > 0) {
            total += sent;
        }
        else if (sent < 0) {
            continue;
        }
        else {
            return -1;
        }
    }
    return 0;
}

// Đọc 1 dòng server gửi về (đến '\n'), bỏ '\n'. Trả về 1 nếu đọc được, 0 nếu mất kết nối.
int read_line(int sock, char *line, int size) {
    int i = 0;
    while (i + 1 < size) {
        char c;
        int received = recv(sock, &c, 1, 0);
        if (received <= 0) {
            return 0;
        }
        if (c == '\n') {
            break;
        }
        line[i++] = c;
    }
    line[i] = '\0';
    return 1;
}

// In lời nhắc rồi đọc 1 dòng người dùng gõ vào buf (bỏ '\n')
void read_input(const char *prompt, char *buf, int size) {
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
}

void user_register(int sock) {
    char username[64], password[64], command[BUFFER], reply[BUFFER];

    read_input("Tên đăng nhập: ", username, sizeof(username));
    read_input("Mật khẩu: ", password, sizeof(password));
    if (username[0] == '\0' || password[0] == '\0' ||
        strchr(username, ' ') != NULL || strchr(password, ' ') != NULL) {
        printf("Không được để trống hoặc chứa dấu cách\n");
        return;
    }

    // Tạo lệnh theo giao thức: REGISTER <username> <password>
    snprintf(command, sizeof(command), "REGISTER %s %s\n", username, password);
    if (send_message(sock, command) == -1 || !read_line(sock, reply, sizeof(reply))) {
        printf("Mất kết nối tới server.\n");
        return;
    }

    // Đổi mã trả về thành thông báo dễ hiểu
    if (strncmp(reply, "100", 3) == 0)      printf("Đăng ký thành công!\n");
    else if (strncmp(reply, "200", 3) == 0) printf("Tên đăng nhập đã có người dùng.\n");
    else if (strncmp(reply, "201", 3) == 0) printf("Tên đăng nhập chỉ gồm chữ cái và số, tối đa 30 ký tự.\n");
    else if (strncmp(reply, "202", 3) == 0) printf("Mật khẩu tối đa 30 ký tự.\n");
    else                                    printf("Lỗi: %s\n", reply);
}

int main () {
    int client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(8080);

    if(connect(client_socket, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("connect");
        close(client_socket);
        return 1;
    }

    char choice[16];
    while (1)
    {
        printf("\n Game Caro \n");
        printf("1. Đăng ký\n");
        printf("0. Thoát\n");
        read_input("Chọn: ", choice, sizeof(choice));

        if (strcmp(choice, "1") == 0) user_register(client_socket);
        else printf("Lựa chọn không hợp lệ\n");
    }

    close(client_socket);
    return 0;
    
}