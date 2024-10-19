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
#include <chrono>
#include <fcntl.h>      // For fcntl()
#include <sys/select.h> // For select()

using namespace std;

#define PORT           18020
#define MAXLINE        1024
#define WINDOW_SIZE    5
#define TIMEOUT_MS     1000

struct Packet {
    uint16_t seq_num;    // Sequence number
    uint16_t ack_num;    // Acknowledgment number
    uint16_t checksum;   // For error detection
    uint16_t data_length;  // Length of data in the packet
    chrono::steady_clock::time_point send_time; // Time when the packet was sent
    char data[MAXLINE];  // Data payload
};

uint16_t calculateChecksum(const Packet& packet) {
    // unify the checksum as 0
    uint32_t checksum = 0;
    checksum += ntohs(packet.seq_num);
    checksum += ntohs(packet.ack_num);
    checksum += ntohs(packet.data_length);
    for (size_t i = 0; i < ntohs(packet.data_length); i++) {
        checksum += static_cast<uint8_t>(packet.data[i]);
    }
    // Add carries
    while (checksum >> 16) {
        checksum = (checksum & 0xFFFF) + (checksum >> 16);
    }
    return static_cast<uint16_t>(~checksum);
}

// Function to send a file over UDP
void sendFile(const char* filePath, int sockfd, struct sockaddr_in& servaddr) {
    // Open the file
    ifstream file(filePath, ios::binary);
    if (!file.is_open()) {
        cerr << "Error opening file: " << filePath << endl;
        exit(EXIT_FAILURE);
    }

    Packet window[WINDOW_SIZE];
    memset(window, 0, sizeof(window)); // Initialize window packets
    char buffer[MAXLINE];
    int head = 0;   // The sequence number of the oldest unacknowledged packet
    int tail = 0;    // Next sequence number to use
    socklen_t len = sizeof(servaddr);
    bool doneReading = false;
    auto now = chrono::steady_clock::now();

    // Set socket to non-blocking mode
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    while (!doneReading || head < tail) {
        // Send packets within window
        while (!doneReading && tail < head + WINDOW_SIZE) {
            // Read a chunk of the file into the buffer
            file.read(buffer, MAXLINE);
            streamsize bytesRead = file.gcount();

            if (bytesRead > 0) {
                Packet& packet = window[tail % WINDOW_SIZE];
                memset(&packet, 0, sizeof(Packet));
                packet.seq_num = htons(tail);
                packet.ack_num = htons(0);
                packet.data_length = htons(bytesRead);
                memcpy(packet.data, buffer, bytesRead);
                packet.checksum = 0;
                packet.checksum = htons(calculateChecksum(packet));
                packet.send_time = chrono::steady_clock::now();

                // Send the packet
                ssize_t bytes_sent = sendto(sockfd, &packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
                if (bytes_sent == -1) {
                    perror("sendto failed");
                    // Handle error
                } else {
                    cout << "Sent packet tail: " << tail <<  endl;
                    cout << bytes_sent << "bytes sent" << endl;
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
            // Timeout occurred, check for packet timeouts
            cout<<"recv timeout, check the sent"<<endl;
        } else {
            if (FD_ISSET(sockfd, &readfds)) {
                // Data is available to read
                Packet ack_packet;
                ssize_t n = recvfrom(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)&servaddr, &len);
                if (n > 0) {
                    // Convert fields from network byte order
                    ack_packet.seq_num = ntohs(ack_packet.seq_num);
                    ack_packet.ack_num = ntohs(ack_packet.ack_num);
                    ack_packet.checksum = ntohs(ack_packet.checksum);
                    

                    uint16_t calc_checksum = calculateChecksum(ack_packet);
                    if (ack_packet.checksum == calc_checksum) {
                        if (ack_packet.ack_num == 1) {
                            cout << "Received ACK for packet: " << ack_packet.seq_num << endl;
                            int seq_index = ack_packet.seq_num % WINDOW_SIZE;
                            window[seq_index].ack_num = 1;

                            // Slide window if head packet is acknowledged
                            while (window[head % WINDOW_SIZE].ack_num == 1 && head < tail) {
                                head++;
                            }
                        } else if (ack_packet.ack_num == 2) {
                            // Packet corrupted, retransmit
                            cout << "Packet corrupted, retransmit tail: " << ack_packet.seq_num << endl;
                            int seq_index = ack_packet.seq_num % WINDOW_SIZE;
                            Packet& packet_to_resend = window[seq_index];
                            packet_to_resend.send_time = chrono::steady_clock::now();
                            ssize_t bytes_sent = sendto(sockfd, &packet_to_resend, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
                            if (bytes_sent == -1) {
                                perror("sendto failed");
                            } else {
                                cout << "Resent packet tail: " << ack_packet.seq_num << endl;
                            }
                        }
                    } else {
                        cout << "Received corrupted ACK packet" << endl;
                    }
                } else {
                    perror("recvfrom failed");
                }
            }
        }

        // Check for timeouts and retransmit if necessary
        now = chrono::steady_clock::now();
        for (int i = head; i < tail; i++) {
            Packet& packet = window[i % WINDOW_SIZE];
            if (packet.ack_num != 1) {
                auto time_since_sent = chrono::duration_cast<chrono::milliseconds>(now - packet.send_time);
                if (time_since_sent.count() >= TIMEOUT_MS) {
                    packet.send_time = now;
                    // Resend packet
                    ssize_t bytes_sent = sendto(sockfd, &packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
                    if (bytes_sent == -1) {
                        perror("sendto failed");
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
    eof_packet.seq_num = htons(tail);
    eof_packet.ack_num = htons(0);
    eof_packet.data_length = 0; // No data
    eof_packet.checksum = htons(calculateChecksum(eof_packet));

    // Send EOF packet until it's acknowledged
    bool eof_acknowledged = false;
    auto eof_send_time = chrono::steady_clock::now();

    while (!eof_acknowledged) {
        ssize_t bytes_sent = sendto(sockfd, &eof_packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
        if (bytes_sent == -1) {
            perror("sendto failed");
        } else {
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
            // Timeout occurred, check if we should resend EOF packet
            auto time_since_sent = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - eof_send_time);
            if (time_since_sent.count() >= TIMEOUT_MS) {
                eof_send_time = chrono::steady_clock::now();
                cout << "Timeout waiting for EOF ACK, retransmitting EOF packet" << endl;
            }
            continue;
        } else {
            if (FD_ISSET(sockfd, &readfds)) {
                Packet ack_packet;
                ssize_t n = recvfrom(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)&servaddr, &len);
                if (n > 0) {
                    ack_packet.seq_num = ntohs(ack_packet.seq_num);
                    ack_packet.ack_num = ntohs(ack_packet.ack_num);
                    ack_packet.checksum = ntohs(ack_packet.checksum);

                    uint16_t calc_checksum = calculateChecksum(ack_packet);
                    if (ack_packet.checksum == calc_checksum && ack_packet.seq_num == tail && ack_packet.ack_num == 1) {
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

    cout << "File transfer completed successfully." << endl;
    file.close();
}

// Driver code
int main() {
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
    servaddr.sin_port = htons(PORT);

    // Set server IP address (e.g., localhost)
    if (inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // Send file
    const char* filePath = "file.txt";
    sendFile(filePath, sockfd, servaddr);

    close(sockfd);
    return 0;
}
