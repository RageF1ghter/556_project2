// Client side implementation of UDP client-server model (sending a file)
#include <iostream>     // For cout, cerr
#include <cstdlib>      // For exit() and EXIT_FAILURE
#include <unistd.h>     // For close() and usleep()
#include <cstring>      // For memset() and strlen()
#include <sys/types.h>  // For socket types
#include <sys/socket.h> // For socket functions
#include <arpa/inet.h>  // For inet functions
#include <netinet/in.h> // For sockaddr_in
#include <fstream>      // For file handling

#define PORT     8080 
#define MAXLINE  1024 

using namespace std; // Use standard namespace to avoid prefixing std::

// Function to send a file over UDP
void sendFile(const char* filePath, int sockfd, struct sockaddr_in& servaddr) {
    // Open the file
    ifstream file(filePath, ios::binary);
    if (!file.is_open()) {
        cerr << "Error opening file: " << filePath << endl;
        exit(EXIT_FAILURE);
    }

    char buffer[MAXLINE];
    socklen_t len = sizeof(servaddr);
    while (!file.eof()) {
        // Read a chunk of the file into the buffer
        file.read(buffer, MAXLINE);
        streamsize bytesRead = file.gcount();

        // Send the chunk to the server
        sendto(sockfd, buffer, bytesRead, 0, (const struct sockaddr *) &servaddr, len);
        cout << "Sent " << bytesRead << " bytes." << endl;

        // Add a small delay to prevent overwhelming the network (optional)
        usleep(1000); // 1ms delay
    }

    // Send a termination message (optional, but helps server know the transfer is done)
    const char* endMessage = "END_OF_FILE";
    sendto(sockfd, endMessage, strlen(endMessage), 0, (const struct sockaddr *) &servaddr, len);

    cout << "File transfer completed." << endl;
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
    servaddr.sin_addr.s_addr = INADDR_ANY; 
    
    // Send file
    const char* filePath = "file.txt"; 
    sendFile(filePath, sockfd, servaddr);

    close(sockfd); 
    return 0; 
}
