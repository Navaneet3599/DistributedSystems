/*This source code was written by Navaneet Rao Dhage, as a part of Distributed Systems course work.*/

#ifndef SERVERFUNCTIONS_CPP
#define SERVERFUNCTIONS_CPP


/*Includes*/
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <ServerFunctions.h>

#pragma comment(lib, "Ws2_32.lib")


/*Defines*/
using namespace std;
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define BUFFER_SIZE 200

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

/*Variable declarations*/
char transmitBuffer[BUFFER_SIZE];
char receiveBuffer[BUFFER_SIZE];
bool chatTerminationRequest = false;
HANDLE mutexHandle, mainHandle, hConsole;

/*Function Declarations*/
void server_errorDisplay();
void server_loadDLL();
SOCKET server_creatingSocket(int socketType, int commProtocol);
void server_bindingSocket(SOCKET serverSocket, const char addr[]);
void server_listenForConnections(SOCKET serverSocket, int MaxNoOfConnections);
SOCKET server_acceptConnection(SOCKET listeningSocket, ST_sockaddr* st_sockaddr);
void server_sendMessage_usingTCP(SOCKET acceptedSocket, const char messageBuffer[200]);
void server_sendData_usingTCP(SOCKET acceptedSocket, char dataBuffer[], int noOfBytes);
char* server_receiveData_usingTCP(SOCKET serverSOCKET, int dataLength);
char* server_receiveMessage_usingTCP(SOCKET serverSOCKET);
void server_sendMessage_usingUDP(SOCKET serverSocket, const char dataBuffer[], const ST_sockaddr* clientSocketAddress);
void server_sendData_usingUDP(SOCKET serverSOCKET, char* dataBuffer, const ST_sockaddr* clientSocketAddress);
char* server_receiveMessage_usingUDP(SOCKET serverSOCKET, int dataLength, ST_sockaddr* clientSocketAddress);
char* server_receiveData_usingUDP(SOCKET serverSOCKET, int dataLength, ST_sockaddr* clientSocketAddress);
void server_disconnect(SOCKET acceptedSocket);
DWORD WINAPI transmitMessages(LPVOID structPtr);
DWORD WINAPI receiveMessages(LPVOID ptr);

/*Function Definitions*/
/*******SERVER Functions*******/


int main()
{
    /*Server chat bot implementation*/
    char hostName[20];
    char peerName[20] = "";
    ST_transmitCfg st_transmitCfg;
    ST_receiveCfg st_receiveCfg;
    DWORD transmitThreadID, receiveThreadID;
    HANDLE transmitHandle, receiveHandle;
    ST_sockaddr clientSocketAddress = { 0 };

    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    /*Loading winsock library*/
    server_loadDLL();

    /*Creating server socket*/
    SOCKET serverSocket = server_creatingSocket(SOCK_STREAM, IPPROTO_TCP);
    //SOCKET serverSocket = server_creatingSocket(SOCK_DGRAM, IPPROTO_UDP);

    /*Binding server socket to IP address*/
    server_bindingSocket(serverSocket, "127.0.0.1");
    server_listenForConnections(serverSocket, 1);
    
    SOCKET acceptedSocket = server_acceptConnection(serverSocket, &clientSocketAddress);
    if (acceptedSocket != SOCKET_ERROR)
    {
        system("cls");
        cout << "Do you wish to be named anything other than \"Server\"? \n[Y/N]";
        cin >> hostName;

        if (strcmp(hostName, "Y") == 0)
        {
            cout << "Enter your name: ";
            cin >> hostName;
            cout << "\n";
        }
        else strcpy_s(hostName, "Server");

        server_sendMessage_usingTCP(acceptedSocket, hostName);
        strcpy_s(peerName, server_receiveMessage_usingTCP(acceptedSocket));

        
        st_transmitCfg.acceptedSocket = acceptedSocket;
        strcpy_s(st_transmitCfg.hostName, hostName);
        strcpy_s(st_transmitCfg.peerName, peerName);

        
        st_receiveCfg.acceptedSocket = acceptedSocket;
        strcpy_s(st_receiveCfg.hostName, hostName);
        strcpy_s(st_receiveCfg.peerName, peerName);

        mutexHandle = CreateMutex(NULL, false, NULL);

        system("cls");
        cout << "Initiating chat sequence with "<< peerName << "...\n";
        cout << "Type \"d7bEx8\" for exiting the chat(case sensitive)\n";

        transmitHandle = CreateThread(NULL, 0, transmitMessages, (LPVOID)(&st_transmitCfg), 0, &transmitThreadID);
        if(transmitHandle != 0)
            SetPriorityClass(transmitHandle, THREAD_PRIORITY_NORMAL);
        receiveHandle = CreateThread(NULL, 0, receiveMessages, (LPVOID)(&st_receiveCfg), 0, &receiveThreadID);
        if (receiveHandle != 0)
            SetPriorityClass(receiveHandle, THREAD_PRIORITY_ABOVE_NORMAL);

        mainHandle = GetCurrentThread();
        SuspendThread(mainHandle);
    }
    Sleep(100);
    while (true)
    {
        /*Clearing the message buffer to receive messages from client*/
        memset(receiveBuffer, 'a', BUFFER_SIZE);

        /*Receiving messages from client*/
        strcpy_s(receiveBuffer, server_receiveMessage_usingTCP(acceptedSocket));

        if(strcmp(receiveBuffer, "") == 0) server_disconnect(acceptedSocket);
    }
    return 0;
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
    serverSocket = socket(  AF_INET,            //This code is created only for IP address families
                            socketType,         //SOCK_STREAM ----> socket for TCP port, SOCK_DGRAM ----> socket for UDP port
                            commProtocol);      //IPPROTO_UDP ----> socket for TCP protocol, IPPROTO_UDP ----> socket for UDP protocol
    if (serverSocket == INVALID_SOCKET) cout << "Server::Server couldn't create a socket\n";
    else cout << "Server::Socket creation is successful!!\n";
    return serverSocket;
}

/*Bind the socket*/
void server_bindingSocket(  SOCKET serverSocket,    //Unbound socket
                            const char addr[])            //Name of the server socket address
{
    ST_sockaddr_in* serverSocketProperties = new ST_sockaddr_in;

    /*Giving the address a 127.0.0.1 will echo back all the requests to this system*/
    if (inet_pton(AF_INET, "127.0.0.1", &serverSocketProperties->sin_addr) == 1) cout << "----------Server address specification ---->VALID\n";
    else if (inet_pton(AF_INET, "127.0.0.1", &serverSocketProperties->sin_addr) == 0) cout << "----------Server address specification ---->INVALID\n";
    else if (inet_pton(AF_INET, "127.0.0.1", &serverSocketProperties->sin_addr) == -1) cout << "----------Server address specification ---->OTHER ERRORS\n" << WSAGetLastError() << "\n";

    serverSocketProperties->sin_family = AF_INET;
    serverSocketProperties->sin_port = htons(8080);

    int bindingStatus = bind(   serverSocket,                           //Unbound socket
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
SOCKET server_acceptConnection(SOCKET listeningSocket, ST_sockaddr* st_sockaddr)
{
    SOCKET acceptedSOCKET = INVALID_SOCKET;
    ST_sockaddr peerSocketAddr;
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
        getpeername(acceptedSOCKET, &peerSocketAddr, &addressLength);
        inet_ntop(AF_INET, &(((ST_sockaddr_in*)&peerSocketAddr)->sin_addr),clientAddress, sizeof(clientAddress));
        cout << "Server::Server has accepted the socket connection\n";
        cout << "Server::Client address " << clientAddress << "\n";
        cout << "Server::Client address " << htons(((ST_sockaddr_in*)&peerSocketAddr)->sin_port) << "\n";
    }
    return acceptedSOCKET;
}

/*Send text message using TCP*/
void server_sendMessage_usingTCP(SOCKET acceptedSocket, const char messageBuffer[200])
{
    int dataTransferedBytes = send(acceptedSocket, messageBuffer, BUFFER_SIZE, 0);

    if (dataTransferedBytes == 0)
    {
        cout << "Server::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
}

/*Send data using TCP*/
void server_sendData_usingTCP(SOCKET acceptedSocket, char dataBuffer[], int noOfBytes)
{
    int dataTransferedBytes = send(acceptedSocket, dataBuffer, noOfBytes, 0);
    ST_sockaddr* socketPeerAddress = new ST_sockaddr;
    int addressLength = sizeof(ST_sockaddr);
    getpeername(acceptedSocket, socketPeerAddress, &addressLength);
   
    if (dataTransferedBytes == 0)
    {
        cout << "Server::Server couldn't send data to Client(" << ntohl(((ST_sockaddr_in*)socketPeerAddress)->sin_addr.S_un.S_addr) << ")\n";
        cout << "Server::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Server::Server has sent " << dataTransferedBytes << " bytes of data to Client(" << ntohl(((ST_sockaddr_in*)socketPeerAddress)->sin_addr.S_un.S_addr) << "\n";
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
    int dataReceivedBytes = recv(serverSOCKET, dataBuffer, BUFFER_SIZE, 0);

    if (dataReceivedBytes == SOCKET_ERROR)
    {
        cout << "Server::Server failed to receive data";
        cout << "Server::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    //else cout << "Server::Server has received " << dataReceivedBytes << " bytes of data from client\n";

    return dataBuffer;
}

/*Send message using UDP*/
void server_sendMessage_usingUDP(SOCKET serverSocket, const char dataBuffer[], const ST_sockaddr* clientSocketAddress)
{
    int dataTransferedBytes = sendto(serverSocket, dataBuffer, sizeof(dataBuffer), 0, clientSocketAddress, sizeof(*clientSocketAddress));
    if (dataTransferedBytes == 0)
    {
        cout << "Server::Server couldn't send data to " << ((ST_sockaddr_in*)clientSocketAddress)->sin_addr.S_un.S_addr << "\n";
        cout << "Server::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Server::Server has sent " << dataTransferedBytes << " bytes\n";
}

/*Send data using UDP*/
void server_sendData_usingUDP(SOCKET serverSOCKET, char* dataBuffer, const ST_sockaddr* clientSocketAddress)
{
    int dataTransferedBytes = sendto(serverSOCKET, dataBuffer, sizeof(dataBuffer), 0, clientSocketAddress, sizeof(*clientSocketAddress));
    if (dataTransferedBytes == 0)
    {
        cout << "Server::Server couldn't send data to " << ((ST_sockaddr_in*)clientSocketAddress)->sin_addr.S_un.S_addr << "\n";
        cout << "Server::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Server::Server has sent " << dataTransferedBytes << " bytes\n";
}

/*Receive message using UDP*/
char* server_receiveMessage_usingUDP(SOCKET serverSOCKET, int dataLength, ST_sockaddr* clientSocketAddress)
{
    char* dataBuffer = new char[dataLength];
    //int dataReceivedBytes = recvfrom(serverSOCKET, dataBuffer, 0, clientSocketAddress, sizeof(ST_sockaddr));
    int dataReceivedBytes = recvfrom(serverSOCKET, dataBuffer, sizeof(dataBuffer), 0, NULL, NULL);

    if (dataReceivedBytes == -1)
    {
        cout << "Server::Server failed to receive data\n";
        cout << "Server::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Server::Server has received " << dataReceivedBytes << " bytes of data from client\n";
    return dataBuffer;
}

/*Receive data using UDP*/
char* server_receiveData_usingUDP(SOCKET serverSOCKET, int dataLength, ST_sockaddr* clientSocketAddress)
{
    char* dataBuffer = new char[dataLength];
    int dataReceivedBytes = recvfrom(serverSOCKET, dataBuffer, sizeof(dataBuffer), 0, NULL, NULL);

    if (dataReceivedBytes == -1)
    {
        cout << "Server::Server failed to receive data\n";
        cout << "Server::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Server::Server has received " << dataReceivedBytes << " bytes of data from client\n";
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
    catch(exception e)
    {
        cerr << "Server::Server couldn't shutdown, please check the below given message\n";
        cerr << WSAGetLastError() << "\n";
    }
    system("pause");
}

/*Server transmit thread*/
DWORD WINAPI transmitMessages(LPVOID structPtr)
{
    ST_transmitCfg* st_transmitCfg = (ST_transmitCfg*)structPtr;
    while (true)
    {
        SetConsoleTextAttribute(hConsole, 1);
        WaitForSingleObject(mutexHandle, INFINITE);
        if (chatTerminationRequest == true)
        {
            std::cout << st_transmitCfg->hostName << "::" << st_transmitCfg->peerName << " has requested for exiting chat\n";
            std::cout << st_transmitCfg->hostName << "::Initiating termination sequence...\n";
            ReleaseMutex(mutexHandle);
            ResumeThread(mainHandle);
            break;
        }
        ReleaseMutex(mutexHandle);

        /*Clearing the transmit buffer before sending data*/
        memset(transmitBuffer, 0, BUFFER_SIZE);

        /*Reading message from terminal*/
        std::cout << st_transmitCfg->hostName << "::";
        cin.getline(transmitBuffer, BUFFER_SIZE);

        /*Sending message to client using TCP protocol*/
        server_sendMessage_usingTCP(st_transmitCfg->acceptedSocket, transmitBuffer);

        if (strcmp(transmitBuffer, "d7bEx8") == 0)
        {
            WaitForSingleObject(mutexHandle, INFINITE);
            std::cout << st_transmitCfg->hostName << "::" << st_transmitCfg->hostName << " has requested for exiting chat\n";
            std::cout << st_transmitCfg->hostName << "::Initiating termination sequence...\n";
            chatTerminationRequest = true;
            ReleaseMutex(mutexHandle);
            ResumeThread(mainHandle);
            break;
        }
    }
    return 0;
}

/*Server receive thread*/
DWORD WINAPI receiveMessages(LPVOID structPtr)
{
    ST_receiveCfg* st_receiveCfg = (ST_receiveCfg*)structPtr;
    while (true)
    {
        SetConsoleTextAttribute(hConsole, 2);
        WaitForSingleObject(mutexHandle, INFINITE);
        if (chatTerminationRequest == true)
        {
            std::cout << st_receiveCfg->hostName << "::Server has requested for exiting chat\n";
            std::cout << st_receiveCfg->hostName << "::Initiating termination sequence...\n";
            ResumeThread(mainHandle);
            ReleaseMutex(mutexHandle);
            break;
        }
        ReleaseMutex(mutexHandle);
        

        /*Clearing the message buffer to receive messages from client*/
        memset(receiveBuffer, 0, BUFFER_SIZE);

        /*Receiving messages from client*/
        strcpy_s(receiveBuffer, server_receiveMessage_usingTCP(st_receiveCfg->acceptedSocket));

        /*Displaying client messages on terminal*/
        
        std::cout << st_receiveCfg->peerName << "::" << receiveBuffer << "\n";

        if (strcmp(receiveBuffer, "d7bEx8") == 0)
        {
            WaitForSingleObject(mutexHandle, INFINITE);
            std::cout << st_receiveCfg->hostName << "::" << st_receiveCfg->peerName << " has requested for exiting chat\n";
            std::cout << st_receiveCfg->hostName << "::Initiating termination sequence...\n";
            chatTerminationRequest = true;
            ReleaseMutex(mutexHandle);
            ResumeThread(mainHandle);
            break;
        }
        SetConsoleTextAttribute(hConsole, 1);
    }
    return 0;
}

#endif