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

#define PORT 8080
#define BUFFER_SIZE 1024
#define SOURCE_FOLDER "./serverImage/"
#define COPY_FOLDER "./serverRenameImage/"

void handle_client(int client_socket) {
    while(true)
    {
        char buffer[BUFFER_SIZE] = {0};
        std::cout << "Waiting for client request..." << std::endl;
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received > 0) {
            std::cout << buffer << std::endl;
            if(strncmp(buffer, "78Be1", 5) == 0)
                break;
                
            std::string image_name = buffer; // Default image file
            std::cout << "Client requested for " + image_name << std::endl;

            // Open the image file
            std::ifstream imageFile(SOURCE_FOLDER + image_name, std::ios::binary);
            if (!imageFile) {
                std::cerr << "Error: Image file '" << image_name << "' not found!" << std::endl;
                close(client_socket);
                return;
            }
            else{
                int fileDescriptor = open((SOURCE_FOLDER + image_name).c_str(), O_RDONLY);
                std::string new_image_name = "";
                bool extFound = false;
                for(int i = image_name.length(); i >= 0; i--)
                {
                    if((image_name[i] == '.')&&(extFound == false))
                    {
                        extFound = true;
                        new_image_name = "_" + std::to_string(fileDescriptor) + "." + new_image_name;
                    }
                    else
                    {
                        new_image_name = image_name[i] + new_image_name;
                    }
                }
                close(fileDescriptor);

                /*Sending image name*/
                std::cout << "Renamed image name is " + new_image_name << std::endl;
                send(client_socket, new_image_name.c_str(), new_image_name.length()+1, 0);

                /*Renaming file and saving a copy*/
                std::ofstream outputFile(COPY_FOLDER + new_image_name, std::ios::binary);
                outputFile << imageFile.rdbuf();
                imageFile.close();
                outputFile.close();
                std::cout << "File copy completed" << std::endl;

                /*Sending file size and file*/
                std::ifstream fileHandle(COPY_FOLDER + new_image_name, std::ios::binary|std::ios::ate);
                std::streamsize fileSize = fileHandle.tellg();
                std::cout << "Renamed image size is " + std::to_string(fileSize) << std::endl;
                fileHandle.seekg(0, std::ios::beg);
                send(client_socket, (std::to_string(fileSize)).c_str(), (std::to_string(fileSize)).length()+1, 0);

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
                    std::cout << "Server sent " << bytes_sent << " bytes of data..." << std::endl;
                }

                if (fileHandle.gcount() > 0) {
                    int bytes_sent = send(client_socket, buffer, fileHandle.gcount(), 0);
                    if (bytes_sent == -1) {
                        std::cerr << "Error in sending data!" << std::endl;
                    }
                    total_bytes_sent += bytes_sent;
                    std::cout << "Server sent " << bytes_sent << " bytes of data..." << std::endl;
                }

                std::cout << "File transfer is complete" << std::endl;
            }
        }
        system("clear");
    }
    close(client_socket);
}

int main() {
    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation failed!" << std::endl;
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed!" << std::endl;
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        std::cerr << "Listen failed!" << std::endl;
        return 1;
    }

    std::cout << "Server is listening on port " << PORT << std::endl;

    while ((client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len))) {
        std::cout << "Client connected!" << std::endl;
        handle_client(client_socket);
    }

    close(server_fd);
    return 0;
}

