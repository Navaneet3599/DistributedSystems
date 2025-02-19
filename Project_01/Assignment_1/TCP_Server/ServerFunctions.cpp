/*My server*/
/*This source code was written by Navaneet Rao Dhage, as a part of Distributed Systems course work.*/

#ifndef SERVERFUNCTIONS_CPP
#define SERVERFUNCTIONS_CPP


/*Includes*/
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>
#include <string>
#include <vector>
#include <iostream>
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
typedef struct ST_transmitCfg
{
    char hostName[20];
    char peerName[20];
    SOCKET acceptedSocket;
};

typedef struct ST_receiveCfg
{
    char hostName[20];
    char peerName[20];
    SOCKET acceptedSocket;
};

typedef sockaddr ST_sockaddr;
typedef sockaddr_in ST_sockaddr_in;

/*Variable declarations*/
vector<char> imageBuffer(BUFFER_SIZE);

/*Function Declarations*/
void server_errorDisplay();
void server_loadDLL();
SOCKET server_creatingSocket(int socketType, int commProtocol);
void server_bindingSocket(SOCKET serverSocket, char addr[], int serverPortNumber);
void server_listenForConnections(SOCKET serverSocket, int MaxNoOfConnections);
SOCKET server_acceptConnection(SOCKET listeningSocket, ST_sockaddr* st_sockaddr);
void server_sendMessage_usingTCP(SOCKET acceptedSocket, const char messageBuffer[200]);
void server_sendData_usingTCP(SOCKET acceptedSocket, const vector<char>& dataBuffer, int noOfBytes);
char* server_receiveData_usingTCP(SOCKET serverSOCKET, int dataLength);
char* server_receiveMessage_usingTCP(SOCKET serverSOCKET);
void server_disconnect(SOCKET acceptedSocket);

/*Function Definitions*/
/*******SERVER Functions*******/


int main()
{
    ST_sockaddr clientSocketAddress = { 0 };
    char clientRequest[200];
    char ServerIPAddress[15] = "0.0.0.0";
    char tempMessage[200];

    /*Loading winsock library*/
    server_loadDLL();

    /*Creating server socket*/
    SOCKET serverSocket = server_creatingSocket(SOCK_STREAM, IPPROTO_TCP);

    /*Binding server socket to IP address*/
    server_bindingSocket(serverSocket, ServerIPAddress, 55555);
    server_listenForConnections(serverSocket, 1);

    SOCKET acceptedSocket = server_acceptConnection(serverSocket, &clientSocketAddress);
    try {
        if (acceptedSocket != SOCKET_ERROR)
        {
            do
            {
                system("cls");
                while (true)
                {
                    Sleep(5);
                    memset(clientRequest, 0, 200);
                    strcpy_s(clientRequest, server_receiveMessage_usingTCP(acceptedSocket));
                    server_sendMessage_usingTCP(acceptedSocket, "Hello");
                    Sleep(5);
                    if (strcmp(clientRequest, "Hello") == 0)
                        break;
                }
                /*Sending messages to client*/
                Sleep(10);
                server_sendMessage_usingTCP(acceptedSocket, "Server::Client can request for the following images:\n");
                Sleep(10);
                server_sendMessage_usingTCP(acceptedSocket, "\tAppleLogo.png\n\tLinuxLogo.png\n\tNaruto.png\n\tNikeLogo.png\n\tWindowsLogo.png\n");
                Sleep(10);
                server_sendMessage_usingTCP(acceptedSocket, "If you wish to terminate the connection, then enter \"78Be1\"(case sensitive).\n");
                Sleep(10);
                /*Receiving messages */
                //strcpy_s(clientRequest, server_receiveMessage_usingTCP(acceptedSocket));
                strncpy_s(clientRequest, 200, server_receiveMessage_usingTCP(acceptedSocket), _TRUNCATE);

                if ((strcmp(clientRequest, "AppleLogo.png") == 0) ||
                    (strcmp(clientRequest, "LinuxLogo.png") == 0) ||
                    (strcmp(clientRequest, "Naruto.png") == 0) ||
                    (strcmp(clientRequest, "NikeLogo.png") == 0) ||
                    (strcmp(clientRequest, "WindowsLogo.png") == 0))
                {
                    cout << "File name is valid\n";
                    ifstream fileHandle(clientRequest, ios::binary | ios::ate);
                    streamsize fileSize = fileHandle.tellg();
                    cout << fileSize << "\n";
                    fileHandle.seekg(0, ios::beg);
                    memset(tempMessage, 0, 200);
                    strcpy_s(tempMessage, to_string(fileSize).c_str());
                    server_sendMessage_usingTCP(acceptedSocket, "Server::Sending file...\n");
                    Sleep(10);
                    server_sendMessage_usingTCP(acceptedSocket, tempMessage);
                    if (fileHandle)
                    {
                        cout << "File created\n";
                        while (!fileHandle.eof())
                        {
                            fileHandle.read(imageBuffer.data(), imageBuffer.size());
                            int bufferSize = static_cast<int>(fileHandle.gcount());
                            server_sendData_usingTCP(acceptedSocket, imageBuffer, bufferSize);
                            Sleep(5);
                        }
                        cout << "File transfer is complete\n";
                        Sleep(5);
                        server_sendMessage_usingTCP(acceptedSocket, "Server:: File transfer is completed\n");
                        fileHandle.close();
                        cout << "File handle closed\n";
                        Sleep(100);
                    }
                    else
                    {
                        server_sendMessage_usingTCP(acceptedSocket, "Server:: Couldn't open the file to read\n");
                    }
                }
                else if (strcmp(clientRequest, "78Be1") == 0)
                {
                    cout << "Client::Client issued a termination request\n";
                    break;
                }
                else
                {
                    server_sendMessage_usingTCP(acceptedSocket, "Please enter valid options or exit word\n");
                }

                system("cls");
            } while (true);
        }
        else
        {
            cout << "Server::Server couldn't accept connection, please check the below given message\n";
        }
    }
    catch (exception e)
    {
        cout << "check now";
    }


    server_disconnect(serverSocket);
    server_errorDisplay();
}


void server_errorDisplay()
{
    cout << "Server::Check the below message for more information\n";
    cout << WSAGetLastError() << "\n";
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
    SOCKET serverSocket = INVALID_SOCKET;
    serverSocket = socket(AF_INET,            //This code is created only for IP address families
        socketType,         //SOCK_STREAM ----> socket for TCP port, SOCK_DGRAM ----> socket for UDP port
        commProtocol);      //IPPROTO_TCP ----> socket for TCP protocol, IPPROTO_UDP ----> socket for UDP protocol
    if (serverSocket == INVALID_SOCKET) cout << "Server::Server couldn't create a socket\n";
    else cout << "Server::Socket creation is successful!!\n";
    return serverSocket;
}

/*Bind the socket*/
void server_bindingSocket(SOCKET serverSocket,    //Unbound socket
    char addr[],      //Name of the server socket address
    int serverPortNumber)
{
    ST_sockaddr_in* serverSocketProperties = new ST_sockaddr_in;

    /*Giving the address a 127.0.0.1 will echo back all the requests to this system*/
    if (inet_pton(AF_INET, addr, &serverSocketProperties->sin_addr) == 1) cout << "----------Server address specification ---->VALID\n";
    else if (inet_pton(AF_INET, addr, &serverSocketProperties->sin_addr) == 0) cout << "----------Server address specification ---->INVALID\n";
    else if (inet_pton(AF_INET, addr, &serverSocketProperties->sin_addr) == -1) cout << "----------Server address specification ---->OTHER ERRORS\n" << WSAGetLastError() << "\n";

    serverSocketProperties->sin_family = AF_INET;
    serverSocketProperties->sin_port = htons(serverPortNumber);

    int bindingStatus = bind(serverSocket,                           //Unbound socket
        (ST_sockaddr*)serverSocketProperties,   //Address of the server socket address
        sizeof(ST_sockaddr));                   //Size of the socket address structure

    if (bindingStatus == 0)
    {
        cout << "Server::Server socket binding is done\n";
        cout << "Server::Address = " << addr << "\n";
        cout << "Server::Port = " << ntohs(serverSocketProperties->sin_port) << "\n";
    }
    else
    {
        cout << "Server::Server socket binding has failed\n";
        cout << "Server::Check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
        closesocket(serverSocket);
    }
}

/*Listen on the socket*/
void server_listenForConnections(SOCKET serverSocket, int MaxNoOfConnections)
{
    if (listen(serverSocket, MaxNoOfConnections) == SOCKET_ERROR)
    {
        cout << "Server::Server failed to listen\n";
        cout << "Server::Check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Server::Server is listening to connect with clients\n";
}

/*Accept a connection*/
SOCKET server_acceptConnection(SOCKET listeningSocket, ST_sockaddr* peerSocketAddr)
{
    SOCKET acceptedSOCKET = INVALID_SOCKET;
    int addressLength = sizeof(ST_sockaddr);
    acceptedSOCKET = accept(listeningSocket, NULL, NULL);
    if (acceptedSOCKET == INVALID_SOCKET)
    {
        cout << "Server::Server failed to accept connection\n";
    }
    else
    {
        char clientAddress[INET_ADDRSTRLEN];
        memset(clientAddress, 0, sizeof(clientAddress));
        getpeername(acceptedSOCKET, peerSocketAddr, &addressLength);
        inet_ntop(AF_INET, &(((ST_sockaddr_in*)peerSocketAddr)->sin_addr), clientAddress, sizeof(clientAddress));
        cout << "Server::Server has accepted the socket connection\n";
        cout << "Server::Client address " << clientAddress << "\n";
        cout << "Server::Client address " << htons(((ST_sockaddr_in*)peerSocketAddr)->sin_port) << "\n";
    }
    return acceptedSOCKET;
}

/*Send text message using TCP*/
void server_sendMessage_usingTCP(SOCKET acceptedSocket, const char messageBuffer[200])
{
    int dataTransferedBytes = send(acceptedSocket, messageBuffer, 200, 0);

    if (dataTransferedBytes == 0)
    {
        cout << "Server::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
}

/*Send data using TCP*/
void server_sendData_usingTCP(SOCKET acceptedSocket, const vector<char>& dataBuffer, int noOfBytes)
{
    int dataTransferredBytes = send(acceptedSocket, dataBuffer.data(), noOfBytes, 0);

    if (dataTransferredBytes == SOCKET_ERROR)
    {
        std::cerr << "Server::Failed to send data to client.\n";
        std::cerr << "Server::Error Code: " << WSAGetLastError() << "\n";
        return;
    }
}


/*Receive data using TCP*/
char* server_receiveData_usingTCP(SOCKET serverSOCKET, int dataLength)
{
    char* dataBuffer = new char[dataLength];
    int dataReceivedBytes = recv(serverSOCKET, dataBuffer, dataLength, 0);

    if (dataReceivedBytes == SOCKET_ERROR)
    {
        cout << "Server::Server failed to receive data";
        cout << "Server::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Server::Server has received " << dataReceivedBytes << " bytes of data from client\n";
    return dataBuffer;
}

/*Receive message using TCP*/
char* server_receiveMessage_usingTCP(SOCKET serverSOCKET)
{
    char* dataBuffer = new char[200];
    int dataReceivedBytes = recv(serverSOCKET, dataBuffer, 200, 0);

    if (dataReceivedBytes == SOCKET_ERROR)
    {
        cout << "Server::Server failed to receive message";
        cout << "Server::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    //else cout << "Server::Server has received " << dataReceivedBytes << " bytes of data from client\n";

    return dataBuffer;
}

/*Disconnect the current connection*/
void server_disconnect(SOCKET acceptedSocket)
{
    try
    {
        int status = shutdown(acceptedSocket, SD_BOTH);
        if (status == SOCKET_ERROR)
        {
            cout << "Server::Server couldn't shutdown\n";
            server_errorDisplay();
        }
        ST_sockaddr_in st_PeerAddress;
        int addressLength = sizeof(ST_sockaddr_in);
        getpeername(acceptedSocket, (ST_sockaddr*)(&st_PeerAddress), &addressLength);
        cout << "Server::Communication has been terminated with the client " << st_PeerAddress.sin_addr.S_un.S_addr << "\n";
        closesocket(acceptedSocket);
        WSACleanup();
    }
    catch (exception e)
    {
        cerr << "Server::Server couldn't shutdown, please check the below given message\n";
        cerr << WSAGetLastError() << "\n";
    }
}

#endif