// sendfile.cpp
#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <getopt.h>
#include <map>
#include <chrono>
#include <netdb.h>
#include <set>

using namespace std;
using namespace std::chrono;

// Constants
#define MAX_DATA_SIZE 512   // Maximum data size per packet
#define MAX_FRAME_SIZE 522  // Frame size: headers + data + checksum
#define ACK_SIZE 6          // ACK packet size (not used in this code)
#define WINDOW_SIZE 5       // Sliding window size
#define TIMEOUT_MS 500      // Retransmission timeout in milliseconds

enum PacketType {
    FILENAME = 1,       // Packet contains filename
    FILEDATA = 2,       // Packet contains file data
    END_OF_TRANSFER = 3 // Packet indicates end of file transfer
};

struct Packet {
    int seq_num;                // Sequence number
    int ack_num;                // Acknowledgment number (not used in this code)
    u_char flags;               // Packet type
    int data_length;            // Length of data in bytes
    char data[MAX_DATA_SIZE];   // Data payload
    unsigned short checksum;    // Checksum (not implemented in this code)
};

// Function to parse command line arguments
void parse_arguments(int argc, char *argv[], string &recv_host, int &recv_port, string &subdir, string &filename, string &filepath) {
    int opt;
    while ((opt = getopt(argc, argv, "r:f:")) != -1) {
        switch (opt) {
        case 'r': {
            char *host_port = strtok(optarg, ":");
            char *port_str = strtok(NULL, ":");
            if (!host_port || !port_str) {
                cerr << "Invalid receiver address format. Use -r <host>:<port>" << endl;
                exit(1);
            }
            recv_host = host_port;
            recv_port = atoi(port_str);

            // Validate port number
            if (recv_port <= 0 || recv_port > 65535) {
                cerr << "Invalid port number." << endl;
                exit(1);
            }

            break;
        }
        case 'f': {
            string file_path(optarg); // Convert to std::string
            filepath = file_path;
            size_t last_slash = filepath.find_last_of("/\\"); // Find last slash
            if (last_slash == string::npos) {
                subdir = ".";
                filename = filepath;
            } else {
                subdir = filepath.substr(0, last_slash);
                filename = filepath.substr(last_slash + 1);
            }
            break;
        }
        default:
            cerr << "Usage: sendfile -r <recv_host>:<recv_port> -f <subdir>/<filename>" << endl;
            exit(1);
        }
    }

    if (recv_host.empty() || recv_port == 0 || filename.empty()) {
        cerr << "Missing required arguments. Usage: sendfile -r <recv_host>:<recv_port> -f <subdir>/<filename>" << endl;
        exit(1);
    }
}

// Function to set up the receiver's address
struct sockaddr_in setup_recv_addr(const string &recv_host, int recv_port) {
    struct sockaddr_in recv_addr;

    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(recv_port);

    // DNS resolution
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_DGRAM;  // UDP socket

    if (getaddrinfo(recv_host.c_str(), NULL, &hints, &res) != 0) {
        cerr << "Error resolving host: " << recv_host << endl;
        exit(1);
    }

    recv_addr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);

    return recv_addr;
}

// Function to send a packet
void send_packet(int sockfd, struct sockaddr_in &recv_addr, Packet &packet) {
    sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&recv_addr, sizeof(recv_addr));
}

// Function to print the current window contents
void print_window(const map<int, Packet> &window, ofstream &log_file) {
    log_file << "Current window contents: [ ";
    for (const auto &entry : window) {
        log_file << entry.first << " ";
    }
    log_file << "]" << endl;
}

// Function to print acknowledged packets
void print_acked_packets(const std::set<int> &acked_packets, ofstream &log_file) {
    log_file << "Acknowledged packets: [ ";
    for (int seq_num : acked_packets) {
        log_file << seq_num << " ";
    }
    log_file << "]" << endl;
}

// Function to calculate total number of packets needed to send the file
int calculate_total_packets(ifstream &file) {
    file.seekg(0, ios::end);       // Move to end of file
    int file_size = file.tellg();  // Get file size
    file.seekg(0, ios::beg);       // Reset to beginning

    int total_packets = file_size / MAX_DATA_SIZE;  // Calculate total packets
    if (file_size % MAX_DATA_SIZE != 0) {
        total_packets++;           // Add one if there's a remainder
    }
    return total_packets;
}

// Function to send the file using sliding window protocol
// Handle sending file data using sliding window
void send_file(int sockfd, struct sockaddr_in &recv_addr, ifstream &file, const string &filename, ofstream &log_file) {
    int base = 0;                // Window base
    int next_seq_num = 0;        // Next sequence number to use
    int total_packets = calculate_total_packets(file) + 1; // +1 for filename packet
    log_file << "Total sequence numbers needed: " << total_packets << endl;
    int round = 1;               // Round counter
    char buffer[MAX_DATA_SIZE];  // Buffer to read file data
    map<int, Packet> window;     // Sliding window buffer
    std::set<int> acked_packets; // Set to track acknowledged packets
    map<int, int> retransmission_counts; // Track retransmission counts
    bool file_ended = false;
    bool filename_sent = false;

    auto start_time = high_resolution_clock::now(); // Start time

    // Send file data with sliding window
    while (!file_ended || !window.empty()) {
        auto round_start_time = high_resolution_clock::now();
        log_file << "\n=== Round " << round << " ===" << endl;
        round++;  // Increment round count

        // Fill the window with new packets if space is available
        while (next_seq_num < base + WINDOW_SIZE && (!file_ended || !filename_sent)) {
            if (!filename_sent) {
                // Send filename packet
                Packet filename_packet;
                memset(&filename_packet, 0, sizeof(filename_packet));
                filename_packet.seq_num = next_seq_num;
                filename_packet.flags = PacketType::FILENAME;
                strncpy(filename_packet.data, filename.c_str(), MAX_DATA_SIZE);
                filename_packet.data_length = strlen(filename_packet.data);

                window[next_seq_num] = filename_packet;
                send_packet(sockfd, recv_addr, filename_packet);

                // Initialize retransmission count
                retransmission_counts[next_seq_num] = 0;

                log_file << "[" << duration_cast<milliseconds>(high_resolution_clock::now() - start_time).count() << " ms] ";
                log_file << "Filename packet sent with seq_num " << next_seq_num << " and filename: " << filename_packet.data << endl;

                next_seq_num++;
                filename_sent = true;
            } else {
                // Read data from the file
                file.read(buffer, MAX_DATA_SIZE);
                int bytes_read = file.gcount();
                if (bytes_read == 0) {
                    // End of file reached
                    file_ended = true;
                    break;
                }
                // Create a new packet
                Packet packet;
                memset(&packet, 0, sizeof(packet)); // Clear the packet
                packet.seq_num = next_seq_num;
                packet.data_length = bytes_read;
                memcpy(packet.data, buffer, bytes_read); 
                packet.flags = FILEDATA;

                window[next_seq_num] = packet;
                send_packet(sockfd, recv_addr, packet);

                // Initialize retransmission count
                retransmission_counts[next_seq_num] = 0;

                log_file << "[" << duration_cast<milliseconds>(high_resolution_clock::now() - start_time).count() << " ms] ";
                log_file << "Packet " << next_seq_num << " sent." << endl;
                next_seq_num++;
            }
        }

        // Print the window after sending new packets
        print_window(window, log_file);

        // Wait for ACKs and adjust the window
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_MS * 1000;

        int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);

        if (ready > 0 && FD_ISSET(sockfd, &readfds)) {
            while (true) {
                int ack_num;
                ssize_t n = recvfrom(sockfd, &ack_num, sizeof(ack_num), MSG_DONTWAIT, NULL, NULL);
                if (n <= 0) {
                    // No more data to read
                    break;
                }

                // Timestamp the ACK reception
                log_file << "[" << duration_cast<milliseconds>(high_resolution_clock::now() - start_time).count() << " ms] ";
                log_file << "ACK received for packet " << ack_num << "." << endl;

                // Mark the packet as acknowledged
                acked_packets.insert(ack_num);

                // Move the window base forward based on cumulative ACK
                if (ack_num >= base) {
                    base = ack_num + 1;
                }
            }
        } else {
            // Timeout occurred
            log_file << "[" << duration_cast<milliseconds>(high_resolution_clock::now() - start_time).count() << " ms] ";
            log_file << "Timeout occurred. Retransmitting unacknowledged packets." << endl;
        }

        // Remove acknowledged packets from the window
        for (auto it = window.begin(); it != window.end();) {
            if (it->first < base) {
                it = window.erase(it);
            } else {
                ++it;
            }
        }

        // Print the acknowledged packets
        print_acked_packets(acked_packets, log_file);

        // Retransmit unacknowledged packets within the window
        for (auto &entry : window) {
            int seq_num = entry.first;
            if (seq_num >= base) { // Only retransmit packets that are still within the window
                retransmission_counts[seq_num]++;
                log_file << "[" << duration_cast<milliseconds>(high_resolution_clock::now() - start_time).count() << " ms] ";
                log_file << "Retransmitting packet " << seq_num << " (Retransmission count: " << retransmission_counts[seq_num] << ")" << endl;
                send_packet(sockfd, recv_addr, entry.second);
            }
        }
    }

    // Send end-of-transfer packet
    Packet end_packet;
    memset(&end_packet, 0, sizeof(end_packet)); // Clear the packet
    end_packet.seq_num = next_seq_num;
    end_packet.flags = PacketType::END_OF_TRANSFER;
    send_packet(sockfd, recv_addr, end_packet);
    log_file << "[" << duration_cast<milliseconds>(high_resolution_clock::now() - start_time).count() << " ms] ";
    log_file << "End of file transfer." << endl;

    auto end_time = high_resolution_clock::now(); // End time

    // Calculate transmission duration
    auto duration = duration_cast<milliseconds>(end_time - start_time);
    cout << "File transfer completed in " << duration.count() << " milliseconds." << endl;
}


int main(int argc, char *argv[]) {
    string recv_host, subdir, filename, filepath;
    int recv_port = 0;

    // Open log file
    ofstream log_file("transfer_log.txt", ios::out);

    parse_arguments(argc, argv, recv_host, recv_port, subdir, filename, filepath);

    // Create UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        log_file << "Socket creation failed" << endl;
        perror("Socket creation failed");
        abort();
    }

    struct sockaddr_in recv_addr = setup_recv_addr(recv_host, recv_port);

    ifstream file(filepath, ios::binary);
    if (!file.is_open()) {
        log_file << "Error opening file: " << filepath << endl;
        cerr << "Error opening file: " << filepath << endl;
        close(sockfd);
        return 1;
    }

    send_file(sockfd, recv_addr, file, filename, log_file);

    file.close();
    close(sockfd);
    log_file.close();  // Close log file

    return 0;
}
