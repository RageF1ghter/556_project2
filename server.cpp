// Server side implementation of UDP client-server model (receiving a file)
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fstream>

#define PORT     8080
#define MAXLINE  1024

using namespace std;

void receiveFile(int sockfd, struct sockaddr_in& cliaddr) {
    char buffer[MAXLINE];
    socklen_t len = sizeof(cliaddr);

    // Open the file to save received data
    ofstream outFile("received_file.txt", ios::binary);
    if (!outFile.is_open()) {
        cerr << "Error opening file for writing." << endl;
        exit(EXIT_FAILURE);
    }

    while (true) {
        // Receive chunks from the client
        int n = recvfrom(sockfd, buffer, MAXLINE, 0, (struct sockaddr *) &cliaddr, &len);
        buffer[n] = '\0'; // Null-terminate the received data

        // Check if the received data is the termination message
        if (strcmp(buffer, "END_OF_FILE") == 0) {
            cout << "File transfer completed." << endl;
            break;
        }

        // Write the received data to the file
        outFile.write(buffer, n);
        cout << "Received " << n << " bytes." << endl;
    }

    // Close the file
    outFile.close();
}

// Driver code
int main() {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;

    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
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
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Receive the file from the client
    receiveFile(sockfd, cliaddr);

    close(sockfd);
    return 0;
}
