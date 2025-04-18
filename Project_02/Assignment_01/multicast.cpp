#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unordered_map>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <string>
#include <chrono>
#include <ctime>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>

#pragma pack(1)

#define MY_NODE_ID "123"    //Will be updated for that particular Linux Node
#define NUMBER_OF_REQUESTS 3
#define ENABLE_LOGS true
#define MULTICAST_ADDR "239.0.0.1"
#define SEND_PORT 12345
#define RECEIVE_PORT SEND_PORT

int currentEventID = 0;
int localClock = 0;
std::set<std::string> serverList;
// Mutex for synchronizing shared resources
std::mutex clockMutex;
std::mutex requestMutex;
std::mutex mapMutex;
std::mutex queueMutex;
std::mutex jobMutex;
std::mutex eventMutex;
std::mutex logMutex;



//clockMutex -> requestMutex -> mapMutex -> queueMutex -> jobMutex

void log(std::string message);
void popHashMap(std::string key);
std::string printHashMap();
bool waitTimeout(int timeoutDuration, std::chrono::time_point<std::chrono::steady_clock> startTime);
void waitForMinute();


typedef struct
{
    int socket;
    struct sockaddr_in addr;
}argStruct;

class Message
{
    public:
        bool isACK = true;/*true->send acknowledgement/ false->receive acknowledgement*/
        int processID = std::stoi(MY_NODE_ID);
        int eventID = currentEventID;
        int clock;

        
    Message() = default;
    Message(std::string type)
    {
        std::lock_guard<std::mutex> lock(clockMutex);
        if(type == "ACK")
            isACK = true;
        else if(type == "REQ")
            isACK = false;
        else
        {
            std::string errorMessage = "Error: Invalid request type at line number " + std::to_string(__LINE__) + ".";
            throw std::invalid_argument(errorMessage);
        }
        clock = localClock;
    }
};

struct Node
{
    Node* next;
    std::set<std::string> requestACKs;//Pop each serverList after receiving ACK from nodes in network
    Message msg;

    //Constructor for Node data structure
    Node(const Message& m, const std::set<std::string>& serverList):
        next(nullptr),
        requestACKs(serverList),
        msg(m){}
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
        if ((node1.msg.clock < node2.msg.clock) ||
            ((node1.msg.clock == node2.msg.clock) && (node1.msg.processID < node2.msg.processID)))
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

    std::string printQueue()
    {   // For debugging
        Node* current = front;
        if (current == nullptr)
        {
            std::cout << "Queue is empty" << std::endl;
            return "";
        }

        std::string queue = "front -> ";
        while (current != nullptr)
        {
            queue = queue +  "(clock: " + std::to_string(current->msg.clock) + ", processID: " + std::to_string(current->msg.processID) + ", eventID: " + std::to_string(current->msg.eventID) + ", ACKs: " + std::to_string(serverList.size() - current->requestACKs.size()) + "]) -> ";
            current = current->next;
        }
        queue = queue + "rear";
        return queue;
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
        std::lock_guard<std::mutex> lock(queueMutex);
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
        Node* newNode = new Node(msg, serverList);
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
        std::lock_guard<std::mutex> lock(jobMutex);
        while (!isQueueEmpty())
            dequeue();
    }
};



int noOfRequests = 0;
std::unordered_map<std::string, Node*>MyRequests;
volatile bool exitThread = false;
PriorityQueue RequestQueue;
Queue JobQueue;

void pushHashMap(Node* newNode);
Node* peekHashMap(std::string key);
void updateClock(std::string type, Message& msg);
void sendMessage(argStruct mcast_sender, Message msg);
sockaddr_in receiveMessage(argStruct mcast_receiver, Message* msg);



void pushHashMap(Node* newNode)
{
    std::string key = std::to_string(newNode->msg.processID) + "." + std::to_string(newNode->msg.eventID);
    MyRequests[key] = newNode;
}

void popHashMap(std::string key)
{
    auto node = MyRequests.find(key);
    if(node != MyRequests.end()) MyRequests.erase(key);
}

Node* peekHashMap(std::string key)
{
    std::lock_guard<std::mutex> lock1(mapMutex);
    auto node = MyRequests.find(key);
    if(node != MyRequests.end()) return node->second;
    return nullptr;
}

std::string printHashMap()
{
    std::lock_guard<std::mutex> lock(mapMutex);
    std::string map = "MyRequest(hashMap)<eventID.processID, ACKs> = ";
    for(const auto& pair : MyRequests)
    {
        map = map + "(" + pair.first + ", " + std::to_string(serverList.size() - pair.second->requestACKs.size()) + ") ";
    }
    return map;
}

void updateClock(std::string type, Message& msg)
{
    std::lock_guard<std::mutex> lock(clockMutex);
    if(type == "RECEIVE")
        localClock = std::max(localClock, msg.clock);

    localClock++;
    msg.clock = localClock;
    if(msg.isACK)
        log(std::string(MY_NODE_ID) + ":" + std::to_string(msg.processID) + "." + std::to_string(msg.eventID) + "\t" + type + "(ACK)" + "\tCLOCK(" + std::to_string(msg.clock) + ")");
    else
    log(std::string(MY_NODE_ID) + ":" + std::to_string(msg.processID) + "." + std::to_string(msg.eventID) + "\t" + type + "(REQ)" + "\tCLOCK(" + std::to_string(msg.clock) + ")");
    //log(RequestQueue.printQueue());
    //log(printHashMap());
}

void log(std::string message)
{
    #if ENABLE_LOGS
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << message << std::endl;
    #endif
}

/*UDP send message*/
/*If sending Msg.REQ then use a set and retransmit the message until 4 different ACKs are received*/
/*If sending Msg.ACK then use a set and retransmit the message until 4 different ACKs are received*/
#if 1
void sendMessage(argStruct mcast_sender, Message msg)
{
    updateClock("SEND", msg);
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
sockaddr_in receiveMessage(argStruct mcast_receiver, Message* msg)
{
    int receivedBytes = -1;
    sockaddr_in senderAddr{};
    auto start = std::chrono::steady_clock::now();
    while(true)
    {
        if(exitThread && waitTimeout(1, start))
        {
            receivedBytes = -1;
            break;
        }
            
        if(!exitThread)
            start = std::chrono::steady_clock::now();

        socklen_t senderAddrLen = sizeof(senderAddr);
        receivedBytes = recvfrom(mcast_receiver.socket, msg, sizeof(*msg), MSG_DONTWAIT, (sockaddr*)&senderAddr, &senderAddrLen);
        if(receivedBytes < 0)
        {
            if(errno == EWOULDBLOCK || errno == EAGAIN)
                continue;
            else
                perror("Error in receiving message");
        }
        else
            break;
    }
    if(receivedBytes > 0)
        updateClock("RECEIVE", *msg);
    return senderAddr;
}

/*This function will tell if the specified timeoutDuration has reached from the startTime*/
bool waitTimeout(int timeoutDuration, std::chrono::time_point<std::chrono::steady_clock> startTime)
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime);

    if (elapsed.count() >= timeoutDuration)
        return true;
    else
        return false;
}

/*Only for sending ACKs*/
/*JobQueue.!selfReq --> multiCastACK*/
void* ClientThread(void* arg)
{
    auto start = std::chrono::steady_clock::now();
    while(true)
    {
        if(exitThread && waitTimeout(1, start))
            break;
        if(!exitThread)
            start = std::chrono::steady_clock::now();

        //make the thread to sleep until ACKs are received?
        if(!JobQueue.isQueueEmpty())
        {
            Message* msg = JobQueue.dequeue();
            msg->isACK = true;
            sendMessage(*((argStruct*)arg), *msg);
            delete msg;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return nullptr;
}

/*if ACK then check in HashMap and pop IP from requestACKs*/
/*if REQ then enqueue in RequestQueue*/
void* ServerThread(void* arg)
{
    auto start = std::chrono::steady_clock::now();
    while(true)
    {
        if(exitThread && waitTimeout(1, start))
            break;
        if(!exitThread)
            start = std::chrono::steady_clock::now();

        Message msg;
        sockaddr_in recvAddr = receiveMessage(*((argStruct*)arg), &msg);
        std::string key = std::to_string(msg.processID) + "." + std::to_string(msg.eventID);
        Node* myRequest = peekHashMap(key);
        
        if(msg.isACK && (myRequest != nullptr))
            myRequest->requestACKs.erase(std::string(inet_ntoa(recvAddr.sin_addr)));
        if(!msg.isACK)
        {
            struct Node* newNode = new Node(msg, serverList);
            RequestQueue.enqueue(newNode);
            JobQueue.enqueue(msg);
            MyRequests[key] = newNode;
        }
    }
    return nullptr;
}

/*For maintaining priority queue and moving the message to application queue*/
/*Req.self --> wait till counter reaches max and then place into job queue*/
/*Req.!self--> Place in JobQueue*/
void* MessageDelivery(void* ptr)
{
    auto start = std::chrono::steady_clock::now();
    while(true)
    {
        if(exitThread && waitTimeout(1, start))
            break;
        if(!exitThread)
            start = std::chrono::steady_clock::now();

        /*Sorting*/
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
                std::cerr << "Request queue is not properly handled" << std::endl;
                std::cerr << "Terminating all threads" << std::endl;
                exitThread = true;
                break;
            }
            else
            {
                std::string key = std::to_string(tempNode->msg.processID)+"."+std::to_string(tempNode->msg.eventID);
                if(MyRequests.find(key) == MyRequests.end())
                {
                    if(tempNode->msg.isACK)
                        std::cerr << "Request(processID.eventID:" << key << "_ACK) wasn't enqueued.\nTerminating all threads" << std::endl;
                    else
                        std::cerr << "Request(processID.eventID:" << key << "_REQ) wasn't enqueued.\nTerminating all threads" << std::endl;
                        exitThread = true;
                    break;
                }
                else
                {
                    Node* verify = MyRequests[key];
                    if(verify->requestACKs.size() == 0)
                    {
                        updateClock("PROCESS", tempNode->msg);
                        RequestQueue.popFront();
                        popHashMap(key);
                    }
                    else
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
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

    std::cout << "Node started at minute: " << start_minute << std::endl;
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
    serverList.insert("192.168.5.123");
    serverList.insert("192.168.5.124");
    serverList.insert("192.168.5.125");
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
            Node* newNode = new Node(msg, serverList);
            updateClock("ISSUE", msg);
            noOfRequests++;
            sendMessage(mcast_sender, msg);
        }

        if(noOfRequests == NUMBER_OF_REQUESTS) break;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    
    while(RequestQueue.isQueueEmpty() && JobQueue.isQueueEmpty());
    
    std::this_thread::sleep_for(std::chrono::seconds(1));

    exitThread = true;

    pthread_join(messageDelivery, nullptr);
    pthread_join(clientThread, nullptr);
    pthread_join(serverThread, nullptr);

    std::cout << "Completed " << noOfRequests << " rounds" << std::endl;

    close(mcast_send);
    close(mcast_receive);
    
    std::cout << "Press any key to end the program" << std::endl;
    std::cin.get();
    return 0;
}