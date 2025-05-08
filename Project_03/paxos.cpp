#include <iostream>
#include <fstream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <string>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <cstdlib>
#include <sstream>
#include <ifaddrs.h>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <set>

#pragma pack(1)

//192.168.5.123 ---> 2063968448 --> PROPOSER
//192.168.5.124 ---> 2080745664 --> ACCEPTOR
//192.168.5.125 ---> 2097522880 --> ACCEPTOR(COMMON)
//192.168.5.126 ---> 2114300096 --> ACCEPTOR
//192.168.5.127 ---> 2131077312 --> PROPOSER
//192.168.5.128 ---> 2147854528 --> COORDINATOR

#define SEND_PORT   12346
#define RECV_PORT   12345
#define ENABLE_LOG  true
#define BROADCAST_ADDR "192.168.5.255"

enum class MsgType : int
{
    PREPARE,
    PROMISE,
    ACCEPT,
    ACCEPTED,
    PROPOSAL_REQ,
    PROPOSAL_OK,
    PROPOSER_DONE,
    ACCEPTOR_DONE
};

enum class CoordinatorMode : int
{
    MSG_Q,
    B2B_1,
    B2B_2,
    B2B_3,
    LIVELOCK
};

struct Message
{//Structure for Proposer, Acceptor, Coordinator
    bool isReq;
    MsgType msgType;
    //For PREPARE
    long int proposalNumber;

    //For PROMISE
    long int lastAcceptedProposal;
    int lastAcceptedValue;
    
    //For ACCEPT
    int proposalValue;

    //Sender address for tracking
    in_addr_t sendIP;
    in_addr_t recvIP;
};

struct RecvArgs
{
    sockaddr_in recvAddr;
    int recvSock;
};

struct Node
{
    Node* next;
    Message msg;
    sockaddr_in recvAddr;
};

class ActivityLog
{
    public:
        std::fstream MyFile;

        ActivityLog()
        {
            MyFile.open("localLog.txt", std::ios::out | std::ios::app);  // Open for read & append
        }

        ~ActivityLog()
        {
            if (MyFile.is_open())
                MyFile.close();
        }

        /*std::string getCurrentTimeStamp()
        {
            auto now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            char buffer[20];  // "YYYY-MM-DD HH:MM:SS" = 19 chars + '\0'

            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));
            return std::string(buffer);
        }*/
        std::string getCurrentTimeStamp()
        {
            // Get the current time
            auto now = std::chrono::system_clock::now();
        
            // Convert to time_t to extract standard date-time
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        
            // Convert to milliseconds
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
            // Format date-time
            std::tm* now_tm = std::localtime(&now_c);
        
            // Create stringstream to format the output
            std::ostringstream oss;
            oss << std::put_time(now_tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
        
            // Return the formatted string
            return oss.str();
        }

        bool log(const std::string txt)
        {
            if (MyFile.is_open())
            {
                MyFile.clear();                       // Clear any error or EOF flags
                MyFile.seekp(0, std::ios::end);       // Force write pointer to end
                MyFile << getCurrentTimeStamp() << " " << txt << std::endl;
                return true;
            }
            return false;
        }

        bool log(const std::string txt, const Message msg)
        {
            if (MyFile.is_open())
            {
                MyFile.clear();                       // Clear any error or EOF flags
                MyFile.seekp(0, std::ios::end);       // Force write pointer to end
                MyFile << getCurrentTimeStamp() << " " << txt << "\t\t";
                if(msg.msgType == MsgType::PREPARE)
                    MyFile << "PREPARE\t\t" << "[PRPSL_NO:" << std::to_string(msg.proposalNumber) << "]" << std::endl;
                else if(msg.msgType == MsgType::ACCEPT)
                    MyFile << "ACCEPT\t\t" << "[PRPSL_NO:" << std::to_string(msg.proposalNumber) << "]\t\t" << "[PRPSL_VAL:" << std::to_string(msg.proposalValue) << "]" << std::endl;
                else if(msg.msgType == MsgType::PROMISE)
                    MyFile << "PROMISE\t\t" << "[ACCEPT_NO:" << std::to_string(msg.lastAcceptedProposal) << "]\t\t" << "[ACCEPT_VAL:" << std::to_string(msg.lastAcceptedValue) << "]" << std::endl;
                else if(msg.msgType == MsgType::ACCEPTED)
                    MyFile << "ACCEPTED\t\t" << "[ACCPTD_NO:" << std::to_string(msg.lastAcceptedProposal) << "]" << std::endl;
                else if(msg.msgType == MsgType::PROPOSAL_REQ)
                    MyFile << "PROPOSAL\t\t" << "[PRPSL_REQ:" << std::to_string(msg.proposalNumber) << "]" << std::endl;
                else if(msg.msgType == MsgType::PROPOSAL_OK)
                    MyFile << "PROPOSAL\t\t" << "[PRPSL_OK:" << std::to_string(msg.proposalNumber) << "]" << std::endl;
                else if(msg.msgType == MsgType::PROPOSER_DONE)
                    MyFile << "PROPOSER\t\t" << "[PRPSR_NO:" << std::to_string(msg.proposalNumber) << "]\t\t" << "[PRPSL_VAL:" << std::to_string(msg.proposalValue) << "]" << std::endl;
                else if(msg.msgType == MsgType::ACCEPTOR_DONE)
                    MyFile << "ACCEPTOR\t\t" << "[DONE]" << "\t\t" << "[PRPSL_VAL:" << std::to_string(msg.proposalValue) << "]" << std::endl;
                return true;
            }
            return false;
        }
};


std::string GRAY  = "\033[90m";     //FOR SEND & RECEIVE MESSAGES
std::string RESET = "\033[0m";      //FOR RESETING THE PRINT CONFIGURATIONS
std::string BLUE  = "\033[34m";     //FOR MAIN FUNCTION LOGS
std::string GREEN  = "\033[32m";    //FOR PROPOSER THREAD LOGS
std::string MAGENTA  = "\033[35m";    //FOR ACCEPTOR THREAD LOGS
std::mutex logMtx, str2typeMtx, type2strMtx, q_Mtx;
std::condition_variable q_cv;
std::set<in_addr_t> Participants;
std::unordered_map<in_addr_t, std::vector<in_addr_t>> commMap;
std::unordered_map<std::string, Message> reqMap;
bool isProposer = false;
bool isCoordinator = false;
bool withCoordinator = false;
volatile bool proposerDone = false;
volatile bool acceptorDone = false;
int proposerDoneCounter = 0;
volatile bool exitThreads = false;
int acceptorCount = 0;
int acceptedCounter = 0;
bool onlyOnce = false;

inline std::string type2str(const MsgType type);
void log(std::string text);

class RecvQueue {
    public:
        Node* front = nullptr;
        Node* rear  = nullptr;
    
        ~RecvQueue() {
            // drain and delete any left-over nodes
            while (front) {
                Node* tmp = front;
                front = front->next;
                delete tmp;
            }
        }
    
        void enqueue(const Message& msg, const sockaddr_in& recvAddr) {
            std::lock_guard<std::mutex> lock(q_Mtx);
            Node* n = new Node();
            n->msg = msg;
            n->recvAddr = recvAddr;
            if (!rear) {
                // empty queue
                front = rear = n;
            } else {
                rear->next = n;
                rear       = n;
            }
        }

        // returns nullptr if empty, otherwise contents of the Node
        Node* peek()
        {
            std::lock_guard<std::mutex> lock(q_Mtx);
            return front;
        }
    
        // returns nullptr if empty, otherwise ownership of the Node
        Node* dequeue()
        {
            std::lock_guard<std::mutex> lock(q_Mtx);
            if (!front) return nullptr;
            Node* n = front;
            front = front->next;
            n->next = nullptr;
            if (!front) rear = nullptr;
            /*if(n != nullptr)
            {
                log(GRAY+"Dequeued the message "+type2str(n->msg.msgType)+RESET);
            }
            else
            {
                log(GRAY+"Dequeued nullptr"+RESET);
            }*/
            return n;
        }

        bool empty()
        {
            return front == nullptr;
        }
};

//For proposer
Message proposerMessage;
int proposerValue = 0;
long int highestAcceptedProposal = -1;
int myNodeID = 0;
in_addr_t coordinatorIP;
in_addr_t commonAcceptorIP;
in_addr_t myIP;
ActivityLog logger;
int noOfProposals = 0;

//For acceptor
Message acceptorMessage;
long int minProposal = 0;
long int acceptedProposal = 0;
int acceptedValue = 0;

//For receiver
RecvQueue recvQueue;

void extractMyIP();
bool sendMessage(Message msg);
bool receiveMessage(RecvArgs recvArgs, Message& msg, sockaddr_in& recvAddr, int flags);
void waitForMinute(void);
bool waitTimeout(int TimeoutDuration, std::chrono::time_point<std::chrono::steady_clock> msecTimeoutDuration);
bool prepare(RecvArgs recvArgs, Message msg);
bool accept(RecvArgs recvArgs, Message msg);
void backOff();
inline MsgType str2type(const std::string& s);

void* coordinatorThread(void* arg);
void* proposerThread(void* arg);
void* acceptorThread(void* arg);

/*This function will update the max round based on last saved data*/
long int updateMaxRound(Message msg)
{
    long int last = 0;

    {
        std::fstream in("MaxRound.txt", std::ios::in);
        long x = 0;
        while(in >> x) last = x;
        in.close();
    }

    last = std::max(last, (msg.proposalNumber >> 8));
    last = std::max(last, (msg.lastAcceptedProposal >> 8));
    last = last + 1;

    {
        std::fstream out("MaxRound.txt", std::ios::out | std::ios::app);
        out << last << std::endl;
        out.flush();
        out.close();
    }

    last = (last << 8) | myNodeID;

    std::cout << last << std::endl;
    return last;
}

/*This function will log messages to terminal*/
void log(std::string text)
{
#if ENABLE_LOG
    {
        std::lock_guard<std::mutex> lock(logMtx);
        std::cout << text << std::endl;
    }
#endif
}

/*This function will convert given string to enum type*/
inline MsgType str2type(const std::string& s)
{
    std::lock_guard<std::mutex> lock(str2typeMtx);
    static const std::unordered_map<std::string,MsgType> m = {
                                                                { "PREPARE",           MsgType::PREPARE      },
                                                                { "PROMISE",           MsgType::PROMISE      },
                                                                { "ACCEPT",            MsgType::ACCEPT       },
                                                                { "ACCEPTED",          MsgType::ACCEPTED     },
                                                                { "PROPOSAL_OK",       MsgType::PROPOSAL_OK  },
                                                                { "PROPOSAL_REQ",      MsgType::PROPOSAL_REQ },
                                                                { "PROPOSER_DONE",     MsgType::PROPOSER_DONE},
                                                                { "ACCEPTOR_DONE",     MsgType::ACCEPTOR_DONE}
                                                                };
    auto it = m.find(s);
    if (it != m.end()) {
        return it->second;
    }
    throw std::invalid_argument("Invalid MsgType string: " + s);
}

/*This function will convert given enum type to string*/
inline std::string type2str(const MsgType type)
{
    std::lock_guard<std::mutex> lock(type2strMtx);
    switch(type)
    {
        case MsgType::PREPARE:          return "PREPARE";
        case MsgType::PROMISE:          return "PROMISE";
        case MsgType::ACCEPT:           return "ACCEPT";
        case MsgType::ACCEPTED:         return "ACCEPTED";
        case MsgType::PROPOSAL_REQ:     return "PROPOSAL_REQ";
        case MsgType::PROPOSAL_OK:      return "PROPOSAL_OK";
        case MsgType::PROPOSER_DONE:    return "PROPOSER_DONE";
        case MsgType::ACCEPTOR_DONE:    return "ACCEPTOR_DONE";
        default: throw std::invalid_argument("Unknown enum type");
    }
}

/*This function will tell if the specified timeoutDuration has reached from the startTime*/
bool waitTimeout(int msecTimeoutDuration, std::chrono::time_point<std::chrono::steady_clock> startTime)
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);

    if (elapsed.count() >= msecTimeoutDuration)
        return true;
    else
        return false;
}

/*This function will parse the config file and extract the IP address of participants*/
void parseConfig(char* fileName)
{
    std::ifstream MyFile(fileName);
    if(!MyFile)
    {
        std::cerr << "Invalid file " << fileName << std::endl;
        exit(EXIT_FAILURE);
    }

    std::string role, address, line;
    while(std::getline(MyFile, line))
    {
        if(line.empty() || line[0] == '#')
            continue;
     
        std::istringstream iss(line);
        if(!(iss >> role >> address)) std::cerr << "Invalid line " << line << std::endl;
        else
        {
            sockaddr_in tempAddr;
            inet_pton(AF_INET, address.c_str(), &tempAddr.sin_addr);
            if(role == "COORDINATOR")
            {
                sockaddr_in tempAddr;
                inet_pton(AF_INET, address.c_str(), &tempAddr.sin_addr);
                coordinatorIP = tempAddr.sin_addr.s_addr;
                withCoordinator = true;
            }
            else if(role == "PROPOSER")
            {
                noOfProposals++;
            }

            if(tempAddr.sin_addr.s_addr == myIP)
            {
                if(role == "COORDINATOR") isCoordinator = true;
                if(role == "PROPOSER") isProposer = true;
            }
            if(role != "COORDINATOR")
            {
                
                Participants.insert(tempAddr.sin_addr.s_addr);
            }
        }
    }
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

/*This function will send Message using UDP*/
bool sendMessage(Message msg)
{
    sockaddr_in sendAddr;
    socklen_t sendAddrlen = sizeof(sendAddr);


    sendAddr.sin_family = AF_INET;
    sendAddr.sin_port = htons(RECV_PORT);
    if(withCoordinator) 
    {
        if(isCoordinator)
        {
            if(msg.isReq) sendAddr.sin_addr.s_addr = msg.recvIP;
            else sendAddr.sin_addr.s_addr = msg.sendIP;
        }
        else
        {
            sendAddr.sin_addr.s_addr = coordinatorIP;
        }
    }
    else
    {
        if(msg.isReq)
        {
            sendAddr.sin_addr.s_addr = msg.recvIP;
        }
        else 
        {
            sendAddr.sin_addr.s_addr = msg.sendIP;
        }
    }
    
    int sendSock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sendSock == -1)
    {
        perror("Failed to create a send socket");
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    if (setsockopt(sendSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("Failed to set SO_REUSEADDR");
        close(sendSock);
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sendSock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
        perror("Failed to set SO_REUSEPORT");
        close(sendSock);
        exit(EXIT_FAILURE);
    }

    sockaddr_in bindAddr;
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(SEND_PORT);
    bindAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sendSock, (sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
        perror("Failed to bind send socket");
        close(sendSock);
        exit(EXIT_FAILURE);
    }

    int sentBytes = sendto(sendSock, &msg, sizeof(msg), 0, (sockaddr*)(&sendAddr), sendAddrlen);
    if(sentBytes < 0)
    {
        perror("Error in sending data");
        exit(EXIT_FAILURE);
    }
    else
    {
        logger.log( "SENT", msg);
        char IP_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sendAddr.sin_addr, IP_str, INET_ADDRSTRLEN);
        if(isProposer)
            log(GRAY+"Proposer: Sent [message:"+type2str(msg.msgType)+"] to [address:"+IP_str+"] at [port:"+std::to_string((uint16_t)ntohs(sendAddr.sin_port))+"]"+RESET);
        else if(isCoordinator)
            log(GRAY+"Coordinator: Sent [message:"+type2str(msg.msgType)+"] to [address:"+IP_str+"] at [port:"+std::to_string((uint16_t)ntohs(sendAddr.sin_port))+"]"+RESET);
        else
            log(GRAY+"Acceptor: Sent [message:"+type2str(msg.msgType)+"] to [address:"+IP_str+"] at [port:"+std::to_string((uint16_t)ntohs(sendAddr.sin_port))+"]"+RESET);
        return true;
    }
}

/*This function will receive message using UDP*/
bool receiveMessage(RecvArgs recvArgs, Message& msg, sockaddr_in& recvAddr, int flags)
{
    socklen_t recvAddrLen = sizeof(recvAddr);
    recvAddr.sin_port = htons(RECV_PORT);
    int receivedBytes = recvfrom(recvArgs.recvSock, &msg, sizeof(msg), flags, (sockaddr*)(&recvAddr), &recvAddrLen);
    if(receivedBytes < 0)
    {
        //perror("Error in receiving data");
        return false;
    }
    else
    {
        logger.log( "RCVD", msg);
        char IP_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &recvAddr.sin_addr, IP_str, INET_ADDRSTRLEN);

        if(isProposer)
            log(GRAY+"Proposer: Received [message:"+type2str(msg.msgType)+"] from [address:"+IP_str+"] from [port:"+std::to_string((uint16_t)ntohs(recvAddr.sin_port))+"]"+RESET);
        else if(isCoordinator)
            log(GRAY+"Coordinator: Received [message:"+type2str(msg.msgType)+"] from [address:"+IP_str+"] from [port:"+std::to_string((uint16_t)ntohs(recvAddr.sin_port))+"]"+RESET);
        else
            log(GRAY+"Acceptor: Received [message:"+type2str(msg.msgType)+"] from [address:"+IP_str+"] from [port:"+std::to_string((uint16_t)ntohs(recvAddr.sin_port))+"]"+RESET);
        return true;
    }
}

/*This function will extract my nodes IP address*/
void extractMyIP()
{
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    char ip[INET_ADDRSTRLEN];
    sockaddr_in tempAddr;
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;

        auto *sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        tempAddr = *sin;
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));

        if(strncmp(ip, "192.168.5", 9) == 0)
            break;
    }

    int endDotPos = std::string(ip).find_last_of(".");
    myNodeID = stoi((std::string(ip)).substr(endDotPos+1, strlen(ip)-endDotPos));

    freeifaddrs(ifaddr);
    myIP = tempAddr.sin_addr.s_addr;
}

/*This will induce a random backoff, incase proposal gets rejected*/
void backOff()
{
    std::srand(std::time(nullptr));
    int msec = std::rand() % (500 - 1 + 1) + 1;

    auto startTime = std::chrono::steady_clock::now();
    while(true)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
        if (elapsed.count() >= msec) break;
    }
}

/*This function contains the logic for proposer's prepare phase*/
#if 0
bool prepare(RecvArgs recvArgs, Message msg)
{
    for(const in_addr_t &sin_addr : Participants)
    {
        msg.recvIP = sin_addr;
        sendMessage(msg);
    }
    log(GREEN+"PROPOSER:Proposer sent PREPARE messages to acceptors"+RESET);

    std::set<in_addr_t> TempList(Participants);
    auto startTime = std::chrono::steady_clock::now();
    bool timeoutStatus = false;
    while(true)
    {
        {
            std::unique_lock<std::mutex> lock(q_Mtx);
            timeoutStatus = q_cv.wait_for(lock, std::chrono::milliseconds(20), [&]{return !recvQueue.empty();});
            //q_cv.wait(lock, []{ return !recvQueue.empty(); });
        }

        if(!timeoutStatus)
        {
            Node* tempNode = recvQueue.peek();
            if(tempNode == nullptr){}
            else 
            {
                if(tempNode->msg.isReq == false)
                {
                    tempNode = recvQueue.dequeue();
                    msg = tempNode->msg;
                    delete(tempNode);
                    if(msg.msgType == MsgType::PROMISE)
                    {
                        TempList.erase(msg.recvIP);
                        char temp[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &msg.recvIP, temp, INET_ADDRSTRLEN);

                        log(GREEN+"PROPOSER:Proposer received PROMISE from "+std::string(temp)+RESET);

                        if((msg.lastAcceptedValue  != 0) && (msg.lastAcceptedProposal > proposerMessage.lastAcceptedProposal))
                        {
                            log(GREEN+"PROPOSER:Proposer updated proposal value("+std::to_string(proposerMessage.proposalValue)+") with "+std::to_string(msg.lastAcceptedValue)+RESET);
                            proposerMessage.proposalValue = msg.lastAcceptedValue;//You will lose prev compared values if you update in recvNode() find a better way
                            proposerMessage.lastAcceptedProposal = msg.lastAcceptedProposal;//You will lose prev compared values if you update in recvNode() find a better way
                        }
                        if(TempList.size() < (Participants.size() + 1) / 2) return true;
                    }
                }
            }
        }
        
        if(waitTimeout(8000, startTime))
        {
            log(GREEN+"PROPOSER: Timeout occurred during PREPARE phase"+RESET);
            break;
        }
    }
    return false;
}
#else
bool prepare(RecvArgs recvArgs, Message msg)
{
    for(const in_addr_t &sin_addr : Participants)
    {
        msg.recvIP = sin_addr;
        sendMessage(msg);
    }
    log(GREEN+"PROPOSER: Proposer sent PREPARE messages with proposerNumber["+std::to_string(msg.proposalNumber)+"] and proposalValue["+std::to_string(msg.proposalValue)+"] to acceptors"+RESET);

    std::set<in_addr_t> TempList(Participants);
    auto startTime = std::chrono::steady_clock::now();
    bool timeoutStatus = false;

    while(true)
    {
        {
            std::unique_lock<std::mutex> lock(q_Mtx);
            timeoutStatus = q_cv.wait_for(lock, std::chrono::milliseconds(100), [&]{ return !recvQueue.empty(); });
            //q_cv.wait(lock, []{ return !recvQueue.empty(); });
        }

        if (!timeoutStatus || (!recvQueue.empty()))
        {
            Node* tempNode = recvQueue.peek();
            if(tempNode == nullptr) continue;

            // Fetch the message from the front of the queue
            if(tempNode->msg.isReq == false)
            {
                // If the message is for a previous proposal number, flush it
                /*if (tempNode->msg.proposalNumber < msg.proposalNumber)
                {
                    tempNode = recvQueue.dequeue();
                    if(tempNode != nullptr)delete(tempNode);
                    else continue;
                    log(GRAY + "PROPOSER: Flushing old message with proposal number " + std::to_string(tempNode->msg.proposalNumber) + RESET);
                    continue;
                }*/

                // If it is a valid `PROMISE`, dequeue and process it
                if (tempNode->msg.msgType == MsgType::PROMISE)
                {
                    tempNode = recvQueue.dequeue();   
                    if(tempNode != nullptr)
                    {
                        msg = tempNode->msg;
                        delete(tempNode);
                    }
                    else continue;

                    TempList.erase(msg.recvIP);

                    char temp[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &msg.recvIP, temp, INET_ADDRSTRLEN);

                    log(GREEN+"PROPOSER: Proposer received PROMISE from "+std::string(temp)+RESET);

                    // Check if the value is greater than the currently stored one
                    if ((msg.lastAcceptedValue != 0) && (msg.lastAcceptedProposal > proposerMessage.lastAcceptedProposal))
                    {
                        log(GREEN+"PROPOSER: Proposer updated proposal value("+std::to_string(proposerMessage.proposalValue)+") with "+std::to_string(msg.lastAcceptedValue)+RESET);
                        proposerMessage.proposalValue = msg.lastAcceptedValue;
                        proposerMessage.lastAcceptedProposal = msg.lastAcceptedProposal;
                    }

                    if (TempList.size() < (Participants.size() + 1) / 2)
                    {
                        return true;
                    }
                }
                else
                {
                    log(GREEN+"Proposer: Proposer received "+type2str(msg.msgType)+RESET);
                }
            }
        }

        if(waitTimeout(5000, startTime))
        {
            log(GREEN+"PROPOSER: Timeout occurred during PREPARE phase"+RESET);
            break;
        }
    }
    return false;
}
#endif

/*This function contains the logic for proposer's accept phase*/
bool accept(RecvArgs recvArgs, Message msg)
{
    try
    {
        for(const in_addr_t &recv_IP : Participants)
        {
            msg.recvIP = recv_IP;
            {
                char temp[INET_ADDRSTRLEN];
                in_addr tempAddr;
                tempAddr.s_addr = recv_IP;
                inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                log(GREEN+"PROPOSER: Proposer sent ACCEPT message to "+std::string(temp)+RESET);
            }
            sendMessage(msg);
        }
        log(GREEN+"PROPOSER: Proposer sent ACCEPT messages to all participants"+RESET);

        std::set<in_addr_t> TempList(Participants);
        std::set<in_addr_t> rejectList;
        bool acceptStatus = true;
        auto startTime = std::chrono::steady_clock::now();
        bool timeoutStatus = false;
        while(true)
        {
            {
                std::unique_lock<std::mutex> lock(q_Mtx);
                timeoutStatus = q_cv.wait_for(lock, std::chrono::milliseconds(100), [&]{return !recvQueue.empty();});
                //q_cv.wait(lock, []{ return !recvQueue.empty(); });
            }

            if(!timeoutStatus || (!recvQueue.empty()))
            {
                Node* recvNode = recvQueue.peek();
                if(recvNode == nullptr){}
                else
                {
                    if(recvNode->msg.isReq == false)
                    {
                        recvNode = recvQueue.dequeue();
                        if(recvNode == nullptr)
                        {
                            continue;
                        }
                        else
                        {
                            msg = recvNode->msg;
                            delete(recvNode);
                        }
                        
                        if(msg.msgType == MsgType::ACCEPTED)
                        {
                            char temp[INET_ADDRSTRLEN];
                            inet_ntop(AF_INET, &msg.recvIP, temp, INET_ADDRSTRLEN);
                            if(msg.proposalNumber > proposerMessage.proposalNumber)
                            {
                                log(GREEN+"PROPOSER: Proposer's proposal was rejected by "+std::string(temp)+" ["+std::to_string(proposerMessage.proposalNumber)+">"+std::to_string(msg.proposalNumber)+"]"+RESET);
                                rejectList.insert(msg.recvIP);
                                if(rejectList.size() >= ((Participants.size() + 1)/2))
                                {
                                    proposerMessage.proposalValue = msg.lastAcceptedValue;
                                    return false;
                                }
                            }
                            else
                            {
                                TempList.erase(msg.recvIP);
                                log(GREEN+"PROPOSER: Proposer received ACCEPTED message from "+std::string(temp)+RESET);
                                if(TempList.size() < (Participants.size() + 1) / 2) return true;
                            }
                        }
                    }
                }
            }

            if(waitTimeout(5000, startTime))
            {
                log(GREEN+"PROPOSER: Proposer's ACCEPT phase has reached timeout"+RESET);
                break;
            }
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "In accept phase " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }
    
    
    return false;
}

/*This function is one of the coordinator modes*/
/*Simulate case 1 as per PPT*/
void back2back_1(RecvArgs recvArgs)
{
    log(BLUE+"Entered slide 63 functionality"+RESET);
    int proposeDoneCounter = 0;
    bool timeoutStatus = false;
    bool onlyOnce = false;
    bool myCondition = false;
    
    while(true)
    {
        if(proposerDone && acceptorDone)
        {
            log(BLUE+"Coordinator: All proposers and acceptors are terminated"+RESET);
            break;
        }

        if(!onlyOnce && myCondition)
        {
            Message msg = reqMap["127_128"];
            sendMessage(msg);
            std::cout << "node 128 is sending PROPOSAL_OK to 127" << std::endl;
            onlyOnce = true;
        }

        {
            std::unique_lock<std::mutex> lock(q_Mtx);
            timeoutStatus = q_cv.wait_for(lock, std::chrono::milliseconds(100), [&]{return !recvQueue.empty();});
            //q_cv.wait(lock, []{ return !recvQueue.empty(); });
        }
        //log(BLUE+"Coordinator: Is receive queue empty"+std::to_string(!recvQueue.empty())+RESET);
        if((!timeoutStatus) || (!recvQueue.empty()))
        {
            Node* tempNode = recvQueue.dequeue();
            if(tempNode != nullptr)
            {
                Message msg = tempNode->msg;

                char sendIP[INET_ADDRSTRLEN], recvIP[INET_ADDRSTRLEN];
                std::string sendNode, recvNode;
                inet_ntop(AF_INET, &msg.sendIP, sendIP, INET_ADDRSTRLEN);
                inet_ntop(AF_INET, &msg.recvIP, recvIP, INET_ADDRSTRLEN);

                int endDotPos = std::string(sendIP).find_last_of(".");
                sendNode = (std::string(sendIP)).substr(endDotPos+1, strlen(sendIP)-endDotPos);
                endDotPos = std::string(recvIP).find_last_of(".");
                recvNode = (std::string(recvIP)).substr(endDotPos+1, strlen(recvIP)-endDotPos);

                std::string key = sendNode + "_" + recvNode;

                delete(tempNode);
                {
                    /*
                    char temp[INET_ADDRSTRLEN];
                    in_addr tempAddr;
                    if(msg.isReq)
                    {
                        tempAddr.s_addr = msg.sendIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent by " << temp << std::endl;
                        tempAddr.s_addr = msg.recvIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent for " << temp << std::endl;
                    }
                    else
                    {
                        tempAddr.s_addr = msg.recvIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent by " << temp << std::endl;
                        tempAddr.s_addr = msg.sendIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent for " << temp << std::endl;
                    }*/
                }
                if(msg.isReq)
                {
                    //log(BLUE+"Coordinator:--- Received message is request for "+std::to_string(msg.recvIP)+" from "+std::to_string(msg.sendIP)+RESET);
                    if(msg.recvIP == myIP)
                    {
                        //std::cout << "Message was received for coordinator" << std::endl;
                        //std::cout << "Message type is " << type2str(msg.msgType) << std::endl;
                        if(msg.msgType == MsgType::PROPOSAL_REQ)
                        {
                            msg.msgType = MsgType::PROPOSAL_OK;
                            msg.isReq = false;

                            if((!onlyOnce) && (msg.sendIP == 2131077312))
                            {
                                std::cout << "Generated key is " << key << std::endl;
                                reqMap[key] = msg;
                                continue;
                            }

                            sendMessage(msg);
                            {
                                char temp[INET_ADDRSTRLEN];
                                in_addr tempAddr;
                                tempAddr.s_addr = msg.sendIP;
                                inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                                log(BLUE+"Coordinator: Coordinator accepted PROPOSAL_REQ from "+std::string(temp)+RESET);
                            }
                            //std::cout << "Coordinator has send approval for proposer's request" << std::endl;
                        }
                        else if(msg.msgType == MsgType::PROPOSER_DONE)
                        {
                            msg.isReq = false;
                            proposeDoneCounter++;
                            if(msg.sendIP == 2063968448)
                            {
                                myCondition = true;
                                std::cout << "node 123 sent PROPOSER_DONE" << std::endl;
                            }
                            sendMessage(msg);
                            {
                                char temp[INET_ADDRSTRLEN];
                                in_addr tempAddr;
                                tempAddr.s_addr = msg.sendIP;
                                inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                                log(BLUE+"Coordinator: Received PROPOSAL_DONE from "+std::string(temp)+RESET);
                            }
                            if(proposeDoneCounter == noOfProposals)
                            {
                                Message msg;
                                msg.sendIP = myIP;
                                msg.isReq = true;
                                msg.msgType = MsgType::ACCEPTOR_DONE;
                                for(const in_addr_t &sin_addr : Participants)
                                {
                                    msg.recvIP = sin_addr;
                                    sendMessage(msg);
                                }
                                log(BLUE+"Coordinator: Coordinator send ACCEPTOR_DONE message to all acceptors"+RESET);
                                acceptorDone = true;
                                proposerDone = true;
                                break;
                            }
                        }
                    }
                    else
                    {
                        if(msg.recvIP != commonAcceptorIP)
                        {//Skip messages for other quorums
                            bool skip = false;

                            for(const in_addr_t recepient : commMap[msg.sendIP])
                            {
                                if(recepient == msg.recvIP)
                                {
                                    skip = true;
                                    break;
                                }
                            }
    
                            if(!skip) continue;
                        }
                        sendMessage(msg);
                    }
                }
                else
                {
                    sendMessage(msg);
                }
            }
        }
    }
}

/*This function is one of the coordinator modes*/
/*Simulate case 2 as per PPT*/
void back2back_2(RecvArgs recvArgs)
{
    log(BLUE+"Entered slide 64 functionality"+RESET);
    int proposeDoneCounter = 0;
    bool timeoutStatus = false;
    bool onlyOnce = false;
    bool myCondition = false;
    
    while(true)
    {
        if(proposerDone && acceptorDone)
        {
            log(BLUE+"Coordinator: All proposers and acceptors are terminated"+RESET);
            break;
        }

        if(!onlyOnce && myCondition)
        {
            Message msg = reqMap["127_128"];
            reqMap.erase("127_128");
            sendMessage(msg);
            log(BLUE+"Coordinator is sending PROPOSAL_OK to 127"+RESET);

            msg = reqMap["123_123"];
            reqMap.erase("123_123");
            sendMessage(msg);
            log(BLUE+"Coordinator is sending PREPARE message from 123 to 123"+RESET);

            msg = reqMap["123_124"];
            reqMap.erase("123_124");
            sendMessage(msg);
            log(BLUE+"Coordinator is sending PREPARE message from 123 to 124"+RESET);

            onlyOnce = true;
        }

        {
            std::unique_lock<std::mutex> lock(q_Mtx);
            timeoutStatus = q_cv.wait_for(lock, std::chrono::milliseconds(100), [&]{return !recvQueue.empty();});
            //q_cv.wait(lock, []{ return !recvQueue.empty(); });
        }
        //log(BLUE+"Coordinator: Is receive queue empty"+std::to_string(!recvQueue.empty())+RESET);
        if((!timeoutStatus) || (!recvQueue.empty()))
        {
            Node* tempNode = recvQueue.dequeue();
            if(tempNode != nullptr)
            {
                Message msg = tempNode->msg;

                char sendIP[INET_ADDRSTRLEN], recvIP[INET_ADDRSTRLEN];
                std::string sendNode, recvNode;
                inet_ntop(AF_INET, &msg.sendIP, sendIP, INET_ADDRSTRLEN);
                inet_ntop(AF_INET, &msg.recvIP, recvIP, INET_ADDRSTRLEN);

                int endDotPos = std::string(sendIP).find_last_of(".");
                sendNode = (std::string(sendIP)).substr(endDotPos+1, strlen(sendIP)-endDotPos);
                endDotPos = std::string(recvIP).find_last_of(".");
                recvNode = (std::string(recvIP)).substr(endDotPos+1, strlen(recvIP)-endDotPos);

                std::string key = sendNode + "_" + recvNode;

                delete(tempNode);
                {
                    /*
                    char temp[INET_ADDRSTRLEN];
                    in_addr tempAddr;
                    if(msg.isReq)
                    {
                        tempAddr.s_addr = msg.sendIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent by " << temp << std::endl;
                        tempAddr.s_addr = msg.recvIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent for " << temp << std::endl;
                    }
                    else
                    {
                        tempAddr.s_addr = msg.recvIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent by " << temp << std::endl;
                        tempAddr.s_addr = msg.sendIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent for " << temp << std::endl;
                    }*/
                }
                if(msg.isReq)
                {
                    //log(BLUE+"Coordinator:--- Received message is request for "+std::to_string(msg.recvIP)+" from "+std::to_string(msg.sendIP)+RESET);
                    if(msg.recvIP == myIP)
                    {
                        //std::cout << "Message was received for coordinator" << std::endl;
                        //std::cout << "Message type is " << type2str(msg.msgType) << std::endl;
                        if(msg.msgType == MsgType::PROPOSAL_REQ)
                        {
                            msg.msgType = MsgType::PROPOSAL_OK;
                            msg.isReq = false;

                            if((!onlyOnce) && (msg.sendIP == 2131077312))
                            {
                                reqMap[key] = msg;
                                continue;
                            }

                            sendMessage(msg);
                            {
                                char temp[INET_ADDRSTRLEN];
                                in_addr tempAddr;
                                tempAddr.s_addr = msg.sendIP;
                                inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                                log(BLUE+"Coordinator: Coordinator accepted PROPOSAL_REQ from "+std::string(temp)+RESET);
                            }
                            //std::cout << "Coordinator has send approval for proposer's request" << std::endl;
                        }
                        else if(msg.msgType == MsgType::PROPOSER_DONE)
                        {
                            msg.isReq = false;
                            proposeDoneCounter++;
                            
                            sendMessage(msg);
                            {
                                char temp[INET_ADDRSTRLEN];
                                in_addr tempAddr;
                                tempAddr.s_addr = msg.sendIP;
                                inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                                log(BLUE+"Coordinator: Received PROPOSAL_DONE from "+std::string(temp)+RESET);
                            }
                            if(proposeDoneCounter == noOfProposals)
                            {
                                Message msg;
                                msg.sendIP = myIP;
                                msg.isReq = true;
                                msg.msgType = MsgType::ACCEPTOR_DONE;
                                for(const in_addr_t &sin_addr : Participants)
                                {
                                    msg.recvIP = sin_addr;
                                    sendMessage(msg);
                                }
                                log(BLUE+"Coordinator: Coordinator send ACCEPTOR_DONE message to all acceptors"+RESET);
                                acceptorDone = true;
                                proposerDone = true;
                                break;
                            }
                        }
                    }
                    else
                    {
                        if(msg.recvIP != commonAcceptorIP)
                        {//Skip messages for other quorums
                            bool skip = false;

                            for(const in_addr_t recepient : commMap[msg.sendIP])
                            {
                                if(recepient == msg.recvIP)
                                {
                                    skip = true;
                                    break;
                                }
                            }
    
                            if(!skip) continue;
                            if(!onlyOnce)
                            {//review this case
                                reqMap[key] = msg;
                                continue;
                            }
                        }
                        sendMessage(msg); 
                    }
                }
                else
                {
                    if((msg.msgType == MsgType::ACCEPTED)&&(msg.recvIP == commonAcceptorIP) && (msg.sendIP == 2063968448))
                    {
                        myCondition = true;
                        log(BLUE+"Coordinator sent ACCEPTED from 125[COMMON_ACCEPTOR] to 123 before consensus is reached"+RESET);
                    }
                    sendMessage(msg);
                }
            }
        }
    }
}

/*This function is one of the coordinator modes*/
/*Simulate case 3 as per PPT*/
void back2back_3(RecvArgs recvArgs)
{
    log(BLUE+"Entered slide 64 functionality"+RESET);
    int proposeDoneCounter = 0;
    bool timeoutStatus = false;
    bool onlyOnce = false;
    bool onlyOnce1 = false;
    
    while(true)
    {
        if(proposerDone && acceptorDone)
        {
            log(BLUE+"Coordinator: All proposers and acceptors are terminated"+RESET);
            break;
        }

        {
            std::unique_lock<std::mutex> lock(q_Mtx);
            timeoutStatus = q_cv.wait_for(lock, std::chrono::milliseconds(100), [&]{return !recvQueue.empty();});
            //q_cv.wait(lock, []{ return !recvQueue.empty(); });
        }
        //log(BLUE+"Coordinator: Is receive queue empty"+std::to_string(!recvQueue.empty())+RESET);
        if((!timeoutStatus) || (!recvQueue.empty()))
        {
            Node* tempNode = recvQueue.dequeue();
            if(tempNode != nullptr)
            {
                Message msg = tempNode->msg;

                char sendIP[INET_ADDRSTRLEN], recvIP[INET_ADDRSTRLEN];
                std::string sendNode, recvNode;
                inet_ntop(AF_INET, &msg.sendIP, sendIP, INET_ADDRSTRLEN);
                inet_ntop(AF_INET, &msg.recvIP, recvIP, INET_ADDRSTRLEN);

                int endDotPos = std::string(sendIP).find_last_of(".");
                sendNode = (std::string(sendIP)).substr(endDotPos+1, strlen(sendIP)-endDotPos);
                endDotPos = std::string(recvIP).find_last_of(".");
                recvNode = (std::string(recvIP)).substr(endDotPos+1, strlen(recvIP)-endDotPos);

                std::string key = sendNode + "_" + recvNode;

                delete(tempNode);
                {
                    /*
                    char temp[INET_ADDRSTRLEN];
                    in_addr tempAddr;
                    if(msg.isReq)
                    {
                        tempAddr.s_addr = msg.sendIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent by " << temp << std::endl;
                        tempAddr.s_addr = msg.recvIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent for " << temp << std::endl;
                    }
                    else
                    {
                        tempAddr.s_addr = msg.recvIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent by " << temp << std::endl;
                        tempAddr.s_addr = msg.sendIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent for " << temp << std::endl;
                    }*/
                }
                if(msg.isReq)
                {
                    //log(BLUE+"Coordinator:--- Received message is request for "+std::to_string(msg.recvIP)+" from "+std::to_string(msg.sendIP)+RESET);
                    if(msg.recvIP == myIP)
                    {
                        //std::cout << "Message was received for coordinator" << std::endl;
                        //std::cout << "Message type is " << type2str(msg.msgType) << std::endl;
                        if(msg.msgType == MsgType::PROPOSAL_REQ)
                        {
                            msg.msgType = MsgType::PROPOSAL_OK;
                            msg.isReq = false;

                            sendMessage(msg);
                            {
                                char temp[INET_ADDRSTRLEN];
                                in_addr tempAddr;
                                tempAddr.s_addr = msg.sendIP;
                                inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                                log(BLUE+"Coordinator: Coordinator accepted PROPOSAL_REQ from "+std::string(temp)+RESET);
                            }
                            //std::cout << "Coordinator has send approval for proposer's request" << std::endl;
                        }
                        else if(msg.msgType == MsgType::PROPOSER_DONE)
                        {
                            msg.isReq = false;
                            proposeDoneCounter++;
                            
                            sendMessage(msg);
                            {
                                char temp[INET_ADDRSTRLEN];
                                in_addr tempAddr;
                                tempAddr.s_addr = msg.sendIP;
                                inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                                log(BLUE+"Coordinator: Received PROPOSAL_DONE from "+std::string(temp)+RESET);
                            }
                            if(proposeDoneCounter == noOfProposals)
                            {
                                Message msg;
                                msg.sendIP = myIP;
                                msg.isReq = true;
                                msg.msgType = MsgType::ACCEPTOR_DONE;
                                for(const in_addr_t &sin_addr : Participants)
                                {
                                    msg.recvIP = sin_addr;
                                    sendMessage(msg);
                                }
                                log(BLUE+"Coordinator: Coordinator send ACCEPTOR_DONE message to all acceptors"+RESET);
                                acceptorDone = true;
                                proposerDone = true;
                                break;
                            }
                        }
                    }
                    else
                    {
                        if(msg.recvIP != commonAcceptorIP)
                        {//Skip messages for other quorums
                            bool skip = false;

                            for(const in_addr_t recepient : commMap[msg.sendIP])
                            {
                                if(recepient == msg.recvIP)
                                {
                                    skip = true;
                                    break;
                                }
                            }
    
                            if(!skip) continue;
                            else sendMessage(msg);

                        }
                        else
                        {
                            if(!onlyOnce)
                            {
                                if((msg.msgType == MsgType::PREPARE) && ((key == "123_125") || (key == "127_125")))
                                {
                                    reqMap[key] = msg;
                                    if((reqMap.find("123_125") != reqMap.end()) && (reqMap.find("127_125") != reqMap.end()))
                                    {
                                        msg = reqMap["123_125"];
                                        reqMap.erase("123_125");
                                        sendMessage(msg);
                                        log(BLUE+"Coordinator: Sent PREPARE from 123 to 125"+RESET);

                                        msg = reqMap["127_125"];
                                        reqMap.erase("127_125");
                                        sendMessage(msg);
                                        log(BLUE+"Coordinator: Sent PREPARE from 127 to 125"+RESET);
                                        onlyOnce = true;
                                    }
                                }
                            }
                            else if(!onlyOnce1 && onlyOnce)
                            {
                                if((msg.msgType == MsgType::ACCEPT) && ((key == "123_125") || (key == "127_125")))
                                {
                                    reqMap[key] = msg;
                                    if((reqMap.find("123_125") != reqMap.end()) && (reqMap.find("127_125") != reqMap.end()))
                                    {
                                        msg = reqMap["127_125"];
                                        reqMap.erase("127_125");
                                        sendMessage(msg);
                                        log(BLUE+"Coordinator: Sent ACCEPT from 127 to 125"+RESET);

                                        msg = reqMap["123_125"];
                                        reqMap.erase("123_125");
                                        sendMessage(msg);
                                        log(BLUE+"Coordinator: Sent ACCEPT from 123 to 125"+RESET);
                                        onlyOnce1 = true;
                                    }
                                }
                            }
                            else
                            {
                                sendMessage(msg);
                            }
                        }
                    }
                }
                else
                {
                    sendMessage(msg);
                }
            }
        }
    }
}

/*This function is one of the coordinator modes*/
/*Simulate case 4 as per PPT*/
void livelock(RecvArgs recvArgs)
{
    int proposeDoneCounter = 0;
    int conditionCounter  = 0;
    bool onlyonce1 = false;
    bool order = true;  //if true then send 123 messages, else send 127 messages
    while(true)
    {
        if((conditionCounter == 4) && (acceptedCounter == 1) && (!onlyOnce))
        {
            Message msg = reqMap["127_125"];
            sendMessage(msg);
            msg = reqMap["123_125"];
            sendMessage(msg);
            msg = reqMap["127_126"];
            sendMessage(msg);
            msg = reqMap["123_123"];
            sendMessage(msg);
            msg = reqMap["127_127"];
            sendMessage(msg);
            msg = reqMap["123_124"];
            sendMessage(msg);
            onlyOnce = true; 
        }

        {
            std::unique_lock<std::mutex> lock(q_Mtx);
            q_cv.wait(lock, []{ return !recvQueue.empty(); });
        }

        Node* tempNode = recvQueue.dequeue();
        if(tempNode != nullptr)
        {
            Message msg = tempNode->msg;

            char sendIP[INET_ADDRSTRLEN], recvIP[INET_ADDRSTRLEN];
            std::string sendNode, recvNode;
            inet_ntop(AF_INET, &msg.sendIP, sendIP, INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &msg.recvIP, recvIP, INET_ADDRSTRLEN);

            int endDotPos = std::string(sendIP).find_last_of(".");
            sendNode = (std::string(sendIP)).substr(endDotPos+1, strlen(sendIP)-endDotPos);
            endDotPos = std::string(recvIP).find_last_of(".");
            recvNode = (std::string(recvIP)).substr(endDotPos+1, strlen(recvIP)-endDotPos);

            std::string key = sendNode + "_" + recvNode;

            if(msg.isReq)
            {
                if(msg.recvIP == myIP)
                {
                    if(msg.msgType == MsgType::PROPOSAL_REQ)
                    {
                        msg.isReq = false;
                        proposeDoneCounter++;
                        sendMessage(msg);
                    }
                }
                else
                {
                    {//Skip messages for other quorums
                        bool skip = false;

                        for(const in_addr_t recepient : commMap[msg.sendIP])
                            if(recepient == msg.recvIP)
                                skip = true;

                        if(skip)
                            continue;
                    }

                    if(!onlyonce1)
                    {
                       if(((msg.msgType == MsgType::PREPARE) && (sendNode == "127")) || ((msg.msgType == MsgType::ACCEPT) && (sendNode == "123")))
                       {
                            reqMap[key] = msg;
                       }
                       else
                       {
                            sendMessage(msg);
                       }
                    }
                    else
                    {
                        if(order)
                        {
                            if((msg.msgType == MsgType::PREPARE) && (sendNode == "127"))
                            {
                                reqMap[key] = msg;
                            }
                            else
                            {
                               sendMessage(msg);
                               if((msg.msgType == MsgType::PREPARE) && (sendNode == "123")) conditionCounter++;
                               if(conditionCounter == 3)
                               {
                                msg = reqMap["127_125"];
                                sendMessage(msg);
                                msg = reqMap["127_126"];
                                sendMessage(msg);
                                msg = reqMap["127_127"];
                                sendMessage(msg);
                               }
                            }
                        }
                        else
                        {
                            if((msg.msgType == MsgType::PREPARE) && (sendNode == "123"))
                            {
                                reqMap[key] = msg;
                            }
                            else
                            {
                               sendMessage(msg); 
                            }
                        }
                    }

                    if(((msg.msgType == MsgType::PREPARE) && (sendNode == "127")) || ((msg.msgType == MsgType::ACCEPT)&&(sendNode == "123")&&(recvNode != "125")))
                    {
                        //Buffer messages from 127
                        reqMap[key] = msg;
                        continue;
                    }
                    else
                    {
                        sendMessage(msg);
                        conditionCounter++;
                    }
                }
            }
            else
            {
                if((msg.msgType == MsgType::ACCEPTED) && (recvNode == "125") && (sendNode == "123")) acceptedCounter++;
                sendMessage(msg);
            }
        }
        if(proposeDoneCounter == 2) break;
    }
}

/*This function is one of the coordinator modes*/
/*Simulate a simple message queue*/
void messageQueue(RecvArgs recvArgs)
{
    log(BLUE+"Entered message queue functionality"+RESET);
    int proposeDoneCounter = 0;
    bool timeoutStatus = false;
    while(true)
    {
        if(proposerDone && acceptorDone)
        {
            log(BLUE+"Coordinator: All proposers and acceptors are terminated"+RESET);
            break;
        }

        {
            std::unique_lock<std::mutex> lock(q_Mtx);
            timeoutStatus = q_cv.wait_for(lock, std::chrono::milliseconds(100), [&]{return !recvQueue.empty();});
            //q_cv.wait(lock, []{ return !recvQueue.empty(); });
        }
        //log(BLUE+"Coordinator: Is receive queue empty"+std::to_string(!recvQueue.empty())+RESET);
        if((!timeoutStatus) || (!recvQueue.empty()))
        {
            Node* tempNode = recvQueue.dequeue();
            if(tempNode != nullptr)
            {
                Message msg = tempNode->msg;
                delete(tempNode);
                {
                    /*
                    char temp[INET_ADDRSTRLEN];
                    in_addr tempAddr;
                    if(msg.isReq)
                    {
                        tempAddr.s_addr = msg.sendIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent by " << temp << std::endl;
                        tempAddr.s_addr = msg.recvIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent for " << temp << std::endl;
                    }
                    else
                    {
                        tempAddr.s_addr = msg.recvIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent by " << temp << std::endl;
                        tempAddr.s_addr = msg.sendIP;
                        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                        std::cout << "Message was sent for " << temp << std::endl;
                    }*/
                }
                if(msg.isReq)
                {
                    //log(BLUE+"Coordinator:--- Received message is request for "+std::to_string(msg.recvIP)+" from "+std::to_string(msg.sendIP)+RESET);
                    if(msg.recvIP == myIP)
                    {
                        //std::cout << "Message was received for coordinator" << std::endl;
                        //std::cout << "Message type is " << type2str(msg.msgType) << std::endl;
                        if(msg.msgType == MsgType::PROPOSAL_REQ)
                        {
                            {
                                char temp[INET_ADDRSTRLEN];
                                in_addr tempAddr;
                                tempAddr.s_addr = msg.sendIP;
                                inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                                log(BLUE+"Coordinator: Coordinator accepted PROPOSAL_REQ from "+std::string(temp)+RESET);
                            }
                            msg.msgType = MsgType::PROPOSAL_OK;
                            msg.isReq = false;
                            sendMessage(msg);
                            //std::cout << "Coordinator has send approval for proposer's request" << std::endl;
                        }
                        else if(msg.msgType == MsgType::PROPOSER_DONE)
                        {
                            msg.isReq = false;
                            proposeDoneCounter++;
                            sendMessage(msg);
                            {
                                char temp[INET_ADDRSTRLEN];
                                in_addr tempAddr;
                                tempAddr.s_addr = msg.sendIP;
                                inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
                                log(BLUE+"Coordinator: Received PROPOSAL_DONE from "+std::string(temp)+RESET);
                            }
                            if(proposeDoneCounter == noOfProposals)
                            {
                                Message msg;
                                msg.sendIP = myIP;
                                msg.isReq = true;
                                msg.msgType = MsgType::ACCEPTOR_DONE;
                                for(const in_addr_t &sin_addr : Participants)
                                {
                                    msg.recvIP = sin_addr;
                                    sendMessage(msg);
                                }
                                log(BLUE+"Coordinator: Coordinator send ACCEPTOR_DONE message to all acceptors"+RESET);
                                acceptorDone = true;
                                proposerDone = true;
                                break;
                            }
                        }
                    }
                    else
                    {
                        if(msg.recvIP != commonAcceptorIP)
                        {//Skip messages for other quorums
                            bool skip = false;

                            for(const in_addr_t recepient : commMap[msg.sendIP])
                            {
                                if(recepient == msg.recvIP)
                                {
                                    skip = true;
                                    break;
                                }
                            }
    
                            if(!skip) continue;
                        }
                        sendMessage(msg);
                    }
                }
                else
                {
                    sendMessage(msg);
                }
            }
        }
    }
}


/*This is receive thread*/
void* receiveThread(void* arg)
{
    RecvArgs recvArgs = *(RecvArgs*)arg;
    while(true)
    {
        Message msg;
        sockaddr_in recvAddr;
        while(true)
        {
            if(withCoordinator && isCoordinator && proposerDone && acceptorDone) break;
            bool status = receiveMessage(recvArgs, msg, recvAddr, MSG_DONTWAIT);
            if(status)
            {//Mutex lock for enqueuing node in recvQueue
                recvQueue.enqueue(msg, recvAddr);
                q_cv.notify_one();
                break;
            }
        }
        if((!withCoordinator)&&(msg.msgType == MsgType::PROPOSER_DONE))
        {
            proposerDoneCounter++;
            if(proposerDoneCounter == noOfProposals)
            {
                acceptorDone = true;
                break;
            }
        }
        if((withCoordinator) && (!isCoordinator) && (msg.msgType == MsgType::ACCEPTOR_DONE))
        {
            acceptorDone = true;
            break;
        }
        /*
        if((withCoordinator) && (isCoordinator) && (msg.msgType == MsgType::PROPOSER_DONE))
        {
            proposerDoneCounter++;
            acceptorDone = true;
            break;
        }
        */
        //log(BLUE+"The exit flag status are "+std::to_string(proposerDone)+"---"+std::to_string(acceptorDone)+RESET);
        if(withCoordinator && isCoordinator && proposerDone && acceptorDone) break;
    }
    return nullptr;
}

/*This is coordinator's thread*/
void* coordinatorThread(void* arg)
{
    RecvArgs recvArgs = *(RecvArgs*)arg;
    switch(CoordinatorMode::B2B_3)
    {
        case CoordinatorMode::B2B_1:
        {
            back2back_1(recvArgs);
            break;
        }
        case CoordinatorMode::B2B_2:
        {
            back2back_2(recvArgs);
            break;
        }
        case CoordinatorMode::B2B_3:
        {
            back2back_3(recvArgs);
            break;
        }
        case CoordinatorMode::LIVELOCK:
        {
            livelock(recvArgs);
            break;
        }
        case CoordinatorMode::MSG_Q:
        default:
        {
            messageQueue(recvArgs);
            break;
        }
    }
    return nullptr;
}

/*This is acceptor's thread*/
void* acceptorThread(void* arg)
{
    RecvArgs commArgs = *(RecvArgs*)arg;
    bool timeoutStatus = false;
    while(true)
    {
        if(acceptorDone)
        {
            break;
        }
        {
            std::unique_lock<std::mutex> lock(q_Mtx);
            timeoutStatus = q_cv.wait_for(lock, std::chrono::milliseconds(100), [&]{return !recvQueue.empty();});
            //q_cv.wait(lock, []{ return !recvQueue.empty(); });
        }

        {
            std::unique_lock<std::mutex> lock(q_Mtx);
            q_cv.wait(lock, []{ return !recvQueue.empty(); });
        }

        if(!timeoutStatus || (!recvQueue.empty()))
        {
            Node* recvNode = recvQueue.peek();
            if(recvNode == nullptr){}
            else
            {
                if(recvNode->msg.isReq)
                {
                    recvNode = recvQueue.dequeue();
                    if(recvNode == nullptr) continue;
                    char temp[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &recvNode->msg.sendIP, temp, INET_ADDRSTRLEN);
                    #if 0
                    if(recvNode->msg.msgType == MsgType::PREPARE)
                    {
                        acceptorMessage.isReq = false;
                        if(recvNode->msg.proposalNumber > acceptorMessage.proposalNumber)
                        {
                            acceptorMessage.proposalNumber = recvNode->msg.proposalNumber;
                            log(MAGENTA+"ACCEPTOR: Acceptor accepted PREPARE message("+std::to_string(recvNode->msg.proposalNumber)+") from "+std::string(temp)+", with minProposal as "+std::to_string(acceptorMessage.proposalNumber)+RESET);
                            updateMaxRound(recvNode->msg);
                        }
                        else
                        {
                            log(MAGENTA+"ACCEPTOR: Acceptor rejected PREPARE message("+std::to_string(recvNode->msg.proposalNumber)+") from "+std::string(temp)+", with minProposal as "+std::to_string(acceptorMessage.proposalNumber)+RESET);
                        }
                        acceptorMessage.recvIP = recvNode->msg.recvIP;
                        acceptorMessage.sendIP = recvNode->msg.sendIP;
                        acceptorMessage.msgType = MsgType::PROMISE;
                        //acceptorMessage.lastAcceptedProposal = recvNode->msg.proposalNumber;
                        //acceptorMessage.lastAcceptedValue = recvNode->msg.proposalValue;
                        sendMessage(acceptorMessage);
                    }
                    else if(recvNode->msg.msgType == MsgType::ACCEPT)
                    {
                        acceptorMessage.isReq = false;
                        if(recvNode->msg.proposalNumber >= acceptorMessage.proposalNumber) // Use >= to handle equality
                        {
                            acceptorMessage.lastAcceptedProposal = recvNode->msg.proposalNumber;
                            acceptorMessage.lastAcceptedValue = recvNode->msg.proposalValue;
                            acceptorMessage.proposalNumber = recvNode->msg.proposalNumber;
                        }
                        log(MAGENTA+"ACCEPTOR: Acceptor accepted ACCEPT message("+std::to_string(recvNode->msg.proposalNumber)+") from "+std::string(temp)+", minProposal["+std::to_string(acceptorMessage.proposalNumber)+"] and proposalValue["+std::to_string(acceptorMessage.lastAcceptedValue)+"]"+RESET);

                        acceptorMessage.recvIP = myIP;
                        acceptorMessage.sendIP = recvNode->msg.sendIP;
                        acceptorMessage.msgType = MsgType::ACCEPTED;
                        sendMessage(acceptorMessage);
                    }
                    #else
                    if(recvNode->msg.msgType == MsgType::PREPARE)
                    {
                        acceptorMessage.isReq = false;

                        // If this proposal number is greater than any we have promised
                        if(recvNode->msg.proposalNumber > acceptorMessage.proposalNumber)
                        {
                            acceptorMessage.proposalNumber = recvNode->msg.proposalNumber;
                            
                            // Log and update state
                            log(MAGENTA+"ACCEPTOR: Acceptor accepted PREPARE message("+std::to_string(recvNode->msg.proposalNumber)+") from "+std::string(temp)+", with minProposal as "+std::to_string(acceptorMessage.proposalNumber)+RESET);
                            updateMaxRound(recvNode->msg);

                            // Send a PROMISE back with the last accepted proposal and value
                            acceptorMessage.lastAcceptedProposal = acceptedProposal;
                            acceptorMessage.lastAcceptedValue = acceptedValue;
                        }
                        else
                        {
                            // Log rejection of PREPARE
                            log(MAGENTA+"ACCEPTOR: Acceptor rejected PREPARE message("+std::to_string(recvNode->msg.proposalNumber)+") from "+std::string(temp)+", with minProposal as "+std::to_string(acceptorMessage.proposalNumber)+RESET);
                        }
                        
                        // Prepare the promise message
                        acceptorMessage.recvIP = recvNode->msg.recvIP;
                        acceptorMessage.sendIP = recvNode->msg.sendIP;
                        acceptorMessage.msgType = MsgType::PROMISE;

                        // Send the promise back
                        sendMessage(acceptorMessage);
                    }
                    else if(recvNode->msg.msgType == MsgType::ACCEPT)
                    {
                        acceptorMessage.isReq = false;

                        // ACCEPT logic: Only accept if proposalNumber matches or exceeds the promised one
                        if(recvNode->msg.proposalNumber >= acceptorMessage.proposalNumber)
                        {
                            // Update state with accepted proposal and value
                            acceptedProposal = recvNode->msg.proposalNumber;
                            acceptedValue = recvNode->msg.proposalValue;

                            log(MAGENTA+"ACCEPTOR: Acceptor accepted ACCEPT message("+std::to_string(recvNode->msg.proposalNumber)+") from "+std::string(temp)+", with minProposal as "+std::to_string(acceptorMessage.proposalNumber)+RESET);
                        }
                        else
                        {
                            // If the proposal was less than promised, it is rejected implicitly
                            log(MAGENTA+"ACCEPTOR: Acceptor rejected ACCEPT message("+std::to_string(recvNode->msg.proposalNumber)+") from "+std::string(temp)+RESET);
                        }

                        // Send ACCEPTED message
                        acceptorMessage.recvIP = myIP;
                        acceptorMessage.sendIP = recvNode->msg.sendIP;
                        acceptorMessage.msgType = MsgType::ACCEPTED;
                        acceptorMessage.lastAcceptedProposal = acceptedProposal;
                        acceptorMessage.lastAcceptedValue = acceptedValue;
                        sendMessage(acceptorMessage);
                    }
                    #endif
                    delete(recvNode);
                }
            }
        }
    }
    return nullptr;
}

/*This is proposer's thread*/
void* proposerThread(void* arg)
{
    RecvArgs recvArgs = *(RecvArgs*)arg;
    proposerMessage.sendIP = myIP;
    MsgType currentState = MsgType::PROPOSAL_REQ;
    while(true)
    {
        if(currentState == MsgType::PROPOSER_DONE) break;
        switch(currentState)
        {
            case MsgType::PROPOSAL_REQ:
            {
                proposerMessage.isReq = true;
                proposerMessage.proposalValue = myNodeID;
                proposerMessage.msgType = MsgType::PROPOSAL_REQ;
                proposerMessage.sendIP = myIP;
                sockaddr_in sendAddr;
                socklen_t sendAddrLen = sizeof(sendAddr);

                if(withCoordinator)
                {
                    proposerMessage.recvIP = coordinatorIP;
                    bool timeoutStatus = false;
                    while(true)
                    {
                        sendMessage(proposerMessage);
                        log(GREEN+"PROPOSER:Proposer sent a request to coordinator"+RESET);
                        {
                            std::unique_lock<std::mutex> lock(q_Mtx);
                            timeoutStatus = q_cv.wait_for(lock, std::chrono::milliseconds(100), [&]{ return !recvQueue.empty(); });
                            //q_cv.wait(lock, []{ return !recvQueue.empty(); });
                        }
                        
                        if(!timeoutStatus || !recvQueue.empty())
                        {
                            Node* recvNode = recvQueue.peek();
                            if(recvNode == nullptr) continue;
                            else
                            {
                                if(recvNode->msg.isReq == false)
                                {
                                    recvNode = recvQueue.dequeue();
                                    if(recvNode == nullptr) continue;
                                    if(recvNode->msg.msgType == MsgType::PROPOSAL_OK)
                                    {
                                        log(GREEN+"PROPOSER:Received a proposal acceptance from coordinator"+RESET);
                                        currentState = MsgType::PREPARE;
                                        delete(recvNode);
                                        break;
                                    }
                                    delete(recvNode);
                                }
                            }
                        }
                    }
                }
                else
                {
                    log(GREEN+"PROPOSER:Proposer is entering PREPARE phase"+RESET);
                    currentState = MsgType::PREPARE;
                    break;
                }

                break;
            }
            case MsgType::PREPARE:
            {
                proposerMessage.isReq = true;
                proposerMessage.msgType = MsgType::PREPARE;
                proposerMessage.proposalValue = myNodeID;
                proposerMessage.proposalNumber = updateMaxRound(proposerMessage);
                if(prepare(recvArgs, proposerMessage))
                {
                    log(GREEN+"PROPOSER:Proposer is entering ACCEPT phase"+RESET);
                    currentState = MsgType::ACCEPT;
                }
                else
                {
                    currentState = MsgType::PREPARE;
                    proposerMessage.proposalNumber = updateMaxRound(proposerMessage);
                    log(GREEN+"PROPOSER:Proposer is going to back off due to rejection in PREPARE phase"+RESET);
                    backOff();
                }
                break;
            }
            case MsgType::ACCEPT:
            {
                proposerMessage.isReq = true;
                proposerMessage.msgType = MsgType::ACCEPT;
                if(accept(recvArgs, proposerMessage))
                {
                    if(withCoordinator)
                    {
                        proposerMessage.recvIP = coordinatorIP;
                        proposerMessage.isReq = true;
                        proposerMessage.msgType = MsgType::PROPOSER_DONE;
                        sendMessage(proposerMessage);
                        log(GREEN+"PROPOSER:Proposer sent PROPOSER_DONE request to coordinator"+RESET);
                    }
                    else
                    {
                        proposerMessage.msgType = MsgType::PROPOSER_DONE;
                        for(const in_addr_t &recv_IP : Participants)
                        {
                            proposerMessage.recvIP = recv_IP;
                            sendMessage(proposerMessage);
                        }
                        log(GREEN+"PROPOSER:Proposer sent PROPOSER_DONE request to all acceptors"+RESET);
                    }
                    currentState = MsgType::PROPOSER_DONE;
                    logger.log("COMPLETED", proposerMessage);
                }
                else
                {
                    backOff();
                    currentState = MsgType::PROPOSAL_REQ;
                }
                break;
            }
            case MsgType::ACCEPTED:
            case MsgType::ACCEPTOR_DONE:
            case MsgType::PROMISE:
            case MsgType::PROPOSAL_OK:
            case MsgType::PROPOSER_DONE:
            {
                perror("Invalid case for proposer");
                exit(EXIT_FAILURE);
            }
        }
    }
    proposerDone = true;
    return nullptr;
}

/*This main function will take care of socket configuration and thread binding*/
int main(int argc, char* argv[])
{
    if(argc != 2)
    {
        std::cerr << "Invalid number of arguments!!" << std::endl;
        std::cerr << "Valid order is \"" << argv[0] << " configFile.txt" << std::endl;
        exit(EXIT_FAILURE);
    }

    //Extract my IP address and nodeID
    extractMyIP();
    in_addr temp;
    temp.s_addr = myIP;
    char tempAddr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &temp, tempAddr, INET_ADDRSTRLEN);
    log(BLUE+"Main: Extracted my IP address("+ std::string(tempAddr) +")"+RESET);
    //Parsing configFile
    parseConfig(argv[1]);
    log(BLUE+"Main: Parsed config file"+RESET);

    if(!isProposer) proposerDone = true;

    

    //Receive port configuration------------------------------------------------------
    sockaddr_in recvAddr;
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(RECV_PORT);
    recvAddr.sin_addr.s_addr = myIP;

    /*std::cout << "Coordinator IP is (uint32)" << coordinatorIP << std::endl;
    {
        char temp[INET_ADDRSTRLEN];
        in_addr tempAddr;
        tempAddr.s_addr = coordinatorIP;
        inet_ntop(AF_INET, &tempAddr, temp, INET_ADDRSTRLEN);
        std::cout << "Coordinator IP is (str)" << temp << std::endl;
    }*/

    int recvSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if(recvSocket == -1)
    {
        perror("Failed to create receive socket");
        exit(EXIT_FAILURE);
    }

    if(bind(recvSocket, (sockaddr*)&recvAddr, sizeof(recvAddr)) < 0)
    {
        perror("Main: Receive socket binding error");
        exit(EXIT_FAILURE);
    }
    log(BLUE+"Main: Created receive socket on port:"+std::to_string(RECV_PORT)+RESET);
    //--------------------------------------------------------------------------------

    RecvArgs commArgs = {recvAddr, recvSocket};

    if(isCoordinator)
    {
        //192.168.5.123 ---> 2063968448 --> PROPOSER
        //192.168.5.124 ---> 2080745664 --> ACCEPTOR
        //192.168.5.125 ---> 2097522880 --> ACCEPTOR(COMMON)
        //192.168.5.126 ---> 2114300096 --> ACCEPTOR
        //192.168.5.127 ---> 2131077312 --> PROPOSER
        //192.168.5.128 ---> 2147854528 --> COORDINATOR

        commMap[(in_addr_t){2063968448}] = {(in_addr_t){2063968448}, (in_addr_t){2080745664}};
        commMap[(in_addr_t){2131077312}] = {(in_addr_t){2131077312}, (in_addr_t){2114300096}};
        commonAcceptorIP = (in_addr_t){2097522880};
    }

    //Thread spawning------------------------------------------------------------------
    waitForMinute();
    if(isCoordinator)
    {//Coordinator and receive threads
        pthread_t CoordinatorThread, ReceiveThread;
        if(pthread_create(&ReceiveThread, nullptr, receiveThread, &commArgs) != 0)
        {
            perror("Failed to create receive thread.");
            exit(EXIT_FAILURE);
        }
        else log(BLUE+"Main: Created receive thread"+RESET);

        if(pthread_create(&CoordinatorThread, nullptr, coordinatorThread, &commArgs) != 0)
        {
            perror("Failed to create coordinator thread.");
            exit(EXIT_FAILURE);
        }
        else log(BLUE+"Main: Created coordinator thread"+RESET);

        pthread_join(ReceiveThread, nullptr);
        log(BLUE+"Main: Joined receive thread"+RESET);
        pthread_join(CoordinatorThread, nullptr);
        log(BLUE+"Main: Joined coordinator thread"+RESET);
    }
    else if(isProposer)
    {//Proposer, Acceptor and receive threads
        pthread_t ProposerThread, AcceptorThread, ReceiveThread;
        if(pthread_create(&ReceiveThread, nullptr, receiveThread, &commArgs) != 0)
        {
            perror("Failed to create receive thread");
            exit(EXIT_FAILURE);
        }
        else log(BLUE+"Main: Created receive thread"+RESET);
        
        if(pthread_create(&AcceptorThread, nullptr, acceptorThread, &commArgs) != 0)
        {
            perror("Failed to create acceptor thread");
            exit(EXIT_FAILURE);
        }
        else log(BLUE+"Main: Created acceptor thread"+RESET);

        if(pthread_create(&ProposerThread, nullptr, proposerThread, &commArgs) != 0)
        {
            perror("Failed to create proposer thread");
            exit(EXIT_FAILURE);
        }
        else log(BLUE+"Main: Created proposer thread"+RESET);

        pthread_join(ProposerThread, nullptr);
        log(BLUE+"Main: Joined proposer thread"+RESET);
        pthread_join(ReceiveThread, nullptr);
        log(BLUE+"Main: Joined receive thread"+RESET);
        pthread_join(AcceptorThread, nullptr);
        log(BLUE+"Main: Joined acceptor thread"+RESET);
    }
    else
    {//Acceptor and receive threads
        pthread_t AcceptorThread, ReceiveThread;
        if(pthread_create(&ReceiveThread, nullptr, receiveThread, &commArgs) != 0)
        {
            perror("Failed to create receive thread");
            exit(EXIT_FAILURE);
        }
        else log(BLUE+"Main: Created receive thread"+RESET);
        
        if(pthread_create(&AcceptorThread, nullptr, acceptorThread, &commArgs) != 0)
        {
            perror("Failed to create acceptor thread");
            exit(EXIT_FAILURE);
        }
        else log(BLUE+"Main: Created acceptor thread"+RESET);

        pthread_join(ReceiveThread, nullptr);
        log(BLUE+"Main: Joined receive thread"+RESET);
        pthread_join(AcceptorThread, nullptr);
        log(BLUE+"Main: Joined acceptor thread"+RESET);
    }
    
    //Close socket
    close(commArgs.recvSock);

    log(BLUE+"Main: Execution completed"+RESET);
    log(BLUE+"Main: Press any key to exit"+RESET);
    std::cin.get();
    return 0;
}