// Client side implementation of UDP client-server model (sending a file)
#include <iostream>     // For cout, cerr
#include <cstdlib>      // For exit() and EXIT_FAILURE
#include <unistd.h>     // For close()
#include <cstring>      // For memset(), memcpy()
#include <sys/types.h>  // For socket types
#include <sys/socket.h> // For socket functions
#include <arpa/inet.h>  // For inet functions
#include <netinet/in.h> // For sockaddr_in
#include <fstream>      // For file handling
// #include <chrono>
#include <fcntl.h>      // For fcntl()
#include <sys/select.h> // For select()
#include <unordered_map>
/*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
#include <sys/time.h>  // For gettimeofday
// #include <stdint.h>    // For uint64_t
// #include <endian.h>    // For htobe64
/*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/


using namespace std;

#define PORT           8080
#define MAXLINE        1024
#define WINDOW_SIZE    10
#define TIMEOUT_MS     100
#define PRINT          false

// struct Packet {
//     uint16_t seq_num;    // Sequence number
//     uint16_t ack_num;    // Acknowledgment number
//     uint16_t checksum;   // For error detection
//     uint16_t data_length;  // Length of data in the packet
//     chrono::steady_clock::time_point send_time; // Time when the packet was sent
//     char data[MAXLINE];  // Data payload
// };

/*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
struct Packet {
    uint16_t seq_num;    // Sequence number
    uint16_t ack_num;    // Acknowledgment number
    uint16_t checksum;   // For error detection
    uint16_t data_length;  // Length of data in the packet
    uint64_t timestamp_ms; // Time when the packet was sent, in milliseconds    
    char data[MAXLINE];  // Data payload
};

// Function to convert timeval to milliseconds since epoch
uint64_t getCurrentTimeInMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);  // 获取当前时间
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000;  // 转换为毫秒
}

// 计算两个时间戳的差异，返回毫秒数
long get_time_diff_ms(const struct timeval& start, const struct timeval& end) {
    long seconds = end.tv_sec - start.tv_sec;
    long microseconds = end.tv_usec - start.tv_usec;
    return seconds * 1000 + microseconds / 1000;
}
/*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/


uint16_t calculateChecksum(const Packet& packet) {
    // unify the checksum as 0
    uint32_t checksum = 0;
    checksum += packet.seq_num;
    checksum += packet.ack_num;
    checksum += packet.data_length;
    for (size_t i = 0; i < packet.data_length; i++) {
        checksum += static_cast<uint8_t>(packet.data[i]);
    }
    // Add carries
    while (checksum >> 16) {
        checksum = (checksum & 0xFFFF) + (checksum >> 16);
    }
    return static_cast<uint16_t>(~checksum);
}

void encode(Packet& sendPacket){
    sendPacket.seq_num = htons(sendPacket.seq_num);
    sendPacket.ack_num = htons(sendPacket.ack_num);
    sendPacket.checksum = htons(sendPacket.checksum);
    sendPacket.data_length = htons(sendPacket.data_length);
}

void decode(Packet& recvPacket){
    recvPacket.seq_num = ntohs(recvPacket.seq_num);
    recvPacket.ack_num = ntohs(recvPacket.ack_num);
    recvPacket.checksum = ntohs(recvPacket.checksum);
    recvPacket.data_length = ntohs(recvPacket.data_length);
}

// Function to send a file over UDP
void sendFile(const char* filePath, int sockfd, struct sockaddr_in& servaddr) {  
    // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/    
    // // 打开日志文件
    // ofstream logFile("transfer_log.txt", ios::out);
    // if (!logFile.is_open()) {
    //     cerr << "Error opening log file!" << endl;
    //     return;
    // }
    // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
    /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
     // 记录开始时间
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/

    /*获取文件名-------------------------------------------------------------------------------------------------------------------------------------------*/
    // Extract the filename from the file path
    string filename = filePath;
    size_t pos = filename.find_last_of("/\\");
    if (pos != string::npos) {
        filename = filename.substr(pos + 1);
    }
    /*获取文件名结束--------------------------------------------------------------------------------------------------------------------------------------------*/

    // Open the file
    ifstream file(filePath, ios::binary);
    if (!file.is_open()) {
        cerr << "Error opening file: " << filePath << endl;
        exit(EXIT_FAILURE);
    }

    /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
    // Calculate total number of packets to send
    file.seekg(0, ios::end);    // Move to the end of the file
    streamsize fileSize = file.tellg();    // Get file size
    file.seekg(0, ios::beg);    // Move back to the beginning of the file

    int total_packets = static_cast<int>(fileSize / MAXLINE) + ((fileSize % MAXLINE) != 0 ? 1 : 0); // Calculate total packets
    cout << "Total number of packets to send: " << total_packets << endl;
    // logFile << "Total number of packets to send: " << total_packets << endl;  // Log this information
    /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/

    Packet window[WINDOW_SIZE];
    memset(window, 0, sizeof(window)); // Initialize window packets
    char buffer[MAXLINE];
    int head = 0;   // The sequence number of the oldest unacknowledged packet
    int tail = 0;    // Next sequence number to use

    unordered_map<int, bool> isAcked;   // seq, bool
    socklen_t len = sizeof(servaddr);
    bool doneReading = false;

    /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
    struct timeval now;
    // auto now = chrono::steady_clock::now();
    /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/

    // Set socket to non-blocking mode
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    /*发送文件名字-------------------------------------------------------------------------------------------------------------------------------------------------*/
    // Send the filename packet
    Packet& filename_packet = window[tail % WINDOW_SIZE];
    memset(&filename_packet, 0, sizeof(Packet));
    filename_packet.seq_num = tail;
    filename_packet.ack_num = 0;
    filename_packet.data_length = filename.length();
    memcpy(filename_packet.data, filename.c_str(), filename_packet.data_length);
    filename_packet.checksum = calculateChecksum(filename_packet);
    filename_packet.timestamp_ms = getCurrentTimeInMs();

    // Encode to network byte order
    encode(filename_packet);

    // Send the filename packet
    ssize_t bytes_sent = sendto(sockfd, &filename_packet, sizeof(Packet), 0,
                                (const struct sockaddr*)&servaddr, len);
    if (bytes_sent == -1) {
        perror("sendto failed");
        // Handle error
    } else {
        isAcked[filename_packet.seq_num] = false;
        cout << "Sent filename packet seq_num: " << filename_packet.seq_num << endl;
    }

    // Increment tail after sending the filename packet
    tail++;
    /*发送文件名字-------------------------------------------------------------------------------------------------------------------------------------------------*/


    while (!doneReading || head < tail) {
        // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
        // // 记录当前窗口信息到日志
        // logFile << "Current Window (seq nums): ";
        // for (int i = 0; i < WINDOW_SIZE; ++i) {
        //     int seq_num = head + i;
        //     logFile << seq_num << " ";
        // }
        // logFile << endl;

        // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/

        // Send packets within window
        while (!doneReading && tail < head + WINDOW_SIZE) {
            // Read a chunk of the file into the buffer
            file.read(buffer, MAXLINE);
            streamsize bytesRead = file.gcount();

            if (bytesRead > 0) {
                // initilize the packet
                Packet& packet = window[tail % WINDOW_SIZE];
                memset(&packet, 0, sizeof(Packet));

                // set the packet
                packet.seq_num = tail;
                packet.ack_num = 0;
                packet.data_length = bytesRead;
                memcpy(packet.data, buffer, bytesRead);
                packet.checksum = calculateChecksum(packet);
                /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
                // 使用当前时间戳初始化时间字段（以毫秒为单位）
                packet.timestamp_ms = getCurrentTimeInMs();
                // packet.send_time = chrono::steady_clock::now();
                /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
                // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                // logFile << "Sending Packet (seq num): " << packet.seq_num << endl;
                // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/

                // convert to net fromat
                encode(packet);

                // Send the packet
                ssize_t bytes_sent = sendto(sockfd, &packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
                if (bytes_sent == -1) {
                    perror("sendto failed");
                    // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                    // logFile << "Error sending packet (seq num): " << packet.seq_num << endl;
                    // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                    // // Handle error
                } else {
                    isAcked[ntohs(packet.seq_num)] = false;
                    cout << "Sent packet tail: " << tail <<  endl;
                }

                tail++;
            } else {
                // No more data to read
                doneReading = true;
                break;
            }
        }

        // Set up select() for timeout handling
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100 ms

        int rv = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        if (rv == -1) {
            perror("select");
            // Handle error
        } else if (rv == 0) {
            // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
            // // 超时，检查是否需要重发数据包
            // logFile << "Timeout, checking for retransmissions." << endl;
            // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/

            // Timeout occurred, check for packet timeouts
            cout<<"recv timeout, check the sent"<<endl;
        } else if (FD_ISSET(sockfd, &readfds)){
            // Data is available to read
            Packet ack_packet;
            ssize_t n = recvfrom(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)&servaddr, &len);
            if (n > 0) {
                // Convert packet to os order
                decode(ack_packet);
                // cout<<ack_packet.seq_num<<endl;

                uint16_t calc_checksum = calculateChecksum(ack_packet);
                if (ack_packet.checksum == calc_checksum) {
                    // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                    // logFile << "Received ACK for packet (seq num): " << ack_packet.seq_num << endl;
                    // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/

                    if (ack_packet.ack_num == 1) {
                        // Duplicate Ack packet, ignore
                        if(isAcked[ack_packet.seq_num] == true){
                            cout<<"Duplicate Ack, seq: "<<ack_packet.seq_num<<endl;
                            // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                            // logFile << "Duplicate ACK (seq num): " << ack_packet.seq_num << endl;
                            // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                        }
                        
                        // Ack packet in the window, update
                        else{
                            isAcked[ack_packet.seq_num] = true;
                            cout << "Received ACK for packet " << ack_packet.seq_num << endl;
                            // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                            // logFile << "Acknowledged packet (seq num): " << ack_packet.seq_num << endl;
                            // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                            // update the window
                            window[ack_packet.seq_num % WINDOW_SIZE].ack_num = 1;

                            // Slide window if head packet is acknowledged
                            while (window[head % WINDOW_SIZE].ack_num == 1 && head < tail) {
                                head++;
                            }
                            if(PRINT){
                                cout<< "Window updated, window: ";
                                for(int i = 0; i < WINDOW_SIZE; i++){
                                    cout<<ntohs(window[i].seq_num)<<" ";
                                }
                                cout<<endl;
                            }
                            
                            
                        }
                        
                        
                        
                    } 
                    else if (ack_packet.ack_num == 2 && ack_packet.seq_num >= head && ack_packet.seq_num < tail) 
                    {                             
                        // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                        // logFile << "Packet corrupted, retransmitting seq num: " << ack_packet.seq_num << endl;
                        // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                        // Packet corrupted, retransmit
                        cout << "Packet corrupted, retransmit tail: " << ack_packet.seq_num << endl;
                        int seq_index = ack_packet.seq_num % WINDOW_SIZE;
                        Packet& packet_to_resend = window[seq_index];
                        /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
                        packet_to_resend.timestamp_ms = getCurrentTimeInMs();
                        // packet_to_resend.send_time = chrono::steady_clock::now();
                        /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
   
                        ssize_t bytes_sent = sendto(sockfd, &packet_to_resend, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
                        if (bytes_sent == -1) {
                            perror("sendto failed");
                        // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                        // logFile << "Failed to resend packet (seq num): " << ack_packet.seq_num << endl;
                        // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                        } else {
                            cout << "Resent packet tail: " << ack_packet.seq_num << endl;
                            
                            // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                            // logFile << "Resent packet (seq num): " << ack_packet.seq_num << endl;
                            // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                        }
                    } else{
                        cout<<"current window" << head <<", "<<tail-1;
                        cout << " ack outside the window, seq: "<< ack_packet.seq_num<<endl;
                        // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                        // logFile << "Received corrupted ACK packet (seq num): " << ack_packet.seq_num << endl;
                        // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                    }
                } else {
                    cout << "Received corrupted ACK packet" << endl;
                }
            } else {
                perror("recvfrom failed");
            }
            
        }

        /// TODO: logic need to be updated!
        
        // Check for timeouts and retransmit if necessary
        

        /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
        gettimeofday(&now, NULL);
        // now = chrono::steady_clock::now();
        /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/

        for (int i = head; i < tail; i++) {
            Packet& packet = window[i % WINDOW_SIZE];
            if (packet.ack_num != 1) {
                /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
                // auto time_since_sent = chrono::duration_cast<chrono::milliseconds>(now - packet.send_time);
                // if (time_since_sent.count() >= TIMEOUT_MS) {
                if (getCurrentTimeInMs() - packet.timestamp_ms >= TIMEOUT_MS){
                    // 重传数据包
                    packet.timestamp_ms = getCurrentTimeInMs();
                /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
                    // packet.send_time = now;
                    // Resend packet
                    ssize_t bytes_sent = sendto(sockfd, &packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
                    if (bytes_sent == -1) {
                        perror("sendto failed");
                        // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                        // logFile << "Timeout, retransmitting packet (seq num): " << packet.seq_num << endl;
                        // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
                    } else {
                        cout << "Timeout, retransmitted packet tail: " << ntohs(packet.seq_num) << endl;
                    }
                }
            }
        }
    }

    // Send an end-of-file packet
    Packet eof_packet;
    memset(&eof_packet, 0, sizeof(Packet));
    eof_packet.seq_num = tail;
    eof_packet.ack_num = 0;
    eof_packet.data_length = 0; // No data
    eof_packet.checksum = calculateChecksum(eof_packet);

    // Send EOF packet until it's acknowledged
    bool eof_acknowledged = false;
    /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
    // auto eof_send_time = chrono::steady_clock::now();
    /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/


    // convert to net format
    encode(eof_packet);

    while (!eof_acknowledged) {
        ssize_t bytes_sent = sendto(sockfd, &eof_packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
        if (bytes_sent == -1) {
            perror("sendto failed");
        } else {
            // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
            // logFile << "Sent EOF packet" << endl;
            // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
            cout << "Sent EOF packet" << endl;
        }

        // Wait for ACK
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100 ms

        int rv = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        if (rv == -1) {
            perror("select");
            // Handle error
        } else if (rv == 0) {
            // // Timeout occurred, check if we should resend EOF packet
            // auto time_since_sent = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - eof_send_time);
            // if (time_since_sent.count() >= TIMEOUT_MS) {
            //     eof_send_time = chrono::steady_clock::now();
            //     cout << "Timeout waiting for EOF ACK, retransmitting EOF packet" << endl;
            // }
            // continue;
            cout<<"eof timeout, check the sent"<<endl;
        } else {
            if (FD_ISSET(sockfd, &readfds)) {
                Packet ack_packet;
                ssize_t n = recvfrom(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)&servaddr, &len);
                if (n > 0) {
                    decode(ack_packet);

                    uint16_t calc_checksum = calculateChecksum(ack_packet);
                    if (ack_packet.checksum == calc_checksum) {
                        cout << "Received ACK for EOF packet" << endl;
                        eof_acknowledged = true;
                        break;
                    } else {
                        cout << "Received corrupted or incorrect ACK for EOF packet" << endl;
                    }
                } else {
                    perror("recvfrom failed");
                }
            }
        }
    }
    /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
    // record end time
    gettimeofday(&end_time, NULL);

    // 计算传输花费的总时间
    long transfer_time_ms = get_time_diff_ms(start_time, end_time);
    cout << "File transfer completed successfully." << endl;
    cout << "Total transfer time: " << transfer_time_ms << " ms" << endl;
    /*修改-------------------------------------------------------------------------------------------------------------------------------------------------*/
    // cout << "File transfer completed successfully." << endl;
    file.close();
}



// Driver code
int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <recv host> <recv port> <subdir>/<filename>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* host = argv[1];
    int port = atoi(argv[2]);
    const char* file_path = argv[3];

    cout<<host<<" "<<port<<" "<<file_path<<endl;

    int sockfd;
    struct sockaddr_in servaddr;

    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));

    // Filling server information
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);

    // Set server IP address (e.g., localhost)
    // "127.0.0.1"
    if (inet_pton(AF_INET, host, &servaddr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // Send file
    // const char* filePath = "file.txt";
    sendFile(file_path, sockfd, servaddr);

    close(sockfd);
    return 0;
}