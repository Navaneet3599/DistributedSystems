#include <iostream>
#include <cstring>
#include <fstream>
#include <arpa/inet.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"  // Localhost
#define PORT 8080
#define BUFFER_SIZE 1024
#define DESTINATION_FOLDER "./clientImage/"

int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    char buffer[BUFFER_SIZE] = {0};

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket == -1) {
        std::cerr << "Socket creation failed!" << std::endl;
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    while(true) {
        std::cout << "Connected to server. Requesting image...\n";
        std::cout << "To exit the loop, send \"78Be1\"" << std::endl;
        std::string image_name = "";
        std::cin >> image_name;
        sendto(client_socket, image_name.c_str(), image_name.length(), 0, (struct sockaddr*)&server_addr, addr_len);

        if (strncmp(image_name.c_str(), "78Be1", 5) == 0) {
            break;
        }

        int bytes_received = recvfrom(client_socket, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&server_addr, &addr_len);
        std::string new_image_name;
        if (bytes_received > 0) {
            std::cout << "New image name is " << buffer << std::endl;
            new_image_name = buffer;
        }

        bytes_received = recvfrom(client_socket, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&server_addr, &addr_len);
        if (bytes_received > 0) {
            std::cout << "New image size is " << buffer << " bytes" << std::endl;
        }

        long file_size;
        sscanf(buffer, "%ld", &file_size);
        std::cout << "Scanned file size is " << file_size << std::endl;

        /* File receive */
        std::ofstream recvFile(DESTINATION_FOLDER + new_image_name, std::ios::binary | std::ios::ate);
        long total_bytes_received = 0;

        while (total_bytes_received < file_size) {
            bytes_received = recvfrom(client_socket, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&server_addr, &addr_len);
            if (bytes_received <= 0) {
                std::cerr << "Error receiving data or connection closed!" << std::endl;
                break;
            }

            recvFile.write(buffer, bytes_received);
            total_bytes_received += bytes_received;
            std::cout << "Client received " << bytes_received << " bytes of data... Total: " << total_bytes_received << " bytes" << std::endl;
        }

        std::cout << "File has been received" << std::endl;
        system("clear");
    }

    close(client_socket);
    return 0;
}
