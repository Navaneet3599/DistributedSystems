#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unordered_map>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <string>

#define PORT 8080
#define ENABLE_LOGS true
#define NUMBER_OF_CLIENTCONNECTIONS 3

#pragma pack(1)

void log(const char* message);
std::unordered_map<std::string, Node*>MyRequests;

class Message
{
    public:
        bool isACK = true;/*true->send acknowledgement/ false->receive acknowledgement*/
        unsigned char processID[4];
        unsigned char eventID = 0;
        
    Message() = default;
    Message(char* type)
    {
        if(strcmp(type, "ACK") == 0)
            isACK = true;
        else if(strcmp(type, "REQ") == 0)
            isACK = false;
        else
        {
            std::cerr << "Error: Invalid request type at line number " << __LINE__ << "." << std::endl;
            std::cerr << "Error: Deleting current object." << std::endl;
            this->~Message();
            //exit(EXIT_FAILURE);
        }
    }
};
struct Node
{
    Node* next;
    unsigned char ackCount = 0;//If counter reaches number of clients
    Message msg;

    Node(const Message& m) : next(nullptr), ackCount(0), msg(m){}
};

/*Use this for managing generated nodes*/
class PriorityQueue
{
    private:
        Node* rear;
        Node* front;

    public:
        PriorityQueue()
        {//Constructor
            rear = nullptr;
            front = nullptr;
        }

        bool checkIf_N1_Before_N2(Node& node1, Node& node2)
        {
            if((node1.msg.eventID < node2.msg.eventID) || ((node1.msg.eventID == node2.msg.eventID) && (node1.msg.processID < node2.msg.processID)))
                return true;
            else// if((node1.msg.eventID > node2.msg.eventID) || ((node1.msg.eventID == node2.msg.eventID) && (node1.msg.processID > node2.msg.processID)))
                return false;
        }

        void insertMessage(Message msg)
        {//Inserting message into priority queue
            Node* newNode = new Node(msg);

            if(front == nullptr)
            {
                log("Inserting first element in priority queue.");
                front = newNode;
                rear = newNode;
            }
            else
            {
                Node* currentNode = rear;
                Node* previousNode = rear;
                while(currentNode != nullptr)
                {
                    if(checkIf_N1_Before_N2(*currentNode, *newNode))
                        break;
                    previousNode = currentNode;
                    currentNode = currentNode->next;
                }

                previousNode->next = newNode;
                if(currentNode == nullptr)
                    newNode = front;
                else
                    newNode->next = currentNode;
            }
        }

        void printQueue()
        {//Printing queue for debugging
            Node* currentNode = rear;
            if(currentNode == nullptr)
            {
                std::cout << "Queue is empty" << std::endl;
            }
            else
            {
                std::cout << "rear -> ";
                while(currentNode != nullptr)
                {
                    std::cout << "(" << currentNode->msg.processID << ", " << currentNode->msg.eventID << ") -> ";
                    currentNode = currentNode->next;
                }
                std::cout << "front" << std::endl;
            }
        }

        void popFront()
        {
            Node* currentNode = front;
            if(currentNode->msg.isACK)
            {

            }
        }

        ~PriorityQueue()
        {//Destructor
            while(front != nullptr)
            {
                Node* temp = front;
                front = front->next;
                delete temp;
            }
        }
};

class Queue
{
    private:
        Node* front = nullptr;  // Points to the first (oldest) element
        Node* rear = nullptr;   // Points to the last (newest) element

    public:

        bool isQueueEmpty()
        {
            return front == nullptr;
        }

        // Enqueue — add to the rear
        void enqueue(Message msg)
        {
            Node* newNode = new Node(msg);
            if (rear == nullptr)
            {
                front = rear = newNode;
            }
            else
            {
                rear->next = newNode;
                rear = newNode;
            }
        }

        // Dequeue — remove from the front
        Message dequeue()
        {
            Node* temp = front;
            Message msg = temp->msg;

            front = front->next;

            if (front == nullptr)
                rear = nullptr;

            delete temp;
            return msg;
        }

        ~Queue()
        {
            while (!isQueueEmpty())
                dequeue();
        }
};


const char myNodeID = 0;
volatile unsigned char currentEventID = 0;

void log(const char* message)
{
#if ENABLE_LOGS
    std::cout << message << std::endl;
#else
#endif
}

/*Extracting node ID, send 0xFFFF if server IP is invalid*/
int extractNodeID(const char* serverIP)
{
    char* nodeID = (char*)calloc(4, 1);
    nodeID[strlen(nodeID)] = '\0';
    unsigned char startIndex;
    unsigned char pattern = '.';
    int nodeId = 0;
    for(int i = strlen(serverIP) - 1; i > 0; i--)
    {
        if((i == 0) && (serverIP[0] != pattern))
            std::cerr << "Error: Invalid server IP" << std::endl;
        if(serverIP[i] == pattern)
        {
            for(int j = i+1; serverIP[j] != '\0'; j++)
                nodeID[j-(i+1)] = serverIP[j];
            break;
        }
    }
    
    if(*nodeID != '\0')
        for(int i = 0; i < strlen(nodeID); i++)
            nodeId = nodeId*10 + (nodeID[i] - 48);
    else
        nodeId = 0xFFFF;
    return nodeId;
}

/*For monitoring local events and requesting ACKs*/
void* CommThread(void* ptr)
{

    return nullptr;
}

/*For maintaining priority queue and moving the message to application queue*/
void* MessageDelivery(void* ptr)
{

    return nullptr;
}

/*For thread spawning and management*/
/*
int main()
{
    int serverSocket, clientSockets[NUMBER_OF_CLIENTCONNECTIONS];
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSocket == -1)
    {
        std::cerr << "Server socket creation failed!" << std::endl;
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Bind failed!" << std::endl;
        return 1;
    }

    if (listen(serverSocket, NUMBER_OF_CLIENTCONNECTIONS) < 0)
    {
        std::cerr << "Listen failed!" << std::endl;
        return 1;
    }

    int clientSocket = accept(serverSocket, (struct sockaddr*)&client_addr, &addr_len);
    char buffer[16];
    ssize_t n = recvfrom(serverSocket, buffer, 15, )

    
    int noOfClients = 0;
    while(true)
    {
        clientSockets[noOfClients] = accept(serverSocket, (struct sockaddr*)&client_addr, &addr_len);
        noOfClients += 1;

        if(noOfClients == NUMBER_OF_CLIENTCONNECTIONS)
        {
            log("All clients are connected");
            break;
        }
    }

    pthread_t commThread, messageDeliveryThread;

    std::cin.get();
}*/

int main()
{
    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if(serverSocket == -1)
    {
        std::cerr << "Server socket creation failed!!" << std::endl;
    }

    struct sockaddr_in serverAddr, clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if(bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        std::cerr << "Server socket binding failed!!" << std::endl;
    }
    else
    {
        std::cout << "UDP server is hosted on port:" << PORT << std::endl;
    }

    char tempBuffer[6] = "Hello";
    while(true)
    {
        sendto(serverSocket, tempBuffer, 6, 0, (struct sockaddr*)&clientAddr, clientAddrLen);
        std::cout << "." <<std::endl;
        if(recvfrom(serverSocket, tempBuffer, 6, MSG_DONTWAIT, (struct sockaddr*)&clientAddr, &clientAddrLen) > 0)
        {
            if(strcmp(tempBuffer, "World") == 0)
                break;
            else
                std::cout << "Received: \"" << tempBuffer << "\"";
        }
    }

    std::cout << "Communication confirmed" << std::endl;
    close(serverSocket);

    std::cin.get();
}