// recvfile.cpp
#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <getopt.h>

using namespace std;

#define MAX_DATA_SIZE 512
#define MAX_FRAME_SIZE 522
#define ACK_SIZE 6
#define WINDOW_SIZE 5
#define TIMEOUT_MS 500

enum PacketType {
    FILENAME = 1,
    FILEDATA = 2,
    END_OF_TRANSFER = 3
};

struct Packet {
    int seq_num;
    int ack_num;
    u_char flags;
    int data_length;
    char data[MAX_DATA_SIZE];
    unsigned short checksum;
};

// Parse command line arguments
void parse_arguments(int argc, char *argv[], int &recv_port) {
    int opt;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
        case 'p':
            recv_port = atoi(optarg);
            break;
        default:
            cerr << "Usage: recvfile -p <recv port>" << endl;
            exit(1);
        }
    }

    if (recv_port <= 0) {
        cerr << "Invalid or missing port number. Use -p <recv port>" << endl;
        exit(1);
    }
}

// Send ACK for a received packet
void send_ack(int sockfd, struct sockaddr_in &sender_addr, socklen_t sender_len, int ack_num) {
    sendto(sockfd, &ack_num, sizeof(ack_num), 0, (struct sockaddr *)&sender_addr, sender_len);
    cout << "ACK sent for packet " << ack_num << endl;
}

// // Receive file and process packets
// void receive_file(int sockfd) {
//     map<int, Packet> recv_window;  // For storing out-of-order packets
//     int expected_seq_num = 0;      // Expected sequence number
//     bool transfer_complete = false;
//     string filename_received;
//     bool filename_received_flag = false;
//     ofstream output_file; // Output file stream

//     struct sockaddr_in sender_addr;
//     socklen_t sender_len = sizeof(sender_addr);

//     while (!transfer_complete) {
//         Packet packet;
//         int recv_len = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&sender_addr, &sender_len);
//         if (recv_len < 0) {
//             perror("Error receiving packet");
//             continue;
//         }

//         // Handle filename packet
//         if (packet.flags == FILENAME && packet.seq_num == expected_seq_num) {
//             filename_received = string(packet.data, packet.data_length);
//             filename_received_flag = true;

//             // Open output file with filename_received + ".recv"
//             string output_file_path = filename_received + ".recv";
//             output_file.open(output_file_path, ios::binary);
//             if (!output_file.is_open()) {
//                 cerr << "Error opening output file: " << output_file_path << endl;
//                 close(sockfd);
//                 exit(1);
//             }
//             cout << "Receiving file: " << filename_received << ", saving as: " << output_file_path << endl;

//             // Send ACK for filename packet
//             send_ack(sockfd, sender_addr, sender_len, packet.seq_num);
//             expected_seq_num++;
//             continue;
//         }

//         // Ensure output file is open
//         if (!filename_received_flag) {
//             cerr << "Error: Filename packet not received yet." << endl;
//             continue;
//         }

//         // Handle end-of-transfer packet
//         if (packet.flags == END_OF_TRANSFER) {
//             cout << "End of file transfer received." << endl;
//             transfer_complete = true;
//             send_ack(sockfd, sender_addr, sender_len, packet.seq_num);  // ACK end-of-transfer packet
//             break;
//         }

//         // Handle data packets
//         if (packet.seq_num == expected_seq_num) {
//             // In-order packet received
//             output_file.write(packet.data, packet.data_length);
//             cout << "Packet " << packet.seq_num << " received and written to file." << endl;

//             // Send ACK
//             send_ack(sockfd, sender_addr, sender_len, packet.seq_num);
//             expected_seq_num++;

//             // Check for any buffered out-of-order packets
//             while (recv_window.find(expected_seq_num) != recv_window.end()) {
//                 Packet &next_packet = recv_window[expected_seq_num];
//                 output_file.write(next_packet.data, next_packet.data_length);
//                 cout << "Out-of-order packet " << next_packet.seq_num << " received earlier, now written to file." << endl;

//                 send_ack(sockfd, sender_addr, sender_len, next_packet.seq_num);
//                 recv_window.erase(expected_seq_num);
//                 expected_seq_num++;
//             }
//         } else if (packet.seq_num > expected_seq_num) {
//             // Out-of-order packet received, store it
//             cout << "Out-of-order packet " << packet.seq_num << " received, storing in window." << endl;
//             recv_window[packet.seq_num] = packet;
//             // Optionally send ACK for the received packet
//             send_ack(sockfd, sender_addr, sender_len, packet.seq_num);
//         } else {
//             // Duplicate packet received
//             cout << "Duplicate packet " << packet.seq_num << " received, resending ACK." << endl;
//             send_ack(sockfd, sender_addr, sender_len, packet.seq_num);
//         }
//     }

//     if (output_file.is_open()) {
//         output_file.close();
//     }
// }





// 接收方处理数据包的函数
void receive_file(int sockfd) {
    int expected_seq_num = 0;      // 接收方期望的下一个包的序列号
    bool transfer_complete = false;
    string filename_received;
    bool filename_received_flag = false;
    ofstream output_file; // 输出文件流

    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    while (!transfer_complete) {
        Packet packet;
        int recv_len = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&sender_addr, &sender_len);
        if (recv_len < 0) {
            perror("Error receiving packet");
            continue;
        }

        // 处理文件传输结束包
        if (packet.flags == END_OF_TRANSFER) {
            cout << "End of file transfer received." << endl;
            transfer_complete = true;
            send_ack(sockfd, sender_addr, sender_len, packet.seq_num);  // ACK结束包
            break;
        }

        // 处理文件名包
        if (packet.flags == FILENAME && packet.seq_num == expected_seq_num) {
            filename_received = string(packet.data, packet.data_length);
            filename_received_flag = true;

            // 打开输出文件，命名为原文件名加上 .recv
            string output_file_path = filename_received + ".recv";
            output_file.open(output_file_path, ios::binary);
            if (!output_file.is_open()) {
                cerr << "Error opening output file: " << output_file_path << endl;
                close(sockfd);
                exit(1);
            }
            cout << "Receiving file: " << filename_received << ", saving as: " << output_file_path << endl;

            // 发送ACK
            send_ack(sockfd, sender_addr, sender_len, packet.seq_num);
            expected_seq_num++;
            continue;
        }

        // 确保文件名已接收
        if (!filename_received_flag) {
            cerr << "Error: Filename packet not received yet." << endl;
            continue;
        }

        // 处理正常的数据包
        if (packet.seq_num == expected_seq_num) {
            // 按顺序接收到包，写入文件
            output_file.write(packet.data, packet.data_length);
            cout << "Packet " << packet.seq_num << " received and written to file." << endl;

            // 发送累积ACK
            send_ack(sockfd, sender_addr, sender_len, packet.seq_num);
            expected_seq_num++;

            // 检查是否有缓存在窗口中的下一个期望包
            // 这里不需要，因为接收方不维护缓冲区
        } else {
            // 接收到乱序包，忽略数据，但发送当前期望的累积ACK
            cout << "Out-of-order packet " << packet.seq_num << " received, sending ACK for " << (expected_seq_num - 1) << "." << endl;
            send_ack(sockfd, sender_addr, sender_len, (expected_seq_num - 1));
        }
    }

    if (output_file.is_open()) {
        output_file.close();
    }
}


int main(int argc, char *argv[]) {
    int recv_port = 0;

    // Parse command line arguments
    parse_arguments(argc, argv, recv_port);

    // Create UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in recv_addr;
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(recv_port);
    recv_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket to the specified port
    if (bind(sockfd, (const struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        return 1;
    }

    // Start receiving file
    receive_file(sockfd);

    // Clean up
    close(sockfd);

    return 0;
}
