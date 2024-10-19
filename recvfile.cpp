// recvfile.cpp
#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <getopt.h>
#include <map>
#include <sys/stat.h>

using namespace std;

#define MAX_DATA_SIZE 512  // Each frame carries up to 512 bytes
#define MAX_FRAME_SIZE 522  // Frame size: headers + data + checksum
#define ACK_SIZE 6
#define WINDOW_SIZE 5  // Sliding window size

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

// Read data frame and verify checksum
bool read_frame(PacketType &pkt_type, int &seq_num, unsigned char *data, int &data_size, const unsigned char *frame) {
    pkt_type = static_cast<PacketType>(frame[0]);
    uint32_t net_seq_num;
    memcpy(&net_seq_num, frame + 1, 4);  // Read sequence number
    seq_num = ntohl(net_seq_num);

    uint32_t net_data_size;
    memcpy(&net_data_size, frame + 5, 4);  // Read data size
    data_size = ntohl(net_data_size);

    memcpy(data, frame + 9, data_size);  // Read data
    unsigned char received_checksum = frame[data_size + 9];
    unsigned char calculated_checksum = checksum(frame, data_size + 9);

    // Return true if checksum does not match
    return received_checksum != calculated_checksum;
}

// Create ACK
void create_ack(int seq_num, unsigned char *ack, bool error) {
    ack[0] = error ? 0x0 : 0x1;
    uint32_t net_seq_num = htonl(seq_num);
    memcpy(ack + 1, &net_seq_num, 4);
    ack[5] = checksum(ack, ACK_SIZE - 1);
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
}

// Set up receiver address
struct sockaddr_in setup_recv_addr(int recv_port) {
    struct sockaddr_in recv_addr;
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(recv_port);
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    return recv_addr;
}

// Receive data and write to file
void receive_data(int sockfd, struct sockaddr_in &sender_addr) {
    unsigned char buffer[MAX_FRAME_SIZE];
    unsigned char data[MAX_DATA_SIZE];
    int expected_seq_num = 0;
    map<int, pair<unsigned char *, int>> frame_buffer;

    string filepath;
    ofstream file;
    bool filename_received = false;

    bool receive_done = false;
    while (!receive_done) {
        socklen_t addr_len = sizeof(sender_addr);
        int frame_size = recvfrom(sockfd, buffer, MAX_FRAME_SIZE, 0, (struct sockaddr *)&sender_addr, &addr_len);
        if (frame_size < 0) {
            perror("Failed to receive frame");
            exit(1);
        }

        PacketType pkt_type;
        int seq_num, data_size;
        if (read_frame(pkt_type, seq_num, data, data_size, buffer)) {
            cout << "[recv corrupt packet]" << endl;
            continue;
        }

        // Send ACK
        unsigned char ack[ACK_SIZE];
        create_ack(seq_num, ack, false);
        sendto(sockfd, ack, ACK_SIZE, 0, (struct sockaddr *)&sender_addr, addr_len);
        cout << "Sending ACK for frame " << seq_num << endl;

        if (pkt_type == FILENAME && !filename_received) {
            // Receive filename and directory
            filepath = string((char *)data, data_size) + ".recv";
            cout << "Received file path: " << filepath << endl;

            // Create directory if it doesn't exist
            size_t last_slash = filepath.find_last_of('/');
            if (last_slash != string::npos) {
                string dir_path = filepath.substr(0, last_slash);
                struct stat st = {0};
                if (stat(dir_path.c_str(), &st) == -1) {
                    mkdir(dir_path.c_str(), 0700);
                }
            }

            // Open file for writing
            file.open(filepath, ios::out | ios::binary);
            if (!file.is_open()) {
                cerr << "Error opening file for writing: " << filepath << endl;
                exit(1);
            }

            filename_received = true;
            expected_seq_num = seq_num + 1;
        } else if (pkt_type == FILEDATA && filename_received) {
            if (seq_num == expected_seq_num) {
                // Write in-order frame to file
                file.write((char *)data, data_size);
                cout << "[recv data] seq_num " << seq_num << " ACCEPTED" << endl;
                expected_seq_num++;

                // Check if any buffered frames can be written
                while (frame_buffer.count(expected_seq_num) > 0) {
                    auto &buf_pair = frame_buffer[expected_seq_num];
                    file.write((char *)buf_pair.first, buf_pair.second);
                    delete[] buf_pair.first;
                    frame_buffer.erase(expected_seq_num);
                    cout << "[recv data] seq_num " << expected_seq_num << " ACCEPTED from buffer" << endl;
                    expected_seq_num++;
                }
            } else if (seq_num > expected_seq_num) {
                // Buffer out-of-order frame
                unsigned char *buffered_data = new unsigned char[data_size];
                memcpy(buffered_data, data, data_size);
                frame_buffer[seq_num] = make_pair(buffered_data, data_size);
                cout << "[recv data] seq_num " << seq_num << " BUFFERED" << endl;
            } else {
                // Duplicate frame, already received
                cout << "[recv data] seq_num " << seq_num << " DUPLICATE" << endl;
            }
        } else if (pkt_type == END_OF_TRANSFER) {
            cout << "End-of-Transfer packet received." << endl;
            receive_done = true;

            // Write any remaining buffered frames
            while (frame_buffer.count(expected_seq_num) > 0) {
                auto &buf_pair = frame_buffer[expected_seq_num];
                file.write((char *)buf_pair.first, buf_pair.second);
                delete[] buf_pair.first;
                frame_buffer.erase(expected_seq_num);
                cout << "[recv data] seq_num " << expected_seq_num << " ACCEPTED from buffer" << endl;
                expected_seq_num++;
            }
        }
    }

    if (file.is_open()) {
        file.close();
    }
}

int main(int argc, char *argv[]) {
    int recv_port = 0;

    parse_arguments(argc, argv, recv_port);

    if (recv_port == 0) {
        cerr << "Usage: recvfile -p <recv port>" << endl;
        return 1;
    }

    int sockfd = create_socket();

    struct sockaddr_in recv_addr = setup_recv_addr(recv_port);

    if (bind(sockfd, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        return 1;
    }

    struct sockaddr_in sender_addr;
    receive_data(sockfd, sender_addr);

    close(sockfd);
    cout << "[completed]" << endl;
    return 0;
}
