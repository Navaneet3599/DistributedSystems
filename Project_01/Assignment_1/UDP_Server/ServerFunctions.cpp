/*My server*/
/*This source code was written by Navaneet Rao Dhage, as a part of Distributed Systems course work.*/

#ifndef SERVERFUNCTIONS_CPP
#define SERVERFUNCTIONS_CPP


/*Includes*/
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <regex>
#include <vector>
#include <fstream>

#pragma comment(lib, "Ws2_32.lib")


/*Defines*/
using namespace std;
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define BUFFER_SIZE 1024
#define TOOK_TOO_LONG_TO_RESPOND 0
#define DISCONNECT_REQUEST 1
#define FILE_TRANSFER_REQUEST 2
#define CONVERSATION_RESTART 3


/*Type defs*/
typedef sockaddr ST_sockaddr;
typedef sockaddr_in ST_sockaddr_in;

/*Variable declarations*/
char imageBuffer[BUFFER_SIZE];

/*Function Declarations*/
void server_errorDisplay();
void server_loadDLL();
SOCKET server_creatingSocket(int socketType, int commProtocol);
bool server_bindSocket(SOCKET clientSocket, ST_sockaddr* clientSocketAddress, int addressLength);
void server_sendMessage_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* clientMessage);
void server_receiveMessage_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* clientMessage);
void server_sendData_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char> buffer, int noOfBytes);
void server_receiveData_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* buffer);
string fileRename(string fileName, string prev, string curr);

/*Function Definitions*/
/*******Client Functions*******/
int main()
{
    int bytesReceived;
    vector<char> clientMessage(200);
    vector<char> serverMessage(200);
    vector<char> imageBuffer(BUFFER_SIZE);
    char serverIPAddress[15] = "127.0.0.1";
    bool imageTransfer = false;
    ST_sockaddr_in serverSocketAddress = { 0 };
    string temp;
    regex pattern(".png");

    /*Loading winsock library*/
    server_loadDLL();

    /*Creating client socket*/
    //SOCKET serverSocket = server_creatingSocket(SOCK_STREAM, IPPROTO_TCP);
    SOCKET serverSocket = server_creatingSocket(SOCK_DGRAM, IPPROTO_UDP);

    /*Specifying server socket address*/
    serverSocketAddress.sin_family = AF_INET;
    serverSocketAddress.sin_port = htons(55555);
    if (inet_pton(AF_INET, serverIPAddress, &serverSocketAddress.sin_addr) == 1) cout << "----------Server address specification ---->VALID\n";
    else if (inet_pton(AF_INET, serverIPAddress, &serverSocketAddress.sin_addr) == 0) cout << "----------Server address specification ---->INVALID\n";
    else if (inet_pton(AF_INET, serverIPAddress, &serverSocketAddress.sin_addr) == -1) cout << "----------Server address specification ---->OTHER ERRORS\n" << WSAGetLastError() << "\n";

    if (server_bindSocket(serverSocket, (ST_sockaddr*)(&serverSocketAddress), sizeof(ST_sockaddr)))
    {
        do
        {
            //system("cls");
            while (true)
            {
                cout << clientMessage.data();
                if (strcmp(clientMessage.data(), "Hello") == 0)
                    break;
                serverMessage.assign("Hello", "Hello" + 5);
                Sleep(5);
                server_receiveMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &clientMessage);
                Sleep(5);
                server_sendMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
                Sleep(5);
                cout << serverMessage.data();
            }

            /*Sending messages to client*/
            Sleep(10);
            temp = "Server::Client can request for the following images:\n";
            serverMessage.assign(temp.begin(), temp.end());
            server_sendMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);

            Sleep(10);
            temp = "\tAppleLogo.png\n\tLinuxLogo.png\n\tNaruto.png\n\tNikeLogo.png\n\tWindowsLogo.png\n";
            serverMessage.assign(temp.begin(), temp.end());
            server_sendMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);

            Sleep(10);
            temp = "If you wish to terminate the connection, then enter \"78Be1\"(case sensitive).\n";
            serverMessage.assign(temp.begin(), temp.end());
            server_sendMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);

            Sleep(10);

            /*Receiving messages */
            server_receiveMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &clientMessage);

            if ((strcmp(clientMessage.data(), "AppleLogo.png") == 0) ||
                (strcmp(clientMessage.data(), "LinuxLogo.png") == 0) ||
                (strcmp(clientMessage.data(), "Naruto.png") == 0) ||
                (strcmp(clientMessage.data(), "NikeLogo.png") == 0) ||
                (strcmp(clientMessage.data(), "WindowsLogo.png") == 0))
            {
                cout << "File name is valid\n";
                ifstream fileHandle(clientMessage.data(), ios::binary | ios::ate);
                streamsize fileSize = fileHandle.tellg();
                fileHandle.seekg(0, ios::beg);

                temp = "Server::Sending file...\n";
                clientMessage.assign(temp.begin(), temp.end());
                server_sendMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &clientMessage);
                Sleep(10);


                temp = to_string(fileSize);
                clientMessage.assign(temp.begin(), temp.end());
                server_sendMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &clientMessage);
                Sleep(10);

                if (fileHandle)
                {
                    cout << "File created\n";
                    while (!fileHandle.eof())
                    {
                        fileHandle.read(imageBuffer.data(), imageBuffer.size());
                        int bufferSize = static_cast<int>(fileHandle.gcount());
                        server_sendData_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), imageBuffer, bufferSize);
                        Sleep(5);
                    }
                    cout << "File transfer is complete\n";
                    Sleep(5);
                    temp = "Server:: File transfer is completed\n";
                    serverMessage.assign(temp.begin(), temp.end());
                    server_sendMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
                    fileHandle.close();
                    cout << "File handle closed\n";
                    Sleep(100);
                }
                else
                {
                    temp = "Server:: Couldn't open the file to read\n";
                    serverMessage.assign(temp.begin(), temp.end());
                    server_sendMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
                }
            }
            else if (strcmp(clientMessage.data(), "78Be1") == 0)
            {
                cout << "Server::Server issued a termination request\n";
                break;
            }
            else
            {
                temp = "Please enter valid options or exit word\n";
                serverMessage.assign(temp.begin(), temp.end());
                server_sendMessage_usingUDP(serverSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
            }

            //system("cls");
        } while (true);
    }
    return 0;
}

/*Server error display*/
void server_errorDisplay()
{
    cout << "Server::Check the below message for more information\n";
    cout << WSAGetLastError() << "\n";
}

/*Server binding to socket*/
bool server_bindSocket(SOCKET serverSocket, ST_sockaddr* serverSocketAddress, int addressLength)
{
    bool bindStatus = true;
    int bindingStatus = bind(serverSocket, serverSocketAddress, addressLength);
    if (bindingStatus == SOCKET_ERROR)
    {
        bindStatus = false;
        cout << "Server couldn't bind to the port\n";
        server_errorDisplay();
    }
    return bindStatus;
}

/*Load WSA DLL*/
void server_loadDLL()
{
    cout << "Server::Initiating winsock loading\n";
    WSADATA wsaData;
    WORD WinsockVersion = MAKEWORD(2, 2); //Binary Format for winsock 2.2 ----> 0000_0010(Minor version) 0000_0010(Major Version)
    int wsaDLLStatus = WSAStartup(WinsockVersion, &wsaData);

    if (wsaDLLStatus == 0) cout << "Server::Server has loaded Winsock 2.2 library\n";
    else
    {
        cout << "Server::Server couldn't load Winsock 2.2 library\n";
        server_errorDisplay();
        WSACleanup();
    }
}

/*Create a socket*/
SOCKET server_creatingSocket(int socketType, int commProtocol)
{
    cout << "Server::Attempting socket creation\n";
    //AF_INET ----> IPv4 Address Family, AF_INET6 ----> IPv6 Address Family
    SOCKET clientSocket = INVALID_SOCKET;
    clientSocket = socket(AF_INET,            //This code is created only for IP address families
        socketType,         //SOCK_STREAM ----> socket for TCP port, SOCK_DGRAM ----> socket for UDP port
        commProtocol);      //IPPROTO_UDP ----> socket for TCP protocol, IPPROTO_UDP ----> socket for UDP protocol
    if (clientSocket == INVALID_SOCKET)
    {
        cout << "Server::Server couldn't create a socket\n";
        server_errorDisplay();
        WSACleanup();
    }
    else cout << "Server::Socket creation is successful!!\n";
    return clientSocket;
}

/*Sending message using UDP*/
void server_sendMessage_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* clientMessage)
{
    int bytesSent = sendto(clientSocket, (*clientMessage).data(), (*clientMessage).size(), 0, &serverSocketAddress, sizeof(serverSocketAddress));
    if (bytesSent == SOCKET_ERROR)
    {
        cout << "Server::Server couldn't send messages from server\n";
        server_errorDisplay();
    }
}

/*Receiving message using UDP*/
void server_receiveMessage_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* clientMessage)
{
    int addressLength = sizeof(serverSocketAddress);
    int bytesReceived = recvfrom(clientSocket, (*clientMessage).data(), (*clientMessage).size(), 0, &serverSocketAddress, &addressLength);
    if (bytesReceived == SOCKET_ERROR)
    {
        cout << "Server::Server couldn't receive messages from server\n";
        server_errorDisplay();
    }
}

/*Sending data using UDP*/
void server_sendData_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char> buffer, int noOfBytes)
{
    int bytesSent = sendto(clientSocket, buffer.data(), noOfBytes, 0, &serverSocketAddress, sizeof(serverSocketAddress));
    if (bytesSent == SOCKET_ERROR)
    {
        cout << "Server::Server couldn't send data to server\n";
        server_errorDisplay();
    }
}

/*Receiving data using UDP*/
void server_receiveData_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* buffer)
{
    int addrLen = sizeof(serverSocketAddress);
    int bytesReceived = recvfrom(clientSocket, (*buffer).data(), (*buffer).size(), 0, &serverSocketAddress, &addrLen);
    if (bytesReceived == SOCKET_ERROR)
    {
        cout << "Server::Server couldn't receive data from server\n";
        server_errorDisplay();
    }
}

/*Server disconnect*/
void serverDisconnect(SOCKET clientSocket)
{
    try
    {
        int status = shutdown(clientSocket, SD_BOTH);
        if (status == SOCKET_ERROR)
        {
            cout << "Server::Server couldn't shutdown\n";
            server_errorDisplay();
        }
        ST_sockaddr_in st_PeerAddress;
        int addressLength = sizeof(ST_sockaddr_in);
        getpeername(clientSocket, (ST_sockaddr*)(&st_PeerAddress), &addressLength);
        cout << "Server::Communication has been terminated with the client " << st_PeerAddress.sin_addr.S_un.S_addr << "\n";
        closesocket(clientSocket);
        WSACleanup();
    }
    catch (exception e)
    {
        cerr << "Server::Server couldn't shutdown, please check the below given message\n";
        cerr << WSAGetLastError() << "\n";
    }
}

/*File renaming*/
string fileRename(string fileName, string prev, string curr)
{
    fileName.replace(fileName.find(prev), fileName.find(prev), curr);
    cout << fileName << "\n";
    return fileName;
}
#endif