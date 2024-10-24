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
#include <sys/time.h>

#define PORT 8080
#define MAXLINE 1024
#define WINDOW_SIZE    10
#define TIMEOUT_MS     100
using namespace std;

struct Packet
{
    uint16_t seq_num;                           // Sequence number
    uint16_t ack_num;                           // Acknowledgment number
    uint16_t checksum;                          // For error detection
    uint16_t data_length;                       // Length of data in the packet
    uint64_t timestamp_ms;
    char data[MAXLINE];                         // Data payload
};
// Function to get the current time in milliseconds
uint64_t getCurrentTimeInMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

uint16_t calculateChecksum(const Packet& packet) {
    // unify the checksum as 0
    uint32_t checksum = 0;
    checksum += packet.seq_num;
    checksum += packet.ack_num;
    checksum += packet.data_length;
    uint16_t data_len = packet.data_length; 
    if (data_len > MAXLINE) { 
        cerr << "Invalid data_length: " << data_len << endl; 
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
    socklen_t len = sizeof(cliaddr);
    bool finished = false;
    unordered_map<uint16_t, Packet> received_packets; // Map to store received packets
    uint16_t expected_seq_num = 0;                    // The next expected sequence number

    bool directory_received = false;
    bool filename_received = false;
    string filedirectory;
    ofstream outputFile;
    const int DATA_SEQ_START = 2; // Data packets start from seq_num = 2

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
            string status;
            size_t start_offset = 0;
            size_t length = recv_packet.data_length;
            if (recv_packet.seq_num >= expected_seq_num && recv_packet.seq_num < expected_seq_num + WINDOW_SIZE)
            {
                if(recv_packet.seq_num == expected_seq_num){
                    status = "ACCEPTED(in-order)";
                }else if (recv_packet.seq_num > expected_seq_num){
                    status = "ACCEPTED(out-of-order)";
                }
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
                    if (expected_seq_num == 0 && !directory_received)
                    {
                        // This is the directory packet
                        filedirectory = string(pkt.data, pkt.data_length);
                        // cout << "Received directory: " << filedirectory << endl;
                        directory_received = true;
                    }
                    else if (expected_seq_num == 1 && !filename_received)
                    {
                        string filename(pkt.data, pkt.data_length);
                        if (filename.empty())
                        {
                            cerr << "Filename is empty. Cannot proceed." << endl;
                            exit(EXIT_FAILURE);
                        }
                        string output_filename = filename + ".recv";
                        if (!filedirectory.empty())
                        {
                            // Create the directory if it doesn't exist
                            string command = "mkdir -p " + filedirectory;
                            system(command.c_str());
                            output_filename = filedirectory + "/" + filename + ".recv";
                        }
                        outputFile.open(output_filename, ios::binary);
                        if (!outputFile.is_open())
                        {
                            cerr << "Error opening file for writing: " << output_filename << endl;
                            exit(EXIT_FAILURE);
                        }
                        // cout << Receiving fileame << endl;
                        filename_received = true;
                    }
                    else
                    {
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
                            if (filename_received)
                            {
                                outputFile.write(pkt.data, pkt.data_length);
                                cout << "Wrote packet seq_num: " << pkt.seq_num << " to file" << endl;
                            }
                            else
                            {
                                cerr << "Filename not received yet. Cannot write data." << endl;
                            }
                        }
                    }
                    received_packets.erase(expected_seq_num);
                    expected_seq_num++;
                }
            }
            else if (recv_packet.seq_num < expected_seq_num)
            {
                status = "IGNORED";
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
                status = "IGNORED";
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
             // Print [recv data] message for data packets
            if (recv_packet.seq_num >= DATA_SEQ_START)
            {
                start_offset = (recv_packet.seq_num - DATA_SEQ_START) * MAXLINE;
                cout << "[recv data] " << start_offset << " (" << length << ") " << status << endl;
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
    int port = 0;
    int opt;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
        case 'p':
            port = atoi(optarg);
            if (port <= 0 || port > 65535) {
                cerr << "Invalid port number." << endl;
                exit(EXIT_FAILURE);
            }
            break;
        default:
            cerr << "Usage: " << argv[0] << " -p <port>" << endl;
            exit(EXIT_FAILURE);
        }
    }

    if (port == 0) {
        cerr << "Usage: " << argv[0] << " -p <port>" << endl;
        exit(EXIT_FAILURE);
    }
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    // int port = atoi(argv[1]);

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
    cout << "[complete]" << endl;
    close(sockfd);
    return 0;
}
