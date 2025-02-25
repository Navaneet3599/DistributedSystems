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

#define SERVER_IP "127.0.0.1"  // Localhost
#define PORT 8080
#define BUFFER_SIZE 1024
#define DESTINATION_FOLDER "./clientImage/"
#define MAX_NO_TRANSFERS 25
#define NUMBER_OF_CLIENT_THREADS 1

std::array<std::string, 20> image_list = {
    "brook.jpeg", "dwarfLuffy.jpeg", "emiya.jpeg", "escanor.jpeg", "gear5.jpeg", 
    "gojo.jpeg", "hashirama.jpeg", "light.jpeg", "L.jpeg", "madara.jpeg", 
    "meliodas.jpeg", "minato.jpeg", "nagi.jpeg", "naruto.jpeg", "saber.jpeg", 
    "saitama.jpeg", "sanji.jpeg", "sasuke.jpeg", "sung.jpeg", "zoro.jpeg"
};

int noOfRequests = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* handle_server(void* created_socket) {
    int client_socket = *(int*)created_socket;
    delete (int*)created_socket;  // Free the dynamically allocated memory
    char buffer[BUFFER_SIZE] = {0};
    std::srand(std::time(0));
    int random_number;

    while (true) {
        // Randomly select an image
        random_number = std::rand() % image_list.size();
        std::string image_name = image_list[random_number];
        send(client_socket, image_name.c_str(), image_name.length() + 1, 0);  // Send null-terminated string

        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        std::string new_image_name;
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';  // Ensure null-termination
            std::cout << "New image name: " << buffer << std::endl;
            new_image_name = buffer;
        }

        // Receive the file size
        bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
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
            bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if (bytes_received <= 0) {
                std::cerr << "Error receiving data or connection closed!" << std::endl;
                break;
            }
            recvFile.write(buffer, bytes_received);
            total_bytes_received += bytes_received;
        }

        std::cout << "File received successfully!" << std::endl;

        // Check transfer limit
        pthread_mutex_lock(&mutex);
        noOfRequests += 1;
        std::cout << "***" << noOfRequests << "****" << std::endl;
        if (noOfRequests >= MAX_NO_TRANSFERS) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);
        sleep(1);
    }

    close(client_socket);
    return nullptr;
}

int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    pthread_t threads[NUMBER_OF_CLIENT_THREADS];

    for (int i = 0; i < NUMBER_OF_CLIENT_THREADS; i++) {
        client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket == -1) {
            std::cerr << "Socket creation failed!" << std::endl;
            return 1;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);
        inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

        if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connection failed!" << std::endl;
            return 1;
        }

        int* accepted_socket = new int(client_socket);
        (void)pthread_create(&threads[i], nullptr, handle_server, accepted_socket);
    }

    sleep(2);
    for (int i = 0; i < NUMBER_OF_CLIENT_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }
    std::cout << "Main thread end..." << std::endl;
    return 0;
}