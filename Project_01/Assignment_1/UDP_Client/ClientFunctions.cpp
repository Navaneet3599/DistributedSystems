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
void client_sendMessage_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* clientMessage);
void client_receiveMessage_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* clientMessage);
void client_sendData_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char> buffer, int noOfBytes);
void client_receiveData_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* buffer);
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
    client_loadDLL();

    /*Creating client socket*/
    //SOCKET clientSocket = client_creatingSocket(SOCK_STREAM, IPPROTO_TCP);
    SOCKET clientSocket = client_creatingSocket(SOCK_DGRAM, IPPROTO_UDP);

    /*Specifying server socket address*/
    serverSocketAddress.sin_family = AF_INET;
    serverSocketAddress.sin_port = htons(55555);
    if (inet_pton(AF_INET, serverIPAddress, &serverSocketAddress.sin_addr) == 1) cout << "----------Server address specification ---->VALID\n";
    else if (inet_pton(AF_INET, serverIPAddress, &serverSocketAddress.sin_addr) == 0) cout << "----------Server address specification ---->INVALID\n";
    else if (inet_pton(AF_INET, serverIPAddress, &serverSocketAddress.sin_addr) == -1) cout << "----------Server address specification ---->OTHER ERRORS\n" << WSAGetLastError() << "\n";

    do
    {
        //system("cls");
        while (true)
        {
            cout << serverMessage.data();
            if (strcmp(serverMessage.data(), "Hello") == 0)
                break;
            clientMessage.assign("Hello", "Hello" + 5);
            Sleep(5);
            client_sendMessage_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &clientMessage);
            Sleep(5);
            client_receiveMessage_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
            Sleep(5);
        }

        client_receiveMessage_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
        cout << serverMessage.data();


        client_receiveMessage_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
        cout << serverMessage.data();


        client_receiveMessage_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
        cout << serverMessage.data();


        client_receiveMessage_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
        cout << serverMessage.data();

        getline(cin, temp);
        clientMessage.assign(temp.begin(), temp.end());
        client_sendMessage_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &clientMessage);

        if (strcmp(clientMessage.data(), "AppleLogo.png") == 0) imageTransfer = true;
        else if (strcmp(clientMessage.data(), "LinuxLogo.png") == 0) imageTransfer = true;
        else if (strcmp(clientMessage.data(), "Naruto.png") == 0) imageTransfer = true;
        else if (strcmp(clientMessage.data(), "NikeLogo.png") == 0) imageTransfer = true;
        else if (strcmp(clientMessage.data(), "WindowsLogo.png") == 0) imageTransfer = true;
        else if (strcmp(clientMessage.data(), "78Be1") == 0)
        {
            cout << "Initiating termination request...\n";
            break;
        }
        else if (!regex_search(serverMessage.data(), pattern))
        {
            cout << "Please enter a valid file name...\n";
            continue;
        }


        if (imageTransfer == true)
        {
            string fileName(clientMessage.data());
            string temp1 = ".png";
            string temp2 = "fileDescriptor.png";
            string newFileName = fileRename(fileName, temp1, temp2);
            ofstream fileHandle(newFileName, ios::binary);
            client_receiveMessage_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
            cout << serverMessage.data() << "\n";

            client_receiveMessage_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);

            int totalFileSize = stoi(serverMessage.data());
            if (fileHandle) {
                while (totalFileSize > 0) {
                    int bytesReceived = 0;
                    client_receiveData_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &imageBuffer);

                    /*if (bytesReceived <= 0) {
                        cout << "Data transfer complete or no data received.\n";
                        break;
                    }*/

                    totalFileSize -= bytesReceived;
                    fileHandle.write(imageBuffer.data(), bytesReceived);
                }

                string temp3 = "fileDescriptor.png";
                string temp4 = to_string(*((int*)(&fileHandle))) + ".png";
                fileName = fileRename(newFileName, temp3, temp4);
                fileHandle.close();

                client_receiveMessage_usingUDP(clientSocket, *((ST_sockaddr*)(&serverSocketAddress)), &serverMessage);
                cout << serverMessage.data() << "\n";

                (void)rename(newFileName.c_str(), fileName.c_str());
            }
            else
            {
                cout << "Client::Couldn't create a file with the name \"" << clientMessage.data() << "\"\n";
                system("pause");
            }
            imageTransfer = false;
            system("pause");
            cout << "If you wish to continue, then press enter...";
            //system("cls");
        }

    } while (true);
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

/*Sending message using UDP*/
void client_sendMessage_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* clientMessage)
{
    int bytesSent = sendto(clientSocket, (*clientMessage).data(), (*clientMessage).size(), 0, &serverSocketAddress, sizeof(serverSocketAddress));
    if (bytesSent == SOCKET_ERROR)
    {
        cout << "Client::Client couldn't send messages from server\n";
        client_errorDisplay();
    }
}

/*Receiving message using UDP*/
void client_receiveMessage_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* clientMessage)
{
    int addrLen = sizeof(serverSocketAddress);
    int bytesReceived = recvfrom(clientSocket, (*clientMessage).data(), (*clientMessage).size(), 0, &serverSocketAddress, &addrLen);
    if (bytesReceived == SOCKET_ERROR)
    {
        cout << "Client::Client couldn't receive messages from server\n";
        client_errorDisplay();
    }
}

/*Sending data using UDP*/
void client_sendData_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char> buffer, int noOfBytes)
{
    int bytesSent = sendto(clientSocket, buffer.data(), noOfBytes, 0, &serverSocketAddress, sizeof(serverSocketAddress));
    if (bytesSent == SOCKET_ERROR)
    {
        cout << "Client::Client couldn't send data to server\n";
        client_errorDisplay();
    }
}

/*Receiving data using UDP*/
void client_receiveData_usingUDP(SOCKET clientSocket, ST_sockaddr serverSocketAddress, vector<char>* buffer)
{
    int addrLen = sizeof(serverSocketAddress);
    int bytesReceived = recvfrom(clientSocket, (*buffer).data(), (*buffer).size(), 0, &serverSocketAddress, &addrLen);
    if (bytesReceived == SOCKET_ERROR)
    {
        cout << "Client::Client couldn't receive data from server\n";
        client_errorDisplay();
    }
}

/*Client disconnect*/
void clientDisconnect(SOCKET clientSocket)
{
    try
    {
        int status = shutdown(clientSocket, SD_BOTH);
        if (status == SOCKET_ERROR)
        {
            cout << "Server::Server couldn't shutdown\n";
            client_errorDisplay();
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