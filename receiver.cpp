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

#define PORT 8080
#define MAXLINE 1024
#define WINDOW_SIZE 5
#define TIMEOUT_MS 1000

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
    for (size_t i = 0; i < ntohs(packet.data_length); i++) {
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

    char buffer[MAXLINE];
    socklen_t len = sizeof(cliaddr);
    bool finished = false;
    Packet window[WINDOW_SIZE];
    memset(window, 0, sizeof(window)); // Initialize window packets
    int head = 0;                      // The oldest unacknowledged packet
    // int tail = 0;                      // The leatest packet to write
    int seqToWrite = 0;                // The seq to write
    auto now = chrono::steady_clock::now();

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
                // Handle actual errors appropriately
                exit(EXIT_FAILURE); // Or handle the error as needed
            }
        }
        

        // convert to os format
        decode(recv_packet);
        
        // verify the checksum
        if (recv_packet.checksum == calculateChecksum(recv_packet))
        {
            // check the packet's order
            if (recv_packet.seq_num > head + WINDOW_SIZE || recv_packet.seq_num < head || window[recv_packet.seq_num % WINDOW_SIZE].data_length != 0)
            {
                cout << "packet out of the window or duplicated";
            }
            // valid packet
            else
            {
                // set the packet to acked
                recv_packet.ack_num = 1;
                window[recv_packet.seq_num % WINDOW_SIZE] = recv_packet;
                cout<<"packet received, seq: " << recv_packet.seq_num << endl;

                // check the window
                for (int i = head; i < head + WINDOW_SIZE; i++)
                {
                    Packet headPacket = window[i % WINDOW_SIZE];
                    // the head packet hasn't been received yet
                    if (headPacket.ack_num == 0)
                    {
                        cout<<"head hasn't received yet"<<endl;
                        break;
                    }
                    // head packet ready
                    else if (headPacket.ack_num == 1)
                    {
                        // write the packet
                        // end of file
                        if (headPacket.data_length == 0)
                        {
                            cout << "File is received" << endl;
                            outputFile.close();
                            finished = true;
                        }
                        else
                        {
                            outputFile.write(headPacket.data, headPacket.data_length);
                            cout<<"write successful"<<endl;
                        }

                        // Set the ack packet
                        Packet ack_packet;
                        memset(&ack_packet, 0, sizeof(Packet));
                        ack_packet.ack_num = 1;
                        ack_packet.seq_num = headPacket.seq_num;
                        ack_packet.checksum = calculateChecksum(ack_packet);
                        ack_packet.data_length = 0;
                        
                        // convert to net format
                        encode(ack_packet);

                        sendto(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);
                        cout<<"ack back to the sender"<<endl;
                        // update the window parameters
                        head++;
                    }
                    
                    // head packet is corrupted
                    else if (headPacket.ack_num == 2){
                        // Set the ack packet
                        Packet ack_packet;
                        memset(&ack_packet, 0, sizeof(Packet));
                        ack_packet.ack_num = 2;
                        ack_packet.seq_num = headPacket.seq_num;
                        ack_packet.checksum = calculateChecksum(ack_packet);
                        ack_packet.data_length = 0;
                        
                        // convert to net format
                        decode(ack_packet);

                        sendto(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);
                    }
                }
            }
        }
        // packet corrupted, retransmit
        else
        {
            cout<<"packet corrupt"<<recv_packet.checksum<<" "<<calculateChecksum(recv_packet)<<endl;
            // Set the ack packet
            Packet ack_packet;
            memset(&ack_packet, 0, sizeof(Packet));
            ack_packet.ack_num = 2;
            ack_packet.seq_num = recv_packet.seq_num;
            ack_packet.checksum = calculateChecksum(ack_packet);
            ack_packet.data_length = 0;
            
            // convert to net format
            decode(ack_packet);

            sendto(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);
        }
    }
}

int main()
{
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;

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
    servaddr.sin_port = htons(PORT);

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
