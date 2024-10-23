// Server side implementation of UDP client-server model (receiving a file)
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

#define PORT 8080
#define MAXLINE 1024
#define WINDOW_SIZE    200
#define TIMEOUT_MS     100
using namespace std;

struct Packet
{
    uint16_t seq_num;                           // Sequence number
    uint16_t ack_num;                           // Acknowledgment number
    uint16_t checksum;                          // For error detection
    uint16_t data_length;                       // Length of data in the packet
    chrono::steady_clock::time_point send_time; // Time when the packet was sent
    char data[MAXLINE];                         // Data payload
};

uint16_t calculateChecksum(const Packet& packet) {
    // unify the checksum as 0
    uint32_t checksum = 0;
    checksum += packet.seq_num;
    checksum += packet.ack_num;
    checksum += packet.data_length;
    uint16_t data_len = packet.data_length;
    if (data_len > MAXLINE) {
        std::cerr << "Invalid data_length: " << data_len << std::endl;
        data_len = MAXLINE; // Prevent buffer overflow
    }
    for (size_t i = 0; i < data_len; i++) {
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


void receiveFile(int sockfd, struct sockaddr_in &cliaddr)
{
    // Open the file to save received data
    ofstream outputFile("received_file.txt", ios::binary);
    if (!outputFile.is_open())
    {
        cerr << "Error opening file for writing." << endl;
        exit(EXIT_FAILURE);
    }

    socklen_t len = sizeof(cliaddr);
    bool finished = false;
    unordered_map<uint16_t, Packet> received_packets; // Map to store received packets
    uint16_t expected_seq_num = 0;                    // The next expected sequence number

    // Set socket to non-blocking mode
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    while (!finished)
    {
        Packet recv_packet;
        ssize_t n = recvfrom(sockfd, &recv_packet, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, &len);
        if (n == -1)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                // No data available, continue
                continue;
            }
            else
            {
                perror("recvfrom failed");
                exit(EXIT_FAILURE);
            }
        }

        // Convert from network to host byte order
        decode(recv_packet);

        // Verify the checksum
        if (recv_packet.checksum == calculateChecksum(recv_packet))
        {
            if (recv_packet.seq_num >= expected_seq_num && recv_packet.seq_num < expected_seq_num + WINDOW_SIZE)
            {
                // Packet is within the window
                received_packets[recv_packet.seq_num] = recv_packet;
                cout << "Received packet seq_num: " << recv_packet.seq_num << endl;

                // Send ACK for the received packet
                Packet ack_packet;
                memset(&ack_packet, 0, sizeof(Packet));
                ack_packet.seq_num = recv_packet.seq_num;
                ack_packet.ack_num = 1; // ACK
                ack_packet.checksum = calculateChecksum(ack_packet);
                ack_packet.data_length = 0;

                // Convert to network byte order
                encode(ack_packet);

                sendto(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);
                cout << "Sent ACK for seq_num: " << recv_packet.seq_num << endl;

                // Write packets to file in order
                while (received_packets.find(expected_seq_num) != received_packets.end())
                {
                    Packet &pkt = received_packets[expected_seq_num];

                    // Check for end of file
                    if (pkt.data_length == 0)
                    {
                        cout << "Received EOF packet seq_num: " << pkt.seq_num << endl;
                        outputFile.close();
                        finished = true;
                        break;
                    }
                    else
                    {
                        outputFile.write(pkt.data, pkt.data_length);
                        cout << "Wrote packet seq_num: " << pkt.seq_num << " to file" << endl;
                    }
                    received_packets.erase(expected_seq_num);
                    expected_seq_num++;
                }
            }
            else if (recv_packet.seq_num < expected_seq_num)
            {
                // Duplicate packet received
                cout << "Received duplicate packet seq_num: " << recv_packet.seq_num << endl;

                // Send ACK for the duplicate packet
                Packet ack_packet;
                memset(&ack_packet, 0, sizeof(Packet));
                ack_packet.seq_num = recv_packet.seq_num;
                ack_packet.ack_num = 1; // ACK
                ack_packet.checksum = calculateChecksum(ack_packet);
                ack_packet.data_length = 0;

                // Convert to network byte order
                encode(ack_packet);

                sendto(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);
                cout << "Sent duplicate ACK for seq_num: " << recv_packet.seq_num << endl;
            }
            else
            {
                // Packet outside the window; ignore or send ACK to help sender
                cout << "Received packet outside window seq_num: " << recv_packet.seq_num << endl;

                // Optionally, send ACK
                Packet ack_packet;
                memset(&ack_packet, 0, sizeof(Packet));
                ack_packet.seq_num = recv_packet.seq_num;
                ack_packet.ack_num = 1; // ACK
                ack_packet.checksum = calculateChecksum(ack_packet);
                ack_packet.data_length = 0;

                // Convert to network byte order
                encode(ack_packet);

                sendto(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);
                cout << "Sent ACK for seq_num outside window: " << recv_packet.seq_num << endl;
            }
        }
        else
        {
            // Packet corrupted; send NAK
            cout << "Received corrupted packet seq_num: " << recv_packet.seq_num << endl;

            // Send NAK for the corrupted packet
            Packet nak_packet;
            memset(&nak_packet, 0, sizeof(Packet));
            nak_packet.seq_num = recv_packet.seq_num;
            nak_packet.ack_num = 2; // NAK
            nak_packet.checksum = calculateChecksum(nak_packet);
            nak_packet.data_length = 0;

            // Convert to network byte order
            encode(nak_packet);

            sendto(sockfd, &nak_packet, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);
            cout << "Sent NAK for seq_num: " << recv_packet.seq_num << endl;
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        cout<<"enter the port first"<<endl;
        exit(EXIT_FAILURE);
    }
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    int port = atoi(argv[1]);
    
    /// TODO: check the port range

    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    // Filling server information
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Receive the file from the client
    receiveFile(sockfd, cliaddr);

    close(sockfd);
    return 0;
}
