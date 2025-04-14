#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unordered_map>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <string>
#include <chrono>
#include <ctime>
#include <set>

#define PORT 8080
#define ENABLE_LOGS true
#define BUFFER_SIZE 1024
#define NUMBER_OF_CONNECTIONS 3
#define NUMBER_OF_REQUESTS 3
#define SERVER_IP "127.0.0.1"
#define BROADCAST_ADDR "192.168.5.255"
#define MY_NODE_ID "1"

#pragma pack(1)

void log(const char* message);

class Message
{
    public:
        bool isACK = true;/*true->send acknowledgement/ false->receive acknowledgement*/
        char processID[4];
        unsigned char eventID = 0;
        
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
    Node* front;
    Node* rear;

public:
    PriorityQueue()
    {   // Constructor
        front = nullptr;
        rear = nullptr;
    }

    bool isQueueEmpty()
    {
        return (front == nullptr);
    }

    // Comparison logic: returns true if node1 has higher priority than node2.
    bool checkIf_N1_Before_N2(const Node& node1, const Node& node2)
    {
        if ((node1.msg.eventID < node2.msg.eventID) ||
            ((node1.msg.eventID == node2.msg.eventID) && (node1.msg.processID < node2.msg.processID)))
            return true;
        else
            return false;
    }

    void enqueue(const Message& msg)
    {
        Node* newNode = new Node(msg);

        if (front == nullptr)
        {
            log("Inserting first element in priority queue.");
            front = newNode;
            rear = newNode;
            return;
        }

        Node* current = front;
        Node* previous = nullptr;

        while (current != nullptr && checkIf_N1_Before_N2(*current, *newNode))
        {
            previous = current;
            current = current->next;
        }

        if (previous == nullptr)
        {
            // Insert at the front
            newNode->next = front;
            front = newNode;
        }
        else
        {
            // Insert in the middle or end
            previous->next = newNode;
            newNode->next = current;

            if (current == nullptr)  // If inserted at the end, update rear
                rear = newNode;
        }
    }

    void printQueue()
    {   // For debugging
        Node* current = front;
        if (current == nullptr)
        {
            std::cout << "Queue is empty" << std::endl;
            return;
        }

        std::cout << "front -> ";
        while (current != nullptr)
        {
            std::cout << "(" << current->msg.processID << ", " << current->msg.eventID << ") -> ";
            current = current->next;
        }
        std::cout << "rear" << std::endl;
    }

    Message popFront()
    {
        if (front == nullptr)
        {
            throw std::runtime_error("Queue is empty!");
        }

        Message msg = front->msg;
        Node* temp = front;
        front = front->next;

        if (front == nullptr)  // If queue is now empty
            rear = nullptr;

        delete temp;
        return msg;
    }

    Node peekFront()
    {
        if (front == nullptr)
            throw std::runtime_error("Queue is empty!");
        return *front;
    }

    ~PriorityQueue()
    {   // Destructor
        while (front != nullptr)
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


int clientPort = 0;
volatile unsigned char currentEventID = 0;
int noOfRequests = 3;
std::unordered_map<std::string, Node*>MyRequests;
PriorityQueue RequestQueue;
Queue JobQueue;
char* serverIPs[NUMBER_OF_CONNECTIONS] = {"127.0.0.1", "127.0.0.2"};

void log(const char* message)
{
#if ENABLE_LOGS
    std::cout << message << std::endl;
#else
#endif
}

/*UDP send message*/
/*If sending Msg.REQ then use a set and retransmit the message until 4 different ACKs are received*/
/*If sending Msg.ACK then use a set and retransmit the message until 4 different ACKs are received*/
void sendMessage(int clientSocket, Message msg)
{
    std::set<char*> recepientSet;
    char bytes[13];
    int retryCount = 0;
    char* currentRound = msg.processID + '.' + msg.eventID;
    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);
    clientAddr.sin_family = AF_INET;                                            /*Send messages to IPv4 family*/
    clientAddr.sin_port = clientPort;                                                 /*Send messages via clientPort(assigned by kernel) since it supports both TCP and UDP*/
    int status = inet_pton(AF_INET, BROADCAST_ADDR, &clientAddr.sin_addr);      /*Send messages to all IP address in LAN*/
    if(status < 1)
    {
        std::cerr << "Broadcast address specification failed at line number " << __LINE__ << std::endl;
        exit(EXIT_FAILURE);
    }

    while(true)
    {
        retryCount++;
        int sentbytes = sendto(clientSocket, &msg, sizeof(msg), 0, (const struct sockaddr *)&clientAddr, sizeof(clientAddr));
        if(sentbytes < 0)
            continue;

        auto start = std::chrono::steady_clock::now();

        if(!msg.isACK)
        {
            while(recepientSet.size() < 0)
            {
                recvfrom(clientSocket, &bytes, 13, 0, (struct sockaddr*)&clientAddr, &addrLen);
                if(strncmp(bytes, currentRound, strlen(currentRound)) == 0)
                {
                    recepientSet.insert(bytes);
                }
                else
                {
                    std::cerr << "Sequence is lost for currentRound" << std::endl;
                }

                if(recepientSet.size() >= NUMBER_OF_CONNECTIONS)
                    return;

                // Get the current time
                auto now = std::chrono::steady_clock::now();

                // Calculate elapsed time in seconds
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();

                if(elapsed >= 3)
                {
                    std::cerr << "Timeout for " << currentRound << std::endl;
                    break;
                }
            }
        }
        else
        {
            while(true)
            {
                recvfrom(clientSocket, &bytes, 13, 0, (struct sockaddr*)&clientAddr, &addrLen);
                if(strncmp(bytes, currentRound, strlen(currentRound)) == 0)
                {
                    return;
                }
                else
                {
                    std::cerr << "Sequence is lost for currentRound" << std::endl;
                }

                // Get the current time
                auto now = std::chrono::steady_clock::now();

                // Calculate elapsed time in seconds
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();

                if(elapsed >= 3)
                {
                    std::cerr << "Timeout for " << currentRound << std::endl;
                    break;
                }
            }
        }
        if(retryCount == 3)
        {
            std::cerr << "Reached maximum number of retries(3) for the event ACK->" << currentRound << std::endl;
            break;
        }
    }
}


/*UDP receive message*/
/*If received message is Msg.REQ then send ACK and update in received messages*/
/*If received message is Msg.ACK then send ACK and update in received messages*/
Message receiveMessage(int serverSocket)
{
    struct sockaddr_in serverAddr{};
    Message msg("ACK");
    socklen_t addrLen = sizeof(serverAddr);
    int retryCount = 0;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = PORT;
    std::string confirmationMessage;
    int status = inet_pton(AF_INET, BROADCAST_ADDR, &serverAddr.sin_addr);      /*Send messages to all IP address in LAN*/
    if(status < 1)
    {
        std::cerr << "Broadcast address specification failed at line number " << __LINE__ << std::endl;
        exit(EXIT_FAILURE);
    }

    while(true)
    {
        if(retryCount >= 3)
        {
            std::cerr << "Max retry count reached for receiving message, aborting program execution" << std::endl;
            exit(EXIT_FAILURE);
        }
        int receivedBytes = recvfrom(serverSocket, &msg, sizeof(msg), 0, (struct sockaddr*)&serverAddr, &addrLen);
        if(receivedBytes < 0)
        {
            std::cerr << "error is receiving message" << std::endl;
            retryCount++;
            continue;
        }
        else
        {
            confirmationMessage = msg.processID+'.'+msg.eventID+'A'+'C'+'K';
            sendto(serverSocket, &confirmationMessage, strlen(confirmationMessage.c_str()), 0, (struct sockaddr*)&serverAddr, addrLen);
        }
    }
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

/*For requesting REQs and sending ACKs*/
/*JobQueue.selfReq --> print*/
/*JobQueue.!selfReq --> multiCastACK*/
void* ClientThread(void* arg)
{
    int clientSocket = *((int*)arg);
    while(true)
    {
        if(JobQueue.isQueueEmpty())
            continue;
        else
        {
            Message msg = JobQueue.dequeue();
            if(strcmp(msg.processID, MY_NODE_ID) != 0)
            {
                msg.isACK = true;
                sendMessage(clientSocket, msg);
            }
            std::cout << MY_NODE_ID << ":" << msg.processID << "." << msg.eventID << std::endl;
        }
    }
}

/*Listening to responses, creating hashes for received messages and checking if they are required or not*/
/*Increments counters for required requests*/
/*Req.self --> increment self counter*/
/*Req.!self --> enqueue into priority queue*/
/*Ack.self --> increment counter[REDUNDANT]*/
/*Ack.!self --> discard*/
void* ServerThread(void* arg)
{
    int serverSocket = *((int*)arg);
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    Message msg("ACK");
    while (true) {
        Message msg = receiveMessage(serverSocket);
        /*Check if the process ID belongs to this node*/
        /*create a hash and check if the message is in hashmap*/
        /*If the message is of type ACK then check if the server ID */
        if(msg.isACK == false)
        {
            if(strcmp(msg.processID, MY_NODE_ID) == 0)
            {
                Node* myRequest = MyRequests[msg.processID+'.'+msg.eventID];
                if(myRequest->ackCount < 3)
                    myRequest->ackCount++;
            }
            else
            {
                RequestQueue.enqueue(msg);
            }
        }
    }
    return nullptr;
}

/*For maintaining priority queue and moving the message to application queue*/
/*Req.self --> wait till counter reaches max and then place into job queue*/
/*Req.!self--> Place in JobQueue*/
void* MessageDelivery(void* ptr)
{
    while(true)
    {
        if(RequestQueue.isQueueEmpty())
            continue;
        else
        {
            Node topNode = RequestQueue.peekFront();
            if((strcmp(topNode.msg.processID, MY_NODE_ID) == 0) && (topNode.ackCount >= 3))
            {
                JobQueue.enqueue(RequestQueue.popFront());
            }
            else if(strcmp(topNode.msg.processID, MY_NODE_ID) != 0)
            {
                JobQueue.enqueue(RequestQueue.popFront());
            }
        }
    }
    return nullptr;
}

/*This will make the program to wait for starting of next minute*/
void waitForMinute()
{
    using namespace std::chrono;

    // Get the current system time
    auto now = system_clock::now();
    std::time_t now_time = system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);

    int start_minute = local_time->tm_min;  // Capture the starting minute

    std::cout << "Loop started at minute: " << start_minute << std::endl;

    while (true) {
        now = system_clock::now();
        now_time = system_clock::to_time_t(now);
        local_time = std::localtime(&now_time);

        if (local_time->tm_min != start_minute) {
            std::cout << "Invoking threads now..." << std::endl;
            break;
        }
    }
}

/*For thread spawning and management*/
/*Event generation*/
int main() {
    int clientSocket, serverSocket;
    struct sockaddr_in serverAddr{};
    char buffer[BUFFER_SIZE];

    serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if(serverSocket < 0)
    {
        perror("Server socket creation failed");
        return -1;
    }
    int broadcastEnable = 1;
    if(setsockopt(serverSocket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0)
    {
        perror("Error in setting broadcast option");
        exit(EXIT_FAILURE);
    }

    clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if(clientSocket < 0)
    {
        perror("Client socket creation failed");
        return -1;
    }
    if(setsockopt(clientSocket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0)
    {
        perror("Error in setting broadcast option");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    if(getsockname(clientSocket, (struct sockaddr*)&clientAddr, &addrLen) == -1) {
        std::cerr << "getsockname() failed\n";
        close(clientSocket);
        return -1;
    }
    clientPort = clientAddr.sin_port;
    std::cout << "Client is hosted on the port: " << clientPort << std::endl;

    serverAddr.sin_family = AF_INET;            //Server address will be used for connecting to IPv4 family
    serverAddr.sin_addr.s_addr = INADDR_ANY;    //Server will listen to messages coming from any address
    serverAddr.sin_port = htons(PORT);          //Server is hosted on the port 8080

    if (bind(serverSocket, (const struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("Bind failed");
        close(serverSocket);
        return -1;
    }

    std::cout << "Server is hosted on the port: " << PORT << std::endl;

    pthread_t clientThread, serverThread, messageDelivery;
    int clientThreadID, serverThreadID, messageDeliveryID;

    using namespace std::chrono;

    waitForMinute();

    pthread_create(&clientThread, nullptr, ClientThread, (void*)(&clientSocket));
    pthread_create(&serverThread, nullptr, ServerThread, (void*)(&serverSocket));
    pthread_create(&messageDelivery, nullptr,MessageDelivery, NULL);

    while(true)
    {
        if(RequestQueue.isQueueEmpty())
        {
            Message msg("REQ");
            strcpy(msg.processID, MY_NODE_ID);
            currentEventID++;
        }

        if(currentEventID == 3)
        {
            break;
        }
    }
    while((noOfRequests > 0) && RequestQueue.isQueueEmpty() && JobQueue.isQueueEmpty());

    pthread_join(messageDelivery, nullptr);
    pthread_join(clientThread, nullptr);
    pthread_join(serverThread, nullptr);

    close(serverSocket);
    close(clientSocket);
    
    std::cout << "Press any key to end the program" << std::endl;
    std::cin.get();
    return 0;
}