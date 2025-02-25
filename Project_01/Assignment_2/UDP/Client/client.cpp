#include <iostream>
#include <cstring>
#include <array>
#include <fstream>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <mutex>

#define SERVER_IP "127.0.0.1"  // Localhost
#define PORT 8080
#define BUFFER_SIZE 1024
#define DESTINATION_FOLDER "./clientImage/"
#define MAX_NO_TRANSFERS 25
#define NUMBER_OF_CLIENT_THREADS 5

typedef struct
{
    sockaddr serverSocketAddress;
    int clientSocket;
}ST_ThreadArg;


int noOfRequests = 0;
std::array<std::string, 20> image_list = {"brook.jpeg", "dwarfLuffy.jpeg", "emiya.jpeg", "escanor.jpeg", "gear5.jpeg", "gojo.jpeg", "hashirama.jpeg", "light.jpeg", "L.jpeg", "madara.jpeg", "meliodas.jpeg", "minato.jpeg", "nagi.jpeg", "naruto.jpeg", "saber.jpeg", "saitama.jpeg", "sanji.jpeg", "sasuke.jpeg", "sung.jpeg", "zoro.jpeg"};
std::mutex mtx;

void* handle_server(void* stp_ThreadArg) {
    int client_socket = (*((ST_ThreadArg*)stp_ThreadArg)).clientSocket;
    struct sockaddr server_addr = (*((ST_ThreadArg*)stp_ThreadArg)).serverSocketAddress;
    socklen_t addr_len = sizeof(server_addr);

    char buffer[BUFFER_SIZE] = {0};

    while (true) {
        // Randomly select an image
        int random_number = std::rand() % 20;
        std::string image_name = image_list[random_number];
        sleep(3);
        // Send image name to server
        sendto(client_socket, image_name.c_str(), image_name.length() + 1, 0, &server_addr, addr_len);
        if (strncmp(image_name.c_str(), "78Be1", 5) == 0) {
            break;
        }

        // Receive the renamed image name
        int bytes_received = recvfrom(client_socket, buffer, BUFFER_SIZE, 0, &server_addr, &addr_len);
        std::string new_image_name;
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';  // Ensure null-termination
            std::cout << "New image name: " << buffer << std::endl;
            new_image_name = buffer;
        }

        // Receive the file size
        bytes_received = recvfrom(client_socket, buffer, BUFFER_SIZE, 0, &server_addr, &addr_len);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';  // Null-terminate
            std::cout << "New image size: " << buffer << " bytes" << std::endl;
        }

        long file_size;
        sscanf(buffer, "%ld", &file_size);
        std::cout << "Received file size: " << file_size << std::endl;

        // File receive
        std::ofstream recvFile(DESTINATION_FOLDER + new_image_name, std::ios::binary);
        long total_bytes_received = 0;

        while (total_bytes_received < file_size) {
            bytes_received = recvfrom(client_socket, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&server_addr, &addr_len);
            if (bytes_received <= 0) {
                std::cerr << "Error receiving data!" << std::endl;
                break;
            }
            recvFile.write(buffer, bytes_received);
            total_bytes_received += bytes_received;
        }

        std::cout << "File received successfully!" << std::endl;

        // Check transfer limit
        {
            std::lock_guard<std::mutex> lock(mtx);
            noOfRequests += 1;
            std::cout << "****" << noOfRequests << "****" << std::endl;
            if (noOfRequests >= MAX_NO_TRANSFERS) {
                break;
            }
        }
    }

    close(client_socket);
    return nullptr;
}

int main() {
    std::srand(std::time(0));  // Initialize the random seed

    pthread_t threads[NUMBER_OF_CLIENT_THREADS];

    for (int i = 0; i < NUMBER_OF_CLIENT_THREADS; i++) {
        int client_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (client_socket == -1) {
            std::cerr << "Socket creation failed!" << std::endl;
            return 1;
        }

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);
        inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

        ST_ThreadArg st_ThreadArg = {*(sockaddr*)(&server_addr), client_socket};

        int* accepted_socket = new int(client_socket);
        (void)pthread_create(&threads[i], nullptr, handle_server, (void*)(&st_ThreadArg));
    }

    sleep(2);
    for (int i = 0; i < NUMBER_OF_CLIENT_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    std::cout << "Main thread end..." << std::endl;
    return 0;
}