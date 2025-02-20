#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 4096

using namespace std;

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed!" << endl;
        return -1;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed! Error: " << WSAGetLastError() << endl;
        WSACleanup();
        return -1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);


    while (true)
    {
        system("cls");
        cout << "Server::Client can request for the following images:\n\tAppleLogo.png\n\tLinuxLogo.png\n\tNaruto.png\n\tNikeLogo.png\n\tWindowsLogo.png\nIf you wish to terminate the connection, then enter \"78Be1\"(case sensitive).\n";

        // Send request to server
        char requestMsg[40] = "";
        cin.getline(requestMsg, 40);
        (void)sendto(clientSocket, requestMsg, strlen(requestMsg), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));

        if (strcmp(requestMsg, "78Be1") == 0)
        {
            cout << "Client initiated disconnect request...\n";
            break;
        }

        ofstream file("received_image.png", ios::binary);
        if (!file.is_open()) {
            cerr << "Error opening output file!" << endl;
            closesocket(clientSocket);
            WSACleanup();
            return -1;
        }

        char buffer[BUFFER_SIZE];
        int serverLen = sizeof(serverAddr);
        int bytesReceived;
        int fileDescriptor = *((int*)(&file));

        cout << "Receiving image..." << endl;
        while (true) {
            bytesReceived = recvfrom(clientSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&serverAddr, &serverLen);
            if (bytesReceived == SOCKET_ERROR) {
                cerr << "recvfrom() failed: " << WSAGetLastError() << endl;
                break;
            }
            if (bytesReceived == 0) continue; // Ignore empty packets

            // Check for termination signal
            if (strncmp(buffer, "DONE", 4) == 0) {
                cout << "File transfer complete!" << endl;
                break;
            }

            file.write(buffer, bytesReceived);
            cout << "Received " << bytesReceived << " bytes" << endl;
        }
        file.close();

        string fileName(requestMsg);
        string fileNameWithoutExt = fileName.substr(0, (int)(fileName.find(".")));
        string newFileName = fileNameWithoutExt + to_string(fileDescriptor) + ".png";
        (void)rename("received_image.png", newFileName.c_str());
    }
    

    

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
