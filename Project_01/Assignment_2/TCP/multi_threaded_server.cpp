#include <iostream>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <filesystem> // For checking file existence

#define PORT 8080
#define BUFFER_SIZE 1024
#define IMAGE_FILE "image.jpg"  // Change this to the correct file name if needed

void handle_client(int client_socket) {
    std::cout << "Client connected: " << client_socket << std::endl;

    char buffer[BUFFER_SIZE] = {0};
    read(client_socket, buffer, BUFFER_SIZE);
    std::cout << "Request received: " << buffer << std::endl;

    // Check if image exists
    if (!std::filesystem::exists(IMAGE_FILE)) {
        std::cerr << "Error: Image file not found! Ensure " << IMAGE_FILE << " is in the correct directory.\n";
        close(client_socket);
        return;
    }

    // Open the image file
    std::ifstream file(IMAGE_FILE, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Failed to open the image file.\n";
        close(client_socket);
        return;
    }

    // Send image data to client
    char file_buffer[BUFFER_SIZE];
    while (file.read(file_buffer, sizeof(file_buffer))) {
        send(client_socket, file_buffer, file.gcount(), 0);
    }

    file.close();
    close(client_socket);
    std::cout << "Image sent to client: " << client_socket << std::endl;
}

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket creation failed\n";
        return -1;
    }

    // Bind socket to port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Binding failed\n";
        return -1;
    }

    // Listen for incoming connections
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listening failed\n";
        return -1;
    }

    std::cout << "Multi-threaded server listening on port " << PORT << std::endl;

    std::vector<std::thread> threads;
    while ((client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen))) {
        threads.emplace_back(handle_client, client_socket);
    }

    // Wait for all threads to finish
    for (auto &t : threads) t.join();
    
    close(server_fd);
    return 0;
}

