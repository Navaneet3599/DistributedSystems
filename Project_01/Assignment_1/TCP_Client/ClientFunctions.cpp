/*My client*/
/*This source code was written by Navaneet Rao Dhage, as a part of Distributed Systems course work.*/

#ifndef CLIENTFUNCTIONS_CPP
#define CLIENTFUNCTIONS_CPP


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
void client_errorDisplay();
void client_loadDLL();
SOCKET client_creatingSocket(int socketType, int commProtocol);
int client_connectingToServer(SOCKET clientSocket, ST_sockaddr* serverAddr);
void client_sendMessage_usingTCP(SOCKET acceptedSocket, const char messageBuffer[BUFFER_SIZE]);
void client_sendData_usingTCP(SOCKET acceptedSocket, char dataBuffer[], int noOfBytes);
//char* client_receiveData_usingTCP(SOCKET serverSOCKET, int* dataLength);
std::vector<char> client_receiveData_usingTCP(SOCKET serverSOCKET, int* bytesReceived);
char* client_receiveMessage_usingTCP(SOCKET clientSocket);
void client_disconnect(SOCKET acceptedSocket);
string fileRename(string fileName, string prev, string curr);

/*Function Definitions*/
/*******Client Functions*******/
int main()
{
    int bytesReceived;
    char clientRequest[200];
    char serverMessage[200];
    char imageBuffer[BUFFER_SIZE];
    char serverIPAddress[15] = "127.0.0.1";
    bool imageTransfer = false;
    ST_sockaddr_in serverSocketAddress = { 0 };
    regex pattern(".png");

    /*Loading winsock library*/
    client_loadDLL();
    /*Creating client socket*/
    SOCKET clientSocket = client_creatingSocket(SOCK_STREAM, IPPROTO_TCP);
    //SOCKET clientSocket = client_creatingSocket(SOCK_DGRAM, IPPROTO_UDP);

    /*Specifying server socket address*/
    serverSocketAddress.sin_family = AF_INET;
    serverSocketAddress.sin_port = htons(55555);
    if (inet_pton(AF_INET, serverIPAddress, &serverSocketAddress.sin_addr) == 1) cout << "----------Server address specification ---->VALID\n";
    else if (inet_pton(AF_INET, serverIPAddress, &serverSocketAddress.sin_addr) == 0) cout << "----------Server address specification ---->INVALID\n";
    else if (inet_pton(AF_INET, serverIPAddress, &serverSocketAddress.sin_addr) == -1) cout << "----------Server address specification ---->OTHER ERRORS\n" << WSAGetLastError() << "\n";

    if (client_connectingToServer(clientSocket, (ST_sockaddr*)(&serverSocketAddress)) != SOCKET_ERROR)
    {
        do
        {
            system("cls");
            while (true)
            {
                Sleep(5);
                memset(clientRequest, 0, 200);
                client_sendMessage_usingTCP(clientSocket, "Hello");
                strcpy_s(clientRequest, client_receiveMessage_usingTCP(clientSocket));
                Sleep(5);
                if (strcmp(clientRequest, "Hello") == 0)
                    break;
            }
            memset(clientRequest, 0, 200);

            memset(serverMessage, 0, 200);
            strcpy_s(serverMessage, client_receiveMessage_usingTCP(clientSocket));
            cout << serverMessage;
            memset(serverMessage, 0, 200);
            strcpy_s(serverMessage, client_receiveMessage_usingTCP(clientSocket));
            cout << serverMessage;
            memset(serverMessage, 0, 200);
            strcpy_s(serverMessage, client_receiveMessage_usingTCP(clientSocket));
            cout << serverMessage;
            memset(serverMessage, 0, 200);

            cin.getline(clientRequest, 200);
            client_sendMessage_usingTCP(clientSocket, clientRequest);

            if (strcmp(clientRequest, "AppleLogo.png") == 0) imageTransfer = true;
            else if (strcmp(clientRequest, "LinuxLogo.png") == 0) imageTransfer = true;
            else if (strcmp(clientRequest, "Naruto.png") == 0) imageTransfer = true;
            else if (strcmp(clientRequest, "NikeLogo.png") == 0) imageTransfer = true;
            else if (strcmp(clientRequest, "WindowsLogo.png") == 0) imageTransfer = true;
            else if (strcmp(clientRequest, "78Be1") == 0)
            {
                cout << "Initiating termination request...\n";
                break;
            }
            else if (!regex_search(serverMessage, pattern))
            {
                cout << "Please enter a valid file name...\n";
                continue;
            }


            if (imageTransfer == true)
            {
                string fileName(clientRequest);
                string temp1 = ".png";
                string temp2 = "fileDescriptor.png";
                string newFileName = fileRename(fileName, temp1, temp2);
                ofstream fileHandle(newFileName, ios::binary);
                strcpy_s(serverMessage, client_receiveMessage_usingTCP(clientSocket));
                cout << serverMessage << "\n";
                memset(serverMessage, 0, 200);

                strcpy_s(serverMessage, client_receiveMessage_usingTCP(clientSocket));

                int totalFileSize = stoi(serverMessage);
                if (fileHandle) {
                    while (totalFileSize > 0) {
                        int bytesReceived = 0;
                        auto imageChunk = client_receiveData_usingTCP(clientSocket, &bytesReceived);

                        /*if (bytesReceived <= 0) {
                            cout << "Data transfer complete or no data received.\n";
                            break;
                        }*/

                        totalFileSize -= bytesReceived;
                        fileHandle.write(imageChunk.data(), bytesReceived);
                    }
                    
                    string temp3 = "fileDescriptor.png";
                    string temp4 = to_string(*((int*)(&fileHandle))) + ".png";
                    fileName = fileRename(newFileName, temp3, temp4);
                    fileHandle.close();
                    memset(serverMessage, 0, 200);
                    strncpy_s(serverMessage, client_receiveMessage_usingTCP(clientSocket), _TRUNCATE);
                    cout << serverMessage << "\n";

                    (void)rename(newFileName.c_str(), fileName.c_str());
                }
                else
                {
                    cout << "Client::Couldn't create a file with the name \"" << clientRequest << "\"\n";
                    system("pause");
                }
                imageTransfer = false;
                system("pause");
                cout << "If you wish to continue, then press enter...";
                system("cls");
            }
        } while (true);
    }
    else
    {
        cout << "Client::Client couldn't connect to server, please check the below given messages\n";
    }
    client_disconnect(clientSocket);
    client_errorDisplay();
    return 0;
}


void client_errorDisplay()
{
    cout << "Client::Check the below message for more information\n";
    cout << WSAGetLastError() << "\n";
}

/*Load WSA DLL*/
void client_loadDLL()
{
    cout << "Client::Initiating winsock loading\n";
    WSADATA wsaData;
    WORD WinsockVersion = MAKEWORD(2, 2); //Binary Format for winsock 2.2 ----> 0000_0010(Minor version) 0000_0010(Major Version)
    int wsaDLLStatus = WSAStartup(WinsockVersion, &wsaData);

    if (wsaDLLStatus == 0) cout << "Client::Client has loaded Winsock 2.2 library\n";
    else
    {
        cout << "Client::Client couldn't load Winsock 2.2 library\n";
        client_errorDisplay();
        WSACleanup();
    }
}

/*Create a socket*/
SOCKET client_creatingSocket(int socketType, int commProtocol)
{
    cout << "Client::Attempting socket creation\n";
    //AF_INET ----> IPv4 Address Family, AF_INET6 ----> IPv6 Address Family
    SOCKET clientSocket = INVALID_SOCKET;
    clientSocket = socket(AF_INET,            //This code is created only for IP address families
        socketType,         //SOCK_STREAM ----> socket for TCP port, SOCK_DGRAM ----> socket for UDP port
        commProtocol);      //IPPROTO_UDP ----> socket for TCP protocol, IPPROTO_UDP ----> socket for UDP protocol
    if (clientSocket == INVALID_SOCKET)
    {
        cout << "Client::Client couldn't create a socket\n";
        client_errorDisplay();
        WSACleanup();
    }
    else cout << "Client::Socket creation is successful!!\n";
    return clientSocket;
}

/*Connecting to server*/
int client_connectingToServer(SOCKET clientSocket, ST_sockaddr* serverAddr)
{
    char serverAddress[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(((ST_sockaddr_in*)(serverAddr))->sin_addr), serverAddress, INET_ADDRSTRLEN);
    int connectionStatus = connect(clientSocket, serverAddr, sizeof(*serverAddr));
    if (connectionStatus != SOCKET_ERROR)
    {
        cout << "Client::Client has connected to server\n";
        cout << "Client::Server address = " << serverAddress << "\n";
        cout << "Client::Server port = " << ntohs(((ST_sockaddr_in*)serverAddr)->sin_port) << "\n";
    }
    else
    {
        cout << "Client::Client couldn't connect to server\n";
        cout << "Client::Server address = " << serverAddress << "\n";
        cout << "Client::Server port = " << ntohs(((ST_sockaddr_in*)serverAddr)->sin_port) << "\n";
        cout << "Client::Check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
        closesocket(clientSocket);
    }
    return connectionStatus;
}

/*Send text message using TCP*/
void client_sendMessage_usingTCP(SOCKET acceptedSocket, const char messageBuffer[200])
{
    int dataTransferedBytes = send(acceptedSocket, messageBuffer, 200, 0);

    if (dataTransferedBytes == 0)
    {
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
}

/*Send data using TCP*/
void client_sendData_usingTCP(SOCKET acceptedSocket, char dataBuffer[], int noOfBytes)
{
    int dataTransferedBytes = send(acceptedSocket, dataBuffer, noOfBytes, 0);
    ST_sockaddr* socketPeerAddress = new ST_sockaddr;
    int addressLength = sizeof(ST_sockaddr);
    getpeername(acceptedSocket, socketPeerAddress, &addressLength);

    if (dataTransferedBytes == 0)
    {
        cout << "Client::Client couldn't send data to Client(" << ntohl(((ST_sockaddr_in*)socketPeerAddress)->sin_addr.S_un.S_addr) << ")\n";
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Client::Client has sent " << dataTransferedBytes << " bytes of data to Client(" << ntohl(((ST_sockaddr_in*)socketPeerAddress)->sin_addr.S_un.S_addr) << "\n";
}

/*Receive data using TCP*/
/*char* client_receiveData_usingTCP(SOCKET serverSOCKET, int* bytesReceived)
{
    char* dataBuffer = new char[BUFFER_SIZE];
    *bytesReceived = recv(serverSOCKET, dataBuffer, BUFFER_SIZE, 0);

    if (*bytesReceived == SOCKET_ERROR)
    {
        cout << "Client::Client failed to receive data";
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Client::Client has received " << *bytesReceived << " bytes of data from client\n";
    return dataBuffer;
}*/
std::vector<char> client_receiveData_usingTCP(SOCKET serverSOCKET, int* bytesReceived) {
    std::vector<char> dataBuffer(BUFFER_SIZE);
    *bytesReceived = recv(serverSOCKET, dataBuffer.data(), BUFFER_SIZE, 0);

    if (*bytesReceived == SOCKET_ERROR) {
        cout << "Client::Client failed to receive data\n";
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
        return {};
    }

    // Resize to actual received bytes
    dataBuffer.resize(*bytesReceived);
    return dataBuffer;
}


/*Receive message using TCP*/
char* client_receiveMessage_usingTCP(SOCKET clientSocket)
{
    char* dataBuffer = new char[200];
    int dataReceivedBytes = recv(clientSocket, dataBuffer, 200, 0);

    if (dataReceivedBytes == SOCKET_ERROR)
    {
        cout << "Client::Client failed to receive data";
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    //else cout << "Client::Client has received " << dataReceivedBytes << " bytes of data from client\n";

    return dataBuffer;
}

/*Disconnect the current connection*/
void client_disconnect(SOCKET acceptedSocket)
{
    try
    {
        cout << "Client::Halting all send requests\n";
        int status = shutdown(acceptedSocket, SD_SEND);
        if (status == SOCKET_ERROR)
        {
            cout << "Client::Client couldn't shutdown\n";
            client_errorDisplay();
        }
        ST_sockaddr_in st_PeerAddress;
        int addressLength = sizeof(ST_sockaddr_in);
        getpeername(acceptedSocket, (ST_sockaddr*)(&st_PeerAddress), &addressLength);
        cout << "Client::Initiating shutdown\n";
        status = shutdown(acceptedSocket, SD_BOTH);
        if (status == SOCKET_ERROR)
        {
            cout << "Client::Client couldn't shutdown\n";
            client_errorDisplay();
        }
        else
        {
            cout << "Client::Communication has been terminated with the server " << st_PeerAddress.sin_addr.S_un.S_addr << " on port " << st_PeerAddress.sin_port << "\n";
            closesocket(acceptedSocket);
            WSACleanup();
        }
    }
    catch (exception e)
    {
        cerr << "Client::Client couldn't shutdown, please check the below given message\n";
        cerr << WSAGetLastError() << "\n";
    }
}

string fileRename(string fileName, string prev, string curr)
{
    fileName.replace(fileName.find(prev), fileName.find(prev), curr);
    cout << fileName << "\n";
    return fileName;
}
#endif