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
#include <unordered_map>

using namespace std;

#define PORT           8080
#define MAXLINE        1024
#define WINDOW_SIZE    5
#define TIMEOUT_MS     2000

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
    // Open the file
    ifstream file(filePath, ios::binary);
    if (!file.is_open()) {
        cerr << "Error opening file: " << filePath << endl;
        exit(EXIT_FAILURE);
    }

    unordered_map<uint16_t, Packet> sent_packets; // Map to store sent but unacknowledged packets
    uint16_t base = 0;       // Sequence number of the oldest unacknowledged packet
    uint16_t next_seq_num = 0; // Next sequence number to be used
    bool eof_sent = false;
    bool eof_acked = false;
    socklen_t len = sizeof(servaddr);

    // Set socket to non-blocking mode
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    while (!eof_acked) {
        // Send packets within window
        while ((next_seq_num < base + WINDOW_SIZE) && !eof_sent) {
            // Read a chunk of the file into the buffer
            char buffer[MAXLINE];
            file.read(buffer, MAXLINE);
            streamsize bytesRead = file.gcount();

            Packet packet;
            memset(&packet, 0, sizeof(Packet));
            packet.seq_num = next_seq_num;
            packet.ack_num = 0;
            packet.data_length = bytesRead;

            if (bytesRead > 0) {
                memcpy(packet.data, buffer, bytesRead);
            } else {
                // End of file reached, send EOF packet
                packet.data_length = 0;
                eof_sent = true;
            }

            packet.checksum = calculateChecksum(packet);
            packet.send_time = chrono::steady_clock::now();

            // Store the packet for potential retransmission (in host byte order)
            sent_packets[packet.seq_num] = packet;

            // Prepare a copy for sending
            Packet send_packet = packet; // Copy to avoid modifying the original
            // Convert to network byte order
            encode(send_packet);

            // Send the packet
            ssize_t bytes_sent = sendto(sockfd, &send_packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
            if (bytes_sent == -1) {
                perror("sendto failed");
                // Handle error
            } else {
                cout << "Sent packet seq_num: " << packet.seq_num << endl;
            }

            next_seq_num++;
        }

        // Wait for ACKs with a timeout
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
            // Timeout occurred, check for packet retransmission
            auto now = chrono::steady_clock::now();
            for (auto& entry : sent_packets) {
                Packet& pkt = entry.second;
                auto time_since_sent = chrono::duration_cast<chrono::milliseconds>(now - pkt.send_time);
                if (time_since_sent.count() >= TIMEOUT_MS) {
                    // Retransmit packet
                    pkt.send_time = now;
                    // Prepare a copy for sending
                    Packet resend_pkt = pkt; // Copy to avoid modifying the original
                    // Convert to network byte order
                    encode(resend_pkt);
                    ssize_t bytes_sent = sendto(sockfd, &resend_pkt, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
                    if (bytes_sent == -1) {
                        perror("sendto failed");
                    } else {
                        cout << "Timeout, retransmitted packet seq_num: " << pkt.seq_num << endl;
                    }
                }
            }
        } else if (FD_ISSET(sockfd, &readfds)) {
            // Data is available to read
            Packet ack_packet;
            ssize_t n = recvfrom(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)&servaddr, &len);
            if (n > 0) {
                // Convert from network to host byte order
                decode(ack_packet);

                uint16_t calc_checksum = calculateChecksum(ack_packet);
                if (ack_packet.checksum == calc_checksum && ack_packet.ack_num == 1) {
                    cout << "Received ACK for seq_num: " << ack_packet.seq_num << endl;

                    if (ack_packet.seq_num >= base && ack_packet.seq_num < next_seq_num) {
                        // Remove acknowledged packets from the map
                        for (uint16_t seq = base; seq <= ack_packet.seq_num; ++seq) {
                            sent_packets.erase(seq);
                        }
                        // Advance the base
                        base = ack_packet.seq_num + 1;
                    }
                    // Check if EOF packet was acknowledged
                    if (eof_sent && ack_packet.seq_num == (next_seq_num - 1)) {
                        eof_acked = true;
                        cout << "EOF packet acknowledged" << endl;
                    }
                } else {
                    cout << "Received corrupted or invalid ACK packet" << endl;
                }
            } else {
                perror("recvfrom failed");
            }
        }
    }

    cout << "File transfer completed successfully." << endl;
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
