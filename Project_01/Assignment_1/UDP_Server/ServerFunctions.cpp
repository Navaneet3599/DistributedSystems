#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string.h>
#include <string>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_PORT 8080
#define BUFFER_SIZE 4096

using namespace std;

void sendFile(const char* filename, SOCKET& serverSocket, sockaddr_in& clientAddr) {
    ifstream file(filename, ios::binary | ios::ate);
    if (!file) {
        cerr << "Error opening file: " << filename << endl;
        return;
    }

    streamsize fileSize = file.tellg();
    file.seekg(0, ios::beg);

    char buffer[BUFFER_SIZE];
    int clientLen = sizeof(clientAddr);

    cout << "Sending file of size: " << fileSize << " bytes" << endl;

    while (!file.eof()) {
        file.read(buffer, BUFFER_SIZE);
        int usedBuffer = file.gcount();
        int sentBytes = sendto(serverSocket, buffer, usedBuffer, 0, (sockaddr*)&clientAddr, clientLen);
        if (sentBytes == SOCKET_ERROR) {
            cerr << "sendto() failed: " << WSAGetLastError() << endl;
            break;
        }
        cout << "Sent " << sentBytes << " bytes" << endl;
        Sleep(1); // Small delay to prevent UDP packet loss
    }

    // Send termination signal
    const char* doneMsg = "DONE";
    sendto(serverSocket, doneMsg, strlen(doneMsg), 0, (sockaddr*)&clientAddr, clientLen);
    cout << "File transfer completed!" << endl;

    file.close();
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed!" << endl;
        return -1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed! Error: " << WSAGetLastError() << endl;
        WSACleanup();
        return -1;
    }

    sockaddr_in serverAddr{}, clientAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Bind failed! Error: " << WSAGetLastError() << endl;
        closesocket(serverSocket);
        WSACleanup();
        return -1;
    }

    while (true)
    {
        system("cls");
        cout << "UDP Server is running, waiting for client request..." << endl;

        char buffer[BUFFER_SIZE];
        int clientLen = sizeof(sockaddr);
        vector<char> fileName(40);
        recvfrom(serverSocket, fileName.data(), BUFFER_SIZE, 0, (sockaddr*)&clientAddr, &clientLen);

        if (strcmp(fileName.data(), "78Be1") == 0)
        {
            cout << "Client initiated disconnect request...\n";
            break;
        }
        else
        {
            cout << fileName.data() << endl;
            cout << "Client connected. Sending image..." << endl;
            sendFile(fileName.data(), serverSocket, clientAddr);
        }
        system("pause");
    }

    

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
