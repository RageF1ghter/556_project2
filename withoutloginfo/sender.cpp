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
#include <fcntl.h>      // For fcntl()
#include <sys/select.h> // For select()
#include <unordered_map>
#include <sys/time.h>  // For gettimeofday()

using namespace std;

#define PORT           8080
#define MAXLINE        1024
#define WINDOW_SIZE    10
#define TIMEOUT_MS     100
#define PRINT          false

/* pckect structure */
struct Packet {
    uint16_t seq_num;    // Sequence number
    uint16_t ack_num;    // Acknowledgment number
    uint16_t checksum;   // For error detection
    uint16_t data_length;  // Length of data in the packet
    uint64_t timestamp_ms; // Time when the packet was sent, in milliseconds    
    char data[MAXLINE];  // Data payload
};

// Function to get current time and convert
uint64_t getCurrentTimeInMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);  
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

// caculate time differ
long get_time_diff_ms(const struct timeval& start, const struct timeval& end) {
    long seconds = end.tv_sec - start.tv_sec;
    long microseconds = end.tv_usec - start.tv_usec;
    return seconds * 1000 + microseconds / 1000;
}

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

/**
 * send file function send the file directroy, file name, and file content
 * 
 */
void sendFile(const char* filePath, int sockfd, struct sockaddr_in& servaddr) {  
    // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/    
    // // 打开日志文件
    // ofstream logFile("transfer_log.txt", ios::out);
    // if (!logFile.is_open()) {
    //     cerr << "Error opening log file!" << endl;
    //     return;
    // }
    // /*添加功能-------------------------------------------------------------------------------------------------------------------------------------------------*/
    
    // record the start time of send file
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    string filename = filePath;
    string filedirectory;
    /*get the file directory and file name-------------------------------------------------------------------------------------------------------------------------------------------*/
    size_t pos = filename.find_last_of("/\\");
    if (pos != string::npos) {
        filedirectory = filename.substr(0, pos + 1);
        filename = filename.substr(pos + 1);
        cout << filedirectory << endl;
        cout << filename << endl;
    }
    /*end of get file directory and filename--------------------------------------------------------------------------------------------------------------------------------------------*/

    // Open the file
    ifstream file(filePath, ios::binary);
    if (!file.is_open()) {
        cerr << "Error opening file: " << filePath << endl;
        exit(EXIT_FAILURE);
    }

    /*Calculate total number of packets to send-------------------------------------------------------------------------------------------------------------------------------------------------*/
    file.seekg(0, ios::end);    // Move to the end of the file
    streamsize fileSize = file.tellg();    // Get file size
    file.seekg(0, ios::beg);    // Move back to the beginning of the file

    int total_packets = static_cast<int>(fileSize / MAXLINE) + ((fileSize % MAXLINE) != 0 ? 1 : 0); // Calculate total packets
    cout << "Total number of packets to send: " << total_packets << endl;
    // logFile << "Total number of packets to send: " << total_packets << endl;  // Log this information
    /*end of Calculate total number of packets to send-------------------------------------------------------------------------------------------------------------------------------------------------*/


    Packet window[WINDOW_SIZE];
    memset(window, 0, sizeof(window)); // Initialize window packets
    char buffer[MAXLINE];
    int head = 0;   
    int tail = 0;

    unordered_map<int, bool> isAcked;   // seq, bool
    socklen_t len = sizeof(servaddr);
    bool doneReading = false;
    struct timeval now;
  
    // Set socket to non-blocking mode
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    /* Send the file directory packet-------------------------------------------------------------------------------------------------------------------------------------------------*/
    Packet& dir_packet = window[tail % WINDOW_SIZE];
    // initialize packet
    memset(&dir_packet, 0, sizeof(Packet));
    dir_packet.seq_num = tail;
    dir_packet.data_length = filedirectory.length();
    // void* memcpy(void* dest, const void* src, size_t n);
    // copy name data to the dest
    memcpy(dir_packet.data, filedirectory.c_str(), dir_packet.data_length);
    dir_packet.checksum = calculateChecksum(dir_packet);
    // Encode to network byte order
    encode(dir_packet);
    
    ssize_t bytes_sentdir = sendto(sockfd, &dir_packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
    if (bytes_sentdir == -1) {
        perror("sendto directory failed");
        return;
    }

    tail++;
    /* end of Sending the file directory packet-------------------------------------------------------------------------------------------------------------------------------------------------*/


    /* Send the filename packet-------------------------------------------------------------------------------------------------------------------------------------------------*/
    Packet& filename_packet = window[tail % WINDOW_SIZE];
    memset(&filename_packet, 0, sizeof(Packet));
    filename_packet.seq_num = tail;
    filename_packet.ack_num = 0;
    filename_packet.data_length = filename.length();
    memcpy(filename_packet.data, filename.c_str(), filename_packet.data_length);
    filename_packet.checksum = calculateChecksum(filename_packet);
    // filename_packet.timestamp_ms = getCurrentTimeInMs();
    encode(filename_packet);

    // Send the filename packet
    ssize_t bytes_sentfilename = sendto(sockfd, &filename_packet, sizeof(Packet), 0,
                                (const struct sockaddr*)&servaddr, len);
    if (bytes_sentfilename == -1) {
        perror("sendto filename failed");
    } else {
        //only when received the packet can be true
        isAcked[filename_packet.seq_num] = false;
        // cout << "Sent filename packet seq_num: " << ntohs(filename_packet.seq_num) << endl;
    }
    tail++;
    /*end of Send the filename packet-------------------------------------------------------------------------------------------------------------------------------------------------*/


    while (!doneReading || head < tail) {
        // // add window info to log------------------
        // logFile << "Current Window (seq nums): ";
        // for (int i = 0; i < WINDOW_SIZE; ++i) {
        //     int seq_num = head + i;
        //     logFile << seq_num << " ";
        // }
        // logFile << endl;


        /** send packets within the window size */
        while (!doneReading && tail < head + WINDOW_SIZE) {
            // Read a chunk of the file into the buffer
            file.read(buffer, MAXLINE);
            // gcount get the actual read size
            streamsize bytesRead = file.gcount();
            // when bytesread > 0 means get data
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
                packet.timestamp_ms = getCurrentTimeInMs();
                // logFile << "Sending Packet (seq num): " << packet.seq_num << endl;
                encode(packet);

                // Send the packet
                ssize_t bytes_sent = sendto(sockfd, &packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
                if (bytes_sent == -1) {
                    perror("sendto failed");
                    // logFile << "Error sending packet (seq num): " << packet.seq_num << endl;
                } else {
                    isAcked[ntohs(packet.seq_num)] = false;
                    cout << "Sent packet sequence number(tail): " << tail <<  endl;
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
        FD_ZERO(&readfds); /* clear everything */
        FD_SET(sockfd, &readfds); /* put the listening socket in */

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100 ms timeout时间，重传时间

        int retval = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        if (retval == -1) {
            perror("select()");
        } else if (retval == 0) {
            // logFile << "Timeout, checking for retransmissions." << endl;
            // Timeout occurred, check for packet timeouts
            cout << "recv timeout, check the sent" << endl;
        } else {
            if (FD_ISSET(sockfd, &readfds)){
                // Data is available to read
                Packet ack_packet;
                ssize_t received_bytes = recvfrom(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)&servaddr, &len);
                if (received_bytes > 0) {
                    // Convert packet to os order
                    decode(ack_packet);
                    // cout<<ack_packet.seq_num<<endl;
                    // check if broken
                    uint16_t calc_checksum = calculateChecksum(ack_packet);
                    if (ack_packet.checksum == calc_checksum) {
                        // logFile << "Received ACK for packet (seq num): " << ack_packet.seq_num << endl;
                        if (ack_packet.ack_num == 1) {
                            // Duplicate Ack packet, ignore
                            if(isAcked[ack_packet.seq_num] == true){
                                cout<<"Duplicate Ack, seq: "<<ack_packet.seq_num<<endl;
                                // logFile << "Duplicate ACK (seq num): " << ack_packet.seq_num << endl;
                            }
                            // Ack packet in the window, update
                            else{
                                isAcked[ack_packet.seq_num] = true;
                                cout << "Received ACK for packet " << ack_packet.seq_num << endl;
                                // logFile << "Acknowledged packet (seq num): " << ack_packet.seq_num << endl;
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
                            // logFile << "Packet corrupted, retransmitting seq num: " << ack_packet.seq_num << endl;
                            // Packet corrupted, retransmit
                            cout << "Packet corrupted, retransmit tail: " << ack_packet.seq_num << endl;
                            int seq_index = ack_packet.seq_num % WINDOW_SIZE;
                            Packet& packet_to_resend = window[seq_index];
                            packet_to_resend.timestamp_ms = getCurrentTimeInMs();    
                            ssize_t bytes_sent = sendto(sockfd, &packet_to_resend, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
                            if (bytes_sent == -1) {
                                perror("sendto failed");
                                // logFile << "Failed to resend packet (seq num): " << ack_packet.seq_num << endl;
                            } else {
                                cout << "Resent packet tail: " << ack_packet.seq_num << endl;
                                // logFile << "Resent packet (seq num): " << ack_packet.seq_num << endl;
                            }
                        } else{
                            cout<<"current window" << head <<", "<<tail-1;
                            cout << " ack outside the window, seq: "<< ack_packet.seq_num<<endl;
                            // logFile << "Received corrupted ACK packet (seq num): " << ack_packet.seq_num << endl;
                        }
                    } else {
                        cout << "Received corrupted ACK packet" << endl;
                    }
                } else {
                    perror("recvfrom failed");
                }
            } 
        }

    
        // check if in a certain time the packet whether be ack or not       
        gettimeofday(&now, NULL);
        for (int i = head; i < tail; i++) {
            Packet& packet = window[i % WINDOW_SIZE];
            if (packet.ack_num != 1) {
                
                if (getCurrentTimeInMs() - packet.timestamp_ms >= TIMEOUT_MS){
                    packet.timestamp_ms = getCurrentTimeInMs();
                    // Resend packet
                    ssize_t bytes_sent = sendto(sockfd, &packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
                    if (bytes_sent == -1) {
                        perror("sendto failed");
                        // logFile << "Timeout, retransmitting packet (seq num): " << packet.seq_num << endl;
                    } else {
                        cout << "Timeout, retransmitted packet tail: " << ntohs(packet.seq_num) << endl;
                    }
                }
            }
        }
    }


    /* Send an end-of-file packet-------------------------------------------------------------------------------------------------------------------------------------------------*/
    Packet eof_packet;
    memset(&eof_packet, 0, sizeof(Packet));
    eof_packet.seq_num = tail;
    eof_packet.ack_num = 0;
    eof_packet.data_length = 0; // No data
    eof_packet.checksum = calculateChecksum(eof_packet);

    // Send EOF packet until it's acknowledged
    bool eof_acknowledged = false;

    // convert to net format
    encode(eof_packet);

    while (!eof_acknowledged) {
        ssize_t bytes_sent = sendto(sockfd, &eof_packet, sizeof(Packet), 0, (const struct sockaddr*)&servaddr, len);
        if (bytes_sent == -1) {
            perror("sendto failed");
        } else {
            // logFile << "Sent EOF packet" << endl;
            cout << "Sent EOF packet" << endl;
        }

        // Wait for ACK
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100 ms

        int retval = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        if (retval == -1) {
            perror("select");
        } else if (retval == 0) {
            cout<<"eof timeout, check the sent"<<endl;
        } else {
            if (FD_ISSET(sockfd, &readfds)) {
                Packet ack_packet;
                ssize_t received_bytes = recvfrom(sockfd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)&servaddr, &len);
                if (received_bytes > 0) {
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
    /* End of Send an end-of-file packet-------------------------------------------------------------------------------------------------------------------------------------------------*/\

    // record end time
    gettimeofday(&end_time, NULL);
    long transfer_time_ms = get_time_diff_ms(start_time, end_time);
    cout << "File transfer completed successfully." << endl;
    cout << "Total transfer time: " << transfer_time_ms << " ms" << endl;
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
