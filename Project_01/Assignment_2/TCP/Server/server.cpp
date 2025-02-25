#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>      // For getcwd()
#include <arpa/inet.h>   // For socket functions
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define SOURCE_FOLDER "./serverImage/"
#define COPY_FOLDER "./serverRenameImage/"
#define MAX_NO_TRANSFERS 25
#define NUMBER_OF_SERVER_THREADS 5

int noOfRequests = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* handle_client(void* accepted_socket) {
    int client_socket = *(int*)accepted_socket;
    
    while (true) {
        char buffer[BUFFER_SIZE] = {0};
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            std::cerr << "Client disconnected or error!" << std::endl;
            break;
        }

        // Ensure null-termination of received string
        buffer[bytes_received] = '\0';
        std::string image_name = std::string(buffer);
        std::cout << "Client requested for: " << image_name << std::endl;

        // Check if the file exists
        std::ifstream imageFile(SOURCE_FOLDER + image_name, std::ios::binary);
        if (!imageFile) {
            std::cerr << "Error: Image file '" << image_name << "' not found!" << std::endl;
            close(client_socket);
            break;
        } else {
            // Generate unique renaming for the image based on timestamp and client info
            std::string new_image_name = image_name;
            pthread_mutex_lock(&mutex);
            new_image_name.insert(new_image_name.find_last_of('.'), "" + std::to_string(client_socket) + "" + std::to_string(noOfRequests));
            pthread_mutex_unlock(&mutex);

            // Sending the renamed image name
            std::cout << "Renamed image name: " << new_image_name << std::endl;
            send(client_socket, new_image_name.c_str(), new_image_name.length() + 1, 0);

            // Renaming and saving a copy
            std::ofstream outputFile(COPY_FOLDER + new_image_name, std::ios::binary);
            outputFile << imageFile.rdbuf();
            imageFile.close();
            outputFile.close();
            std::cout << "File copy completed" << std::endl;

            // Sending file size and file data
            std::ifstream fileHandle(COPY_FOLDER + new_image_name, std::ios::binary | std::ios::ate);
            std::streamsize fileSize = fileHandle.tellg();
            std::cout << "Renamed image size: " << fileSize << std::endl;
            fileHandle.seekg(0, std::ios::beg);
            send(client_socket, std::to_string(fileSize).c_str(), std::to_string(fileSize).length() + 1, 0);

            sleep(0.005);
            long total_bytes_sent = 0;
            while (fileHandle.read(buffer, BUFFER_SIZE)) {
                long bytes_to_send = fileHandle.gcount();
                int bytes_sent = send(client_socket, buffer, bytes_to_send, 0);
                if (bytes_sent == -1) {
                    std::cerr << "Error in sending data!" << std::endl;
                    break;
                }
                total_bytes_sent += bytes_sent;
            }

            if (fileHandle.gcount() > 0) {
                int bytes_sent = send(client_socket, buffer, fileHandle.gcount(), 0);
                if (bytes_sent == -1) {
                    std::cerr << "Error in sending data!" << std::endl;
                }
                total_bytes_sent += bytes_sent;
            }

            pthread_mutex_lock(&mutex);
            noOfRequests += 1;
            std::cout << "***" << noOfRequests << "****" << std::endl;
            pthread_mutex_unlock(&mutex);

            std::cout << "File transfer is complete" << std::endl;
        }

        // Check transfer limit
        pthread_mutex_lock(&mutex);
        if (noOfRequests >= MAX_NO_TRANSFERS) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);
    }

    close(client_socket);
    return nullptr;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        std::cerr << "Socket creation failed!" << std::endl;
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed!" << std::endl;
        return 1;
    }

    if (listen(server_socket, NUMBER_OF_SERVER_THREADS) < 0) {
        std::cerr << "Listen failed!" << std::endl;
        return 1;
    }

    std::cout << "Server is listening on port " << PORT << std::endl;

    while ((client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len))) {
        std::cout << "Client connected!" << std::endl;

        pthread_t th;
        int* accepted_socket = new int(client_socket);
        (void)pthread_create(&th, nullptr, handle_client, accepted_socket);
        pthread_detach(th);

        pthread_mutex_lock(&mutex);
        if (noOfRequests >= MAX_NO_TRANSFERS) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);
    }

    std::cout << "Total requests handled: " << noOfRequests << std::endl;

    sleep(2);

    close(server_socket);
    std::cout << "Main thread end..." << std::endl;
    return 0;
}