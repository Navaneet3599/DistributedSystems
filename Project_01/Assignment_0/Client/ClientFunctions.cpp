/*This source code was written by Navaneet Rao Dhage, as a part of Distributed Systems course work.*/

#ifndef CLIENTFUNCTIONS_CPP
#define CLIENTFUNCTIONS_CPP


/*Includes*/
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <ClientFunctions.h>

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
void client_errorDisplay();
void client_loadDLL();
SOCKET client_creatingSocket(int socketType, int commProtocol);
int client_connectingToServer(SOCKET clientSocket, ST_sockaddr* serverAddr);
void client_sendMessage_usingTCP(SOCKET acceptedSocket, const char messageBuffer[BUFFER_SIZE]);
void client_sendData_usingTCP(SOCKET acceptedSocket, char dataBuffer[], int noOfBytes);
char* client_receiveData_usingTCP(SOCKET serverSOCKET, int dataLength);
char* client_receiveMessage_usingTCP(SOCKET clientSocket);
void client_sendMessage_usingUDP(SOCKET clientSocket, const char dataBuffer[], const ST_sockaddr* serverSocketAddress);
void client_sendData_usingUDP(SOCKET clientSocket, char* dataBuffer, const ST_sockaddr* serverSocketAddress);
char* client_receiveMessage_usingUDP(SOCKET clientSocket, int dataLength, ST_sockaddr* serverSocketAddress);
char* client_receiveData_usingUDP(SOCKET clientSocket, int dataLength, ST_sockaddr* serverSocketAddress);
void client_disconnect(SOCKET acceptedSocket);
DWORD WINAPI transmitMessages(LPVOID structPtr);
DWORD WINAPI receiveMessages(LPVOID ptr);

/*Function Definitions*/
/*******Client Functions*******/




int main()
{
    /*Client chat bot implementation*/
    char hostName[20];
    char peerName[20] = "";
    ST_transmitCfg st_transmitCfg;
    ST_receiveCfg st_receiveCfg;
    DWORD transmitThreadID, receiveThreadID;
    HANDLE transmitHandle, receiveHandle;
    ST_sockaddr_in serverSocketAddress = { 0 };
    
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    /*Loading winsock library*/
    client_loadDLL();

    /*Creating client socket*/
    SOCKET clientSocket = client_creatingSocket(SOCK_STREAM, IPPROTO_TCP);
    //SOCKET clientSocket = client_creatingSocket(SOCK_DGRAM, IPPROTO_UDP);

    /*Specifying server socket address*/
    serverSocketAddress.sin_family = AF_INET;
    serverSocketAddress.sin_port = htons(8080);
    if (inet_pton(AF_INET, "127.0.0.1", &serverSocketAddress.sin_addr) == 1) cout << "----------Server address specification ---->VALID\n";
    else if (inet_pton(AF_INET, "127.0.0.1", &serverSocketAddress.sin_addr) == 0) cout << "----------Server address specification ---->INVALID\n";
    else if (inet_pton(AF_INET, "127.0.0.1", &serverSocketAddress.sin_addr) == -1) cout << "----------Server address specification ---->OTHER ERRORS\n" << WSAGetLastError() << "\n";

    if (client_connectingToServer(clientSocket, (ST_sockaddr*)(&serverSocketAddress)) != SOCKET_ERROR)
    {
        system("cls");
        cout << "Do you wish to be named anything other than \"Client\"? \n[Y/N]";
        cin >> hostName;

        if (strcmp(hostName, "Y") == 0)
        {
            cout << "Enter your name: ";
            cin >> hostName;
            cout << "\n";
        }
        else strcpy_s(hostName, "Client");

        strcpy_s(peerName, client_receiveMessage_usingTCP(clientSocket));
        client_sendMessage_usingTCP(clientSocket, hostName);

        st_transmitCfg.acceptedSocket = clientSocket;
        strcpy_s(st_transmitCfg.hostName, hostName);

        mutexHandle = CreateMutex(NULL, false, NULL);

        st_receiveCfg.acceptedSocket = clientSocket;
        strcpy_s(st_receiveCfg.hostName, hostName);
        strcpy_s(st_receiveCfg.peerName, peerName);

        system("cls");
        cout << "Initiating chat sequence with " << peerName << "...\n";
        cout << "Type \"d7bEx8\" for exiting the chat(case sensitive)\n";

        transmitHandle = CreateThread(NULL, 0, transmitMessages, (LPVOID)(&st_transmitCfg), 0, &transmitThreadID);
        if (transmitHandle != 0)
            SetPriorityClass(transmitHandle, THREAD_PRIORITY_NORMAL);
        receiveHandle = CreateThread(NULL, 0, receiveMessages, (LPVOID)(&st_receiveCfg), 0, &receiveThreadID);
        if (receiveHandle != 0)
            SetPriorityClass(receiveHandle, THREAD_PRIORITY_ABOVE_NORMAL);

        mainHandle = GetCurrentThread();
        SuspendThread(mainHandle);
    }
    Sleep(100);
    if (chatTerminationRequest == true) client_disconnect(clientSocket);
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
    clientSocket = socket(  AF_INET,            //This code is created only for IP address families
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
void client_sendMessage_usingTCP(SOCKET acceptedSocket, const char messageBuffer[BUFFER_SIZE])
{
    int dataTransferedBytes = send(acceptedSocket, messageBuffer, BUFFER_SIZE, 0);

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
char* client_receiveData_usingTCP(SOCKET serverSOCKET, int dataLength)
{
    char* dataBuffer = new char[dataLength];
    int dataReceivedBytes = recv(serverSOCKET, dataBuffer, dataLength, 0);

    if (dataReceivedBytes == SOCKET_ERROR)
    {
        cout << "Client::Client failed to receive data";
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Client::Client has received " << dataReceivedBytes << " bytes of data from client\n";
    return dataBuffer;
}

/*Receive message using TCP*/
char* client_receiveMessage_usingTCP(SOCKET clientSocket)
{
    char* dataBuffer = new char[BUFFER_SIZE];
    int dataReceivedBytes = recv(clientSocket, dataBuffer, BUFFER_SIZE, 0);

    if (dataReceivedBytes == SOCKET_ERROR)
    {
        cout << "Client::Client failed to receive data";
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    //else cout << "Client::Client has received " << dataReceivedBytes << " bytes of data from client\n";

    return dataBuffer;
}

/*Send message using UDP*/
void client_sendMessage_usingUDP(SOCKET clientSocket, const char dataBuffer[], const ST_sockaddr* serverSocketAddress)
{
    int dataTransferedBytes = sendto(clientSocket, dataBuffer, sizeof(dataBuffer), 0, serverSocketAddress, sizeof(*serverSocketAddress));
    if (dataTransferedBytes == 0)
    {
        cout << "Client::Client couldn't send data to " << ((ST_sockaddr_in*)serverSocketAddress)->sin_addr.S_un.S_addr << "\n";
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Client::Client has sent " << dataTransferedBytes << " bytes\n";
}

/*Send data using UDP*/
void client_sendData_usingUDP(SOCKET clientSocket, char* dataBuffer, const ST_sockaddr* serverSocketAddress)
{
    int dataTransferedBytes = sendto(clientSocket, dataBuffer, sizeof(dataBuffer), 0, serverSocketAddress, sizeof(*serverSocketAddress));
    if (dataTransferedBytes == 0)
    {
        cout << "Client::Client couldn't send data to " << ((ST_sockaddr_in*)serverSocketAddress)->sin_addr.S_un.S_addr << "\n";
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Client::Client has sent " << dataTransferedBytes << " bytes\n";
}

/*Receive message using UDP*/
char* client_receiveMessage_usingUDP(SOCKET clientSocket, int dataLength, ST_sockaddr* serverSocketAddress)
{
    char* dataBuffer = new char[dataLength];
    //int dataReceivedBytes = recvfrom(clientSocket, dataBuffer, 0, serverSocketAddress, sizeof(ST_sockaddr));
    int dataReceivedBytes = recvfrom(clientSocket, dataBuffer, sizeof(dataBuffer), 0, NULL, NULL);

    if (dataReceivedBytes == -1)
    {
        cout << "Client::Client failed to receive data\n";
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Client::Client has received " << dataReceivedBytes << " bytes of data from client\n";
    return dataBuffer;
}

/*Receive data using UDP*/
char* client_receiveData_usingUDP(SOCKET clientSocket, int dataLength, ST_sockaddr* serverSocketAddress)
{
    char* dataBuffer = new char[dataLength];
    int dataReceivedBytes = recvfrom(clientSocket, dataBuffer, sizeof(dataBuffer), 0, NULL, NULL);

    if (dataReceivedBytes == -1)
    {
        cout << "Client::Client failed to receive data\n";
        cout << "Client::Please check the below message for more information\n";
        cout << WSAGetLastError() << "\n";
    }
    else cout << "Client::Client has received " << dataReceivedBytes << " bytes of data from client\n";
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
    system("pause");
}

/*Client transmit thread*/
DWORD WINAPI transmitMessages(LPVOID structPtr)
{
    ST_transmitCfg* st_transmitCfg = (ST_transmitCfg*)structPtr;
    while (true)
    {
        SetConsoleTextAttribute(hConsole, 2);
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
        client_sendMessage_usingTCP(st_transmitCfg->acceptedSocket, transmitBuffer);

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

/*Client receive thread*/
DWORD WINAPI receiveMessages(LPVOID structPtr)
{
    ST_receiveCfg* st_receiveCfg = (ST_receiveCfg*)structPtr;
    while (true)
    {
        SetConsoleTextAttribute(hConsole, 1);
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
        strcpy_s(receiveBuffer, client_receiveMessage_usingTCP(st_receiveCfg->acceptedSocket));

        /*Displaying client messages on terminal*/
        
        std::cout << st_receiveCfg->peerName << "::" << receiveBuffer << "\n";

        if ((strcmp(receiveBuffer, "d7bEx8") == 0) || (strcmp(transmitBuffer, "d7bEx8") == 0))
        {
            WaitForSingleObject(mutexHandle, INFINITE);
            std::cout << st_receiveCfg->hostName << "::" << st_receiveCfg->peerName << " has requested for exiting chat\n";
            std::cout << st_receiveCfg->hostName << "::Initiating termination sequence...\n";
            chatTerminationRequest = true;
            ReleaseMutex(mutexHandle);
            ResumeThread(mainHandle);
            break;
        }
        SetConsoleTextAttribute(hConsole, 2);
    }
    return 0;
}


#endif