#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <mutex>

#define PORT 8080
#define BUFFER_SIZE 1024
#define SOURCE_FOLDER "./serverImage/"
#define COPY_FOLDER "./serverRenameImage/"
#define MAX_NO_TRANSFERS 25
#define NUMBER_OF_SERVER_THREADS 5

int noOfRequests = 0;
std::mutex mtx;

void* handle_client(void* socket_desc) {
    int server_socket = *(int*)socket_desc;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recvfrom(server_socket, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client_addr, &addr_len);
        if (bytes_received <= 0) {
            std::cerr << "Error receiving data!" << std::endl;
            continue;
        }

        buffer[bytes_received] = '\0';
        std::string image_name(buffer);
        std::cout << "Client requested: " << image_name << std::endl;

        std::ifstream imageFile(SOURCE_FOLDER + image_name, std::ios::binary);
        if (!imageFile) {
            std::cerr << "Error: Image file not found!" << std::endl;
            continue;
        }

        std::string new_image_name = image_name;
        {
            std::lock_guard<std::mutex> lock(mtx);
            new_image_name.insert(new_image_name.find_last_of('.'), std::to_string(noOfRequests));
        }

        sendto(server_socket, new_image_name.c_str(), new_image_name.length() + 1, 0, (struct sockaddr*)&client_addr, addr_len);

        std::ofstream outputFile(COPY_FOLDER + new_image_name, std::ios::binary);
        outputFile << imageFile.rdbuf();
        imageFile.close();
        outputFile.close();

        std::ifstream fileHandle(COPY_FOLDER + new_image_name, std::ios::binary | std::ios::ate);
        std::streamsize fileSize = fileHandle.tellg();
        fileHandle.seekg(0, std::ios::beg);

        std::string file_size_str = std::to_string(fileSize);
        sendto(server_socket, file_size_str.c_str(), file_size_str.length() + 1, 0, (struct sockaddr*)&client_addr, addr_len);

        long total_bytes_sent = 0;
        while (fileHandle.read(buffer, BUFFER_SIZE)) {
            int bytes_to_send = fileHandle.gcount();
            sendto(server_socket, buffer, bytes_to_send, 0, (struct sockaddr*)&client_addr, addr_len);
            total_bytes_sent += bytes_to_send;
        }

        if (fileHandle.gcount() > 0) {
            sendto(server_socket, buffer, fileHandle.gcount(), 0, (struct sockaddr*)&client_addr, addr_len);
            total_bytes_sent += fileHandle.gcount();
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            noOfRequests += 1;
            std::cout << "****" << noOfRequests << "****" << std::endl;
            if (noOfRequests >= MAX_NO_TRANSFERS) {
                break;
            }
        }
    }
    return nullptr;
}

int main() {
    int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket == -1) {
        std::cerr << "Socket creation failed!" << std::endl;
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed!" << std::endl;
        close(server_socket);  // Close the socket on failure
        return 1;
    }

    std::cout << "UDP Server is running on port " << PORT << std::endl;

    pthread_t threads[NUMBER_OF_SERVER_THREADS];
    for (int i = 0; i < NUMBER_OF_SERVER_THREADS; i++) {

        pthread_create(&threads[i], nullptr, handle_client, &server_socket);
    }

    for (int i = 0; i < NUMBER_OF_SERVER_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }

    close(server_socket);
    return 0;
}