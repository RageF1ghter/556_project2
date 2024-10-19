// sendfile.cpp
#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <getopt.h>
#include <map>

using namespace std;

#define MAX_DATA_SIZE 512  // Each frame carries up to 512 bytes
#define MAX_FRAME_SIZE 522  // Frame size: headers + data + checksum
#define ACK_SIZE 6
#define WINDOW_SIZE 5  // Sliding window size
#define TIMEOUT_MS 500  // Retransmission timeout in milliseconds

enum PacketType {
    FILENAME = 1,
    FILEDATA = 2,
    END_OF_TRANSFER = 3
};

// Simple checksum function
unsigned char checksum(const unsigned char *frame, int count) {
    unsigned long sum = 0;
    while (count--) {
        sum += *frame++;
        if (sum & 0xFF00) {
            sum &= 0xFF;
            sum++;
        }
    }
    return (unsigned char)(sum & 0xFF);
}

// Create data frame
int create_frame(PacketType pkt_type, int seq_num, const unsigned char *data, int data_size, unsigned char *frame) {
    frame[0] = static_cast<unsigned char>(pkt_type);
    uint32_t net_seq_num = htonl(seq_num);
    uint32_t net_data_size = htonl(data_size);
    memcpy(frame + 1, &net_seq_num, 4);
    memcpy(frame + 5, &net_data_size, 4);
    memcpy(frame + 9, data, data_size);
    frame[data_size + 9] = checksum(frame, data_size + 9);  // Calculate checksum
    return data_size + 10;
}

// Create ACK
void create_ack(int seq_num, unsigned char *ack, bool error) {
    ack[0] = error ? 0x0 : 0x1;
    uint32_t net_seq_num = htonl(seq_num);
    memcpy(ack + 1, &net_seq_num, 4);
    ack[5] = checksum(ack, ACK_SIZE - 1);
}

// Read ACK
bool read_ack(int *seq_num, bool *error, const unsigned char *ack) {
    *error = ack[0] == 0x0;
    uint32_t net_seq_num;
    memcpy(&net_seq_num, ack + 1, 4);
    *seq_num = ntohl(net_seq_num);
    return ack[5] != checksum(ack, ACK_SIZE - 1);
}

// Create UDP socket
int create_socket() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    return sockfd;
}

// Parse command line arguments
void parse_arguments(int argc, char *argv[], string &recv_host, int &recv_port, string &subdir, string &filename) {
    int opt;
    while ((opt = getopt(argc, argv, "r:f:")) != -1) {
        switch (opt) {
        case 'r': {
            char *host_port = strtok(optarg, ":");
            char *port_str = strtok(NULL, ":");
            if (!host_port || !port_str) {
                cerr << "Invalid receiver address format" << endl;
                exit(1);
            }
            recv_host = host_port;
            recv_port = atoi(port_str);
            break;
        }
        case 'f': {
            char *last_slash = strrchr(optarg, '/');
            if (last_slash) {
                subdir = string(optarg, last_slash - optarg);
                filename = string(last_slash + 1);
            } else {
                subdir = ".";
                filename = optarg;
            }
            break;
        }
        default:
            cerr << "Usage: sendfile -r <recv host>:<recv port> -f <subdir>/<filename>" << endl;
            exit(1);
        }
    }
}

// Set up receiver address
struct sockaddr_in setup_recv_addr(const string &recv_host, int recv_port) {
    struct sockaddr_in recv_addr;
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(recv_port);
    if (inet_pton(AF_INET, recv_host.c_str(), &recv_addr.sin_addr) <= 0) {
        perror("Invalid receiver IP address");
        exit(1);
    }
    return recv_addr;
}

// Send frame with retransmission
void send_frame_with_retransmission(int sockfd, struct sockaddr_in &recv_addr, const unsigned char *frame, int frame_size, int seq_num) {
    unsigned char ack[ACK_SIZE];
    socklen_t addr_len = sizeof(recv_addr);
    bool ack_received = false;

    while (!ack_received) {
        // Send the frame
        if (sendto(sockfd, frame, frame_size, 0, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
            perror("Failed to send frame");
            exit(1);
        }

        // Wait for ACK
        fd_set fds;
        struct timeval timeout;
        timeout.tv_sec = TIMEOUT_MS / 1000;
        timeout.tv_usec = (TIMEOUT_MS % 1000) * 1000;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);

        int result = select(sockfd + 1, &fds, NULL, NULL, &timeout);
        if (result > 0) {
            // Receive ACK
            if (recvfrom(sockfd, ack, ACK_SIZE, 0, (struct sockaddr *)&recv_addr, &addr_len) < 0) {
                perror("Failed to receive ACK");
                exit(1);
            }

            int ack_seq_num;
            bool error;
            if (!read_ack(&ack_seq_num, &error, ack) && !error && ack_seq_num == seq_num) {
                ack_received = true;
            } else {
                cout << "Received corrupt or incorrect ACK, retransmitting seq_num " << seq_num << endl;
            }
        } else {
            // Timeout, retransmit
            cout << "Timeout waiting for ACK, retransmitting seq_num " << seq_num << endl;
        }
    }
}

// Send data function with sliding window
void send_data(int sockfd, struct sockaddr_in &recv_addr, ifstream &file, int &seq_num) {
    map<int, pair<unsigned char *, int>> frame_map;
    int base = seq_num;
    int next_seq_num = seq_num;
    bool send_done = false;

    while (!send_done || base != next_seq_num) {
        // Send frames within the window
        while (next_seq_num < base + WINDOW_SIZE && !send_done) {
            unsigned char data[MAX_DATA_SIZE];
            file.read((char *)data, MAX_DATA_SIZE);
            int data_size = file.gcount();

            unsigned char *frame = new unsigned char[MAX_FRAME_SIZE];
            int frame_size = create_frame(FILEDATA, next_seq_num, data, data_size, frame);
            frame_map[next_seq_num] = make_pair(frame, frame_size);

            // Send the frame
            if (sendto(sockfd, frame, frame_size, 0, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
                perror("Failed to send frame");
                exit(1);
            }

            cout << "[send data] seq_num " << next_seq_num << " sent" << endl;
            next_seq_num++;

            if (file.eof()) {
                send_done = true;
                break;
            }
        }

        // Wait for ACKs
        fd_set fds;
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_MS * 1000;  // Timeout in microseconds
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);

        int result = select(sockfd + 1, &fds, NULL, NULL, &timeout);
        if (result > 0) {
            // Receive ACK
            unsigned char ack[ACK_SIZE];
            socklen_t addr_len = sizeof(recv_addr);
            if (recvfrom(sockfd, ack, ACK_SIZE, 0, (struct sockaddr *)&recv_addr, &addr_len) < 0) {
                perror("Failed to receive ACK");
                exit(1);
            }

            int ack_seq_num;
            bool error;
            if (!read_ack(&ack_seq_num, &error, ack) && !error) {
                cout << "Received ACK for frame " << ack_seq_num << endl;
                if (ack_seq_num >= base) {
                    // Slide the window
                    for (int i = base; i <= ack_seq_num; i++) {
                        delete[] frame_map[i].first;
                        frame_map.erase(i);
                    }
                    base = ack_seq_num + 1;
                }
            } else {
                cout << "Received corrupt or incorrect ACK" << endl;
            }
        } else {
            // Timeout occurred, retransmit frames in the window
            cout << "Timeout, retransmitting frames from " << base << " to " << next_seq_num - 1 << endl;
            for (int i = base; i < next_seq_num; i++) {
                auto &frame_pair = frame_map[i];
                if (sendto(sockfd, frame_pair.first, frame_pair.second, 0, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
                    perror("Failed to retransmit frame");
                    exit(1);
                }
                cout << "[retransmit] seq_num " << i << endl;
            }
        }
    }

    // Send End-of-Transfer packet
    unsigned char eot_frame[MAX_FRAME_SIZE];
    int eot_frame_size = create_frame(END_OF_TRANSFER, next_seq_num, nullptr, 0, eot_frame);
    send_frame_with_retransmission(sockfd, recv_addr, eot_frame, eot_frame_size, next_seq_num);
    cout << "End-of-Transfer packet sent." << endl;
    seq_num = next_seq_num + 1;
}

// Send filename and directory
void send_filename(int sockfd, struct sockaddr_in &recv_addr, const string &subdir, const string &filename, int &seq_num) {
    // Prepare the filename data
    string filepath = subdir + "/" + filename;
    unsigned char data[MAX_DATA_SIZE];
    size_t total_length = filepath.length();
    if (total_length > MAX_DATA_SIZE) {
        cerr << "File path too long to send" << endl;
        exit(1);
    }
    memcpy(data, filepath.c_str(), total_length);

    // Create frame
    unsigned char frame[MAX_FRAME_SIZE];
    int frame_size = create_frame(FILENAME, seq_num, data, total_length, frame);

    // Send frame with retransmission
    send_frame_with_retransmission(sockfd, recv_addr, frame, frame_size, seq_num);
    cout << "Filename sent: " << filepath << endl;
    seq_num++;
}

int main(int argc, char *argv[]) {
    string recv_host, subdir, filename;
    int recv_port = 0;

    parse_arguments(argc, argv, recv_host, recv_port, subdir, filename);

    if (recv_host.empty() || recv_port == 0 || filename.empty()) {
        cerr << "Usage: sendfile -r <recv host>:<recv port> -f <subdir>/<filename>" << endl;
        return 1;
    }

    int sockfd = create_socket();

    struct sockaddr_in recv_addr = setup_recv_addr(recv_host, recv_port);

    string file_path = subdir + "/" + filename;
    ifstream file(file_path, ios::in | ios::binary);
    if (!file.is_open()) {
        cerr << "Error opening file: " << file_path << endl;
        close(sockfd);
        return 1;
    }

    int seq_num = 0;

    // Send filename and directory
    send_filename(sockfd, recv_addr, subdir, filename, seq_num);

    // Send file data
    send_data(sockfd, recv_addr, file, seq_num);

    close(sockfd);
    cout << "[completed]" << endl;
    return 0;
}



// #include <stdio.h>  // 标准输入输出库，用于文件操作和打印消息
// #include <stdlib.h> // 标准库，包含了`exit()`函数等
// #include <string.h> // 字符串操作库，用于处理字符串
// #include <unistd.h> // 提供UNIX系统调用函数，例如`close()`用于关闭文件描述符
// #include <arpa/inet.h> // 提供与网络地址操作相关的函数和结构体，例如`inet_addr()`
// #include <sys/socket.h> // 提供创建套接字以及进行网络通信的函数和结构体
// #include <sys/time.h>

// #define BUF_SIZE 1024
// #define WINDOW_SIZE 4
// #define TIMEOUT 5  // 超时时间 (秒)
// // #define FILENAME "testfile.bin"
// #define FILENAME "testfile_10MB.bin"

// // 处理程序中的错误。当发生错误时，perror()函数会打印详细的错误信息，
// // 然后程序会通过exit(1)安全退出。错误处理非常重要，因为它有助于在出错时提供有用的信息。
// void error_handling(const char *message) {
//     perror(message);
//     exit(1);
// }


// int main(int argc, char *argv[]) {
//     int sock;
//     struct sockaddr_in recv_addr;
//     FILE *fp;
//     char buffer[BUF_SIZE];
//     int bytes_read, sent_bytes, ack_num;
//     int base = 0, next_seq_num = 0;
//     socklen_t addr_len;
//     struct timeval tv;
//     fd_set reads, temp;
//     long file_size;

//     // 检查命令行参数
//     // argv[1]：接收端的IP地址。
//     // argv[2]：接收端的端口号。
//     // 如果参数不对就退出
//     if (argc != 3) {
//         printf("Usage: %s <Receiver IP> <Receiver Port>\n", argv[0]);
//         exit(1);
//     }

//     // 创建UDP套接字
//     // PF_INET：表示IPv4协议。
//     // SOCK_DGRAM：表示使用UDP协议进行数据报通信。
//     // 0：协议号，设置为0表示自动选择UDP协议。
//     // 如果套接字创建失败，程序会调用error_handling()进行错误处理并退出。
//     sock = socket(PF_INET, SOCK_DGRAM, 0);
//     if (sock == -1) {
//         error_handling("socket() error");
//     }

//     memset(&recv_addr, 0, sizeof(recv_addr));
//     recv_addr.sin_family = AF_INET; // 使用IPv4地址族
//     recv_addr.sin_addr.s_addr = inet_addr(argv[1]); // 接收端IP地址
//     recv_addr.sin_port = htons(atoi(argv[2])); // 接收端端口号 (需要更改)

//     // 打开文件
//     fp = fopen(FILENAME, "rb");
//     if (fp == NULL) {
//         error_handling("fopen() error");
//     }

//     // 获取文件大小
//     fseek(fp, 0, SEEK_END);
//     file_size = ftell(fp);
//     rewind(fp);
//     printf("File size: %ld bytes\n", file_size);

//     // 发送文件大小到接收端
//     file_size = htonl(file_size);
//     sendto(sock, &file_size, sizeof(file_size), 0, (struct sockaddr*)&recv_addr, sizeof(recv_addr));

//     FD_ZERO(&reads);
//     FD_SET(sock, &reads);
//     tv.tv_sec = TIMEOUT;
//     tv.tv_usec = 0;

//     while (1) {
//         // 发送窗口内的数据包
//         while (next_seq_num < base + WINDOW_SIZE && (bytes_read = fread(buffer + sizeof(int), 1, BUF_SIZE - sizeof(int), fp)) > 0) {
//             // 将序列号封装到数据包前
//             int network_seq_num = htonl(next_seq_num);
//             memcpy(buffer, &network_seq_num, sizeof(network_seq_num));

//             printf("Sending packet %d\n", next_seq_num);
//             sent_bytes = sendto(sock, buffer, bytes_read + sizeof(int), 0, (struct sockaddr*)&recv_addr, sizeof(recv_addr));
//             if (sent_bytes == -1) {
//                 error_handling("sendto() error");
//             }
//             next_seq_num++;
//         }

//         // 等待ACK，使用select设置超时
//         temp = reads;
//         if (select(sock + 1, &temp, 0, 0, &tv) == -1) {
//             error_handling("select() error");
//         }

//         if (FD_ISSET(sock, &temp)) {
//             addr_len = sizeof(recv_addr);
//             if (recvfrom(sock, &ack_num, sizeof(ack_num), 0, (struct sockaddr*)&recv_addr, &addr_len) > 0) {
//                 ack_num = ntohl(ack_num);  // 将网络字节序转换回主机字节序
//                 printf("Received ACK for packet %d\n", ack_num);
//                 base = ack_num + 1;  // 滑动窗口
//             }
//         } else {
//             // 超时重传窗口中的数据包
//             printf("Timeout, resending window from packet %d\n", base);
//             fseek(fp, base * (BUF_SIZE - sizeof(int)), SEEK_SET);  // 重置文件指针
//             next_seq_num = base;
//         }

//         // 如果所有数据包已确认，退出
//         if (feof(fp) && base == next_seq_num) {
//             break;
//         }
//     }

//     // 发送结束信号
//     int end_signal = -1;  // 使用特殊序列号标识结束信号
//     end_signal = htonl(end_signal);
//     sendto(sock, &end_signal, sizeof(end_signal), 0, (struct sockaddr*)&recv_addr, sizeof(recv_addr));
//     printf("Sent end signal\n");

//     fclose(fp);
//     close(sock);
//     printf("[completed] File transfer completed.\n");
//     return 0;
// }



// #include <stdio.h>         // 标准输入输出库，用于文件操作和打印消息
// #include <stdlib.h>        
// #include <string.h>        // 字符串操作库，用于处理字符串
// #include <unistd.h>        // 提供UNIX系统调用函数，例如`close()`用于关闭文件描述符
// #include <arpa/inet.h>     // 提供与网络地址操作相关的函数和结构体，例如`inet_addr()`
// #include <sys/socket.h>    // 提供创建套接字以及进行网络通信的函数和结构体

// #define BUF_SIZE 1024  // 定义每个数据包的大小
// // #define FILENAME "testfile.txt"  // 假设我们传输的文件名为testfile.txt
// #define FILENAME "testfile.bin"
// #define END_SIGNAL "END"  // 结束信号

// // 处理程序中的错误。当发生错误时，perror()函数会打印详细的错误信息，
// // 然后程序会通过exit(1)安全退出。错误处理非常重要，因为它有助于在出错时提供有用的信息。
// void error_handling(const char *message) {
//     perror(message);       // 打印系统错误信息
//     exit(1);               // 退出程序，错误码为1
// }

// int main(int argc, char *argv[]) {
//     // sock：表示套接字的文件描述符，用于网络通信。
//     // recv_addr：存储接收端的IP地址和端口信息。
//     // fp：文件指针，用于读取要发送的文件。
//     // buffer[]：数据缓冲区，用于存储每次从文件中读取的内容。
//     // bytes_read 和 sent_bytes：分别表示读取的字节数和发送的字节数。
//     int sock;
//     struct sockaddr_in recv_addr;
//     FILE *fp;
//     char buffer[BUF_SIZE];
//     int bytes_read, sent_bytes;
    
//     // 检查命令行参数
//     // argv[1]：接收端的IP地址。
//     // argv[2]：接收端的端口号。
//     // 如果参数不对就退出
//     if (argc != 3) {
//         printf("Usage: %s <Receiver IP> <Receiver Port>\n", argv[0]);
//         exit(1);
//     }
    
//     // 创建UDP套接字
//     // PF_INET：表示IPv4协议。
//     // SOCK_DGRAM：表示使用UDP协议进行数据报通信。
//     // 0：协议号，设置为0表示自动选择UDP协议。
//     // 如果套接字创建失败，程序会调用error_handling()进行错误处理并退出。
//     sock = socket(PF_INET, SOCK_DGRAM, 0);
//     if (sock == -1) {
//         error_handling("socket() error");
//     }

//     // 配置接收端的地址信息
//     memset(&recv_addr, 0, sizeof(recv_addr));
//     recv_addr.sin_family = AF_INET; // 使用IPv4地址族
//     recv_addr.sin_addr.s_addr = inet_addr(argv[1]);  // 接收端IP地址
//     recv_addr.sin_port = htons(atoi(argv[2]));  // 接收端端口号

//     // 打开要发送的文件，rb模式表示以二进制只读方式打开文件。如果文件不存在或无法打开，程序会报错并退出。
//     fp = fopen(FILENAME, "rb");
//     if (fp == NULL) {
//         error_handling("fopen() error");
//     } else {
//         fseek(fp, 0, SEEK_END);   // 移动到文件末尾
//         long file_size = ftell(fp);  // 获取文件大小
//         rewind(fp);  // 重置文件指针
//         printf("File opened successfully, size: %ld bytes\n", file_size);
//     }

//     // 读取文件并通过UDP发送
//     // 从文件中读取数据并通过UDP发送：
//     // fread(buffer, 1, BUF_SIZE, fp)：每次从文件读取BUF_SIZE字节的数据（即1024字节），并存储到缓冲区buffer中。
//     // sendto(sock, buffer, bytes_read, 0, (struct sockaddr*)&recv_addr, sizeof(recv_addr))：使用sendto()函数通过UDP发送读取到的字节数据。
//     // sock：UDP套接字。
//     // buffer：要发送的数据。
//     // bytes_read：读取到的数据长度。
//     // (struct sockaddr*)&recv_addr：接收端的地址信息。
//     while ((bytes_read = fread(buffer, 1, BUF_SIZE, fp)) > 0) {
//         printf("Bytes read from file: %zu\n", bytes_read);  // 调试输出
//         // 可选：打印缓冲区前10个字节的内容（十六进制）
//         printf("Buffer content: ");
//         for(int i = 0; i < bytes_read && i < 10; i++) {
//             printf("%02x ", (unsigned char)buffer[i]);
//         }
//         printf("\n");

//         sent_bytes = sendto(sock, buffer, bytes_read, 0, (struct sockaddr*)&recv_addr, sizeof(recv_addr));
//         if (sent_bytes != bytes_read) {
//             error_handling("sendto() error");
//         }
//         printf("[send data] %zu bytes sent\n", sent_bytes);
//     }
//     // 在文件发送完毕后，发送一个结束信号
//     sent_bytes = sendto(sock, END_SIGNAL, strlen(END_SIGNAL), 0, (struct sockaddr*)&recv_addr, sizeof(recv_addr));
//     if (sent_bytes != strlen(END_SIGNAL)) {
//         error_handling("sendto() error");
//     }
//     printf("[send data] End signal sent\n");


//     // 文件发送完毕后，关闭文件和套接字
//     fclose(fp);
//     close(sock);
//     printf("[completed] File transfer completed.\n");
    
//     return 0;
// }

