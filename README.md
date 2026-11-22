# Game Cờ Caro qua mạng (IT4062)

Xây dựng ứng dụng chơi cờ caro trực tuyến

## Cách chạy

```bash
# Biên dịch
gcc -Wall server.c -o server -pthread
gcc -Wall client.c -o client

# Terminal 1
./server

# Terminal 2 
./client
```

Server lắng nghe ở cổng **8080**. Client kết nối tới `127.0.0.1:8080`.

## Tiến độ

| # | Yêu cầu | Điểm | Trạng thái |
|---|---|---|---|
| 1 | Xử lý truyền dòng | 1 | Xong |
| 2 | Cơ chế vào/ra socket trên server | 2 | Xong |
| 3 | Đăng ký và quản lý tài khoản | 2 | Xong |
| 4 | Đăng nhập và quản lý phiên | 2 | Xong |
| 5 | Danh sách người chơi sẵn sàng | 2 | Chưa làm |
| 6 | Thách đấu (chênh hạng ±10) | 2 | Chưa làm |
| 7 | Nhận / từ chối thách đấu | 1 | Chưa làm |
| 8 | Chuyển thông tin nước đi | 2 | Chưa làm |
| 9 | Kiểm tra nước đi hợp lệ | 1 | Chưa làm |
| 10 | Xác định kết quả thắng/thua/hoà | 1 | Chưa làm |
| 11 | Ghi log ván đấu | 1 | Chưa làm |
| 12 | Gửi kết quả và log cho 2 người chơi | 2 | Chưa làm |
| 13 | Xin ngừng ván (người xin thua) | 1 | Chưa làm |
| | **Đã xong** | **7 / 20** | |

Tính điểm xếp hạng: thắng **+3**, hoà **0**, thua **−3**.

