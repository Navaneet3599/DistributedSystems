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
#include <mutex>
#include <atomic>
#include <thread>

#pragma pack(1)

#define ENABLE_LOGS true
#define MULTICAST_ADDR "239.0.0.1"
#define MY_IP_ADDR "192.168.5.125"  //Will be updated for the node
#define SEND_PORT 12345
#define RECEIVE_PORT SEND_PORT
#define MY_NODE_ID 125              //Will be updated for the node
#define NUMBER_OF_CONNECTIONS 3
#define NUMBER_OF_REQUESTS 3
#define RETRY_LIMIT 3
#define TIMEOUT_DURATION 3
#define BUFFER_SIZE 32


int currentEventID = 0;
// Mutex for synchronizing shared resources
std::mutex queueMutex;
std::mutex requestMutex;
std::mutex jobMutex;
std::mutex eventMutex;



void log(std::string message);

typedef struct
{
    int socket;
    struct sockaddr_in addr;
}argStruct;

class Message
{
    public:
        bool isACK = true;/*true->send acknowledgement/ false->receive acknowledgement*/
        unsigned short int processID = MY_NODE_ID;
        unsigned short int eventID = currentEventID;
        
    Message() = default;
    Message(std::string type)
    {
        if(type == "ACK")
            isACK = true;
        else if(type == "REQ")
            isACK = false;
        else
        {
            std::string errorMessage = "Error: Invalid request type at line number " + std::to_string(__LINE__) + ".";
            throw std::invalid_argument(errorMessage);
        }
    }
};

struct Node
{
    Node* next;
    int ackCount = 0;//If counter reaches number of clients
    Message msg;

    //Constructor for Node data structure
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

    //void enqueue(const Message& msg)
    void enqueue(Node* newNode)
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        //Node* newNode = new Node(msg);

        if (front == nullptr)
        {
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
            std::cout << "(" << current->msg.processID << ", " << current->msg.eventID << "[" << current->ackCount << "]) -> ";
            current = current->next;
        }
        std::cout << "rear" << std::endl;
    }

    Message popFront()
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (front == nullptr)
        {
            throw std::runtime_error("Queue is empty!");
        }

        Message msg = front->msg;
        Node* temp = front;
        front = front->next;

        if (front == nullptr)  // If queue is now empty
            rear = nullptr;

        if(temp != nullptr)
            delete temp;
        return msg;
    }

    Node* peekFront()
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        return front;
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
        std::lock_guard<std::mutex> lock(jobMutex);
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
    Message* dequeue()
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        if(!isQueueEmpty())
        {
            Node* temp = front;
            Message* msg = new Message("ACK");
            *msg = temp->msg;
            front = front->next;

            if (front == nullptr)
                rear = nullptr;
            if(temp != nullptr)
                delete temp;
            return msg;
        }
        return nullptr;
    }

    ~Queue()
    {
        while (!isQueueEmpty())
            dequeue();
    }
};



int noOfRequests = 0;
std::unordered_map<std::string, Node*>MyRequests;
std::atomic<bool> exitThread(false);
PriorityQueue RequestQueue;
Queue JobQueue;

int updateEvent(std::string type)
{
    std::lock_guard<std::mutex> lock(eventMutex);
    //currentEventID++;
    std::cout << MY_NODE_ID << ":" << MY_NODE_ID << "." << currentEventID <<  '\t' << type << std::endl;
    return currentEventID;
}

int updateReqEvent(Message msg)
{
    std::lock_guard<std::mutex> lock(eventMutex);
    if(msg.eventID > currentEventID)
        currentEventID = msg.eventID;
    //currentEventID++;
    if(msg.processID == MY_NODE_ID)
        std::cout << MY_NODE_ID << ":" << MY_NODE_ID << "." << msg.eventID <<  "\tRECEIVE(SELF ACK)" << std::endl;
    else
        std::cout << MY_NODE_ID << ":" << msg.processID << "." << msg.eventID <<  "\tRECEIVE" << std::endl;
    return currentEventID;
}

void pushHashMap(Node* newNode)
{
    std::lock_guard<std::mutex> lock(requestMutex);
    std::string key = std::to_string(newNode->msg.processID) + "." + std::to_string(newNode->msg.eventID);
    MyRequests[key] = newNode;
}

void popHashMap(std::string key)
{
    std::lock_guard<std::mutex> lock(requestMutex);
    auto node = MyRequests.find(key);
    if(node != MyRequests.end()) MyRequests.erase(key);
}

Node* peekHashMap(std::string key)
{
    std::lock_guard<std::mutex> lock(requestMutex);
    auto node = MyRequests.find(key);
    if(node != MyRequests.end()) return node->second;
    return nullptr;
}

void log(std::string message)
{
    #if ENABLE_LOGS
    std::cout << "--" << message << std::endl;
    #endif
}

/*UDP send message*/
/*If sending Msg.REQ then use a set and retransmit the message until 4 different ACKs are received*/
/*If sending Msg.ACK then use a set and retransmit the message until 4 different ACKs are received*/
#if 1
void sendMessage(argStruct mcast_sender, Message msg)
{
    msg.eventID = updateEvent("SEND");
    int sentbytes = sendto(mcast_sender.socket, &msg, sizeof(msg), 0, (sockaddr*)&mcast_sender.addr, sizeof(struct sockaddr));
    if(sentbytes < 0)
    {
        perror("Error in sending data");
        exit(EXIT_FAILURE);
    }
}
#else
void sendMessage(argStruct mcast_sender, Message msg)
{
    int retries = 0;

    std::string key = std::to_string(msg.processID) + "." + std::to_string(msg.eventID);

    while (retries < RETRY_LIMIT) 
    {
        int sentbytes = sendto(mcast_sender.socket, &msg, sizeof(msg), 0, (sockaddr*)&mcast_sender.addr, sizeof(struct sockaddr));
        if(sentbytes < 0)
        {
            perror("Error in sending data");
            exit(EXIT_FAILURE);
        }

        log("Sent message---> "+std::to_string(msg.processID)+"."+std::to_string(msg.eventID));

        auto start = std::chrono::steady_clock::now();

        while (true) 
        {
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                auto it = MyRequests.find(key);
                if (it != MyRequests.end() && it->second->ackCount >= NUMBER_OF_CONNECTIONS) 
                {
                    log("Received enough ACKs for " + key);
                    return;  // Success!
                }
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();

            if (elapsed >= TIMEOUT_DURATION) 
            {
                log("Timeout waiting for ACKs for " + key + ", retrying...");
                retries++;
                break;  // Exit inner loop, retry sending.
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100*(1 << retries)));  // Avoid busy waiting
        }
    }

    log("Failed to receive enough ACKs for " + key + " after " + std::to_string(RETRY_LIMIT) + " retries.");
}
#endif


/*UDP receive message*/
/*If received message is Msg.REQ then send ACK and update in received messages*/
/*If received message is Msg.ACK then send ACK and update in received messages*/
Message receiveMessage(argStruct mcast_receiver)
{
    int receivedBytes = -1;
    Message msg;
    while(true)
    {
        sockaddr_in senderAddr{};
        socklen_t senderAddrLen = sizeof(senderAddr);
        receivedBytes = recvfrom(mcast_receiver.socket, &msg, sizeof(msg), 0, (sockaddr*)&senderAddr, &senderAddrLen);
        if(receivedBytes < 0) perror("Error in receiving message");
        else break;
    }
    return msg;
}

/*For requesting REQs and sending ACKs*/
/*JobQueue.selfReq --> print*/
/*JobQueue.!selfReq --> multiCastACK*/
void* ClientThread(void* arg)
{
    while(true)
    {
        Message* msg = JobQueue.dequeue();
        if(msg != nullptr)
        {
            if(msg->processID != MY_NODE_ID)
            {
                msg->isACK = true;
                sendMessage(*((argStruct*)arg), *msg);
                msg->eventID = updateEvent("SEND");
            }
            else
            {
                updateEvent("PROCESS");
            }
        }
        if(msg != nullptr)
            delete msg;
        if(exitThread && JobQueue.isQueueEmpty() && RequestQueue.isQueueEmpty())
            break;
    }
    return nullptr;
}

/*Listening to responses, creating hashes for received messages and checking if they are required or not*/
/*Increments counters for required requests*/
/*Req.self --> increment self counter*/
/*Req.!self --> enqueue into priority queue*/
/*Ack.self --> increment counter[REDUNDANT]*/
/*Ack.!self --> discard*/
void* ServerThread(void* arg)
{
    while (true) {
        Message msg = receiveMessage(*((argStruct*)arg));
        /*Check if the process ID belongs to this node*/
        /*create a hash and check if the message is in hashmap*/
        /*If the message is of type ACK then check if the server ID */
        if(msg.processID == MY_NODE_ID)
        {
            std::string key = std::to_string(msg.processID) + "." + std::to_string(msg.eventID);
            Node* myRequest = peekHashMap(key);
            if((myRequest!= nullptr) && (myRequest->ackCount < NUMBER_OF_CONNECTIONS))
                myRequest->ackCount++;
            updateReqEvent(msg);
        }
        else
        {
            if(msg.isACK == false)
            {
                updateReqEvent(msg);
                struct Node* newNode = new Node(msg);
                RequestQueue.enqueue(newNode);
            }
        }
        
        if(exitThread && JobQueue.isQueueEmpty() && RequestQueue.isQueueEmpty())
            break;
    }
    return nullptr;
}

/*For maintaining priority queue and moving the message to application queue*/
/*Req.self --> wait till counter reaches max and then place into job queue*/
/*Req.!self--> Place in JobQueue*/
void* MessageDelivery(void* ptr)
{
    bool selfRequest = false;
    while(true)
    {
        if(RequestQueue.isQueueEmpty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        else
        {
            Node* tempNode = RequestQueue.peekFront();
            if(tempNode == nullptr)
            {
                std::cout << "Request queue is empty" << std::endl;
            }
            else
            {
                if(tempNode->msg.processID == MY_NODE_ID)
                {
                    std::string key = std::to_string(tempNode->msg.processID)+"."+std::to_string(tempNode->msg.eventID);
                    Node* verify = MyRequests[key];

                    if((tempNode->ackCount >= NUMBER_OF_CONNECTIONS) && selfRequest)
                    {
                        popHashMap(key);
                        JobQueue.enqueue(RequestQueue.popFront());
                        selfRequest = false;
                    }
                    if(selfRequest == false)
                    {
                        selfRequest = true;
                        Node* temp = peekHashMap(key);
                        if(temp == nullptr)    continue;
                        temp->ackCount ++;
                    }
                    
                }
                else if(tempNode->msg.processID != MY_NODE_ID)
                {
                    JobQueue.enqueue(RequestQueue.popFront());
                }
            }
        }
        if(exitThread)
            break;
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
    std::cout << "Please wait till next minute(" << start_minute+1 << ")" << std::endl;

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
    //Multicast sender socket configuration
    /*-------------------------------------------------------------------------------------------------------------------*/
    struct sockaddr_in mcast_sendAddr{};
    int mcast_send = socket(AF_INET, SOCK_DGRAM, 0);

    if(mcast_send < 0)
    {
        perror("Sender socket creation failed");
        return -1;
    }

    mcast_sendAddr.sin_family = AF_INET;
    mcast_sendAddr.sin_port = htons(SEND_PORT);
    #if 0
    mcast_sendAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    #else
    inet_pton(AF_INET, MULTICAST_ADDR, &mcast_sendAddr.sin_addr);

    /*if(bind(mcast_send, (sockaddr*)&mcast_sendAddr, sizeof(mcast_sendAddr)) < 0)
    {
        perror("Sender port binding failed");
    }*/
    #endif

    argStruct mcast_sender = {mcast_send, mcast_sendAddr};
    /*-------------------------------------------------------------------------------------------------------------------*/

    //Multicast receiver socket configuration
    /*-------------------------------------------------------------------------------------------------------------------*/
    struct sockaddr_in mcast_receiveAddr{};
    int mcast_receive = socket(AF_INET, SOCK_DGRAM, 0);

    if(mcast_receive < 0)
    {
        perror("Receiver socket creation failed");
        return -1;
    }

    mcast_receiveAddr.sin_family = AF_INET;
    mcast_receiveAddr.sin_port = htons(RECEIVE_PORT);
    mcast_receiveAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    ip_mreq mreq{};
    inet_pton(AF_INET, MULTICAST_ADDR, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    //inet_pton(AF_INET, MY_IP_ADDR, &mreq.imr_interface);
    if(setsockopt(mcast_receive, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        std::cerr << "Receiver failed to join multicast group." << std::endl;
        return -1;
    }
    if(bind(mcast_receive, (sockaddr*)&mcast_receiveAddr, sizeof(mcast_receiveAddr)) < 0) {
        perror("Receiver port binding failed");
        return -1;
    }

    argStruct mcast_receiver = {mcast_receive, mcast_receiveAddr};
    /*-------------------------------------------------------------------------------------------------------------------*/

    //Thread spawning
    /*-------------------------------------------------------------------------------------------------------------------*/
    pthread_t clientThread, serverThread, messageDelivery;
    using namespace std::chrono;

    waitForMinute();

    pthread_create(&clientThread, nullptr, ClientThread, (void*)(&mcast_sender));
    pthread_create(&serverThread, nullptr, ServerThread, (void*)(&mcast_receiver));
    pthread_create(&messageDelivery, nullptr,MessageDelivery, NULL);
    /*-------------------------------------------------------------------------------------------------------------------*/

    while(true)
    {
        if(RequestQueue.isQueueEmpty())
        {
            currentEventID++;
            Message msg("REQ");
            Node* newNode = new Node(msg);
            msg.eventID = updateEvent("ISSUE");
            RequestQueue.enqueue(newNode);
            pushHashMap(newNode);
            noOfRequests++;
            std::cout << "Issued request for round " << noOfRequests << std::endl;
            msg.eventID = updateEvent("SEND");
            sendMessage(mcast_sender, msg);
        }

        if(noOfRequests == NUMBER_OF_REQUESTS) break;

        //RequestQueue.printQueue();
        //std::cout << "JobQueue isEmpty(" << JobQueue.isQueueEmpty() << "); RequestQueue isEmpty(" << RequestQueue.isQueueEmpty() << ")" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Completed " << noOfRequests << " rounds" << std::endl;
    while(RequestQueue.isQueueEmpty() || JobQueue.isQueueEmpty());
    std::this_thread::sleep_for(std::chrono::seconds(1));

    exitThread = true;

    pthread_join(messageDelivery, nullptr);
    pthread_join(clientThread, nullptr);
    pthread_join(serverThread, nullptr);

    close(mcast_send);
    close(mcast_receive);
    
    std::cout << "Press any key to end the program" << std::endl;
    std::cin.get();
    return 0;
}