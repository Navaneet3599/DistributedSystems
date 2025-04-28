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

//#define SEND_PORT   12346
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

typedef struct Message
{//Structure for Proposer, Acceptor, Coordinator
    bool isReq;
    MsgType msgType;
    //For PREPARE
    long int proposalNumber;

    //For PROMISE
    long int lastAcceptedProposal;
    int lastAccpetedValue;
    
    //For ACCEPT
    int proposalValue;

    //Sender address for tracking
    in_addr sendIP;
    in_addr recvIP;
};

typedef struct RecvArgs
{
    sockaddr_in recvAddr;
    int recvSock;
};

typedef struct Node
{
    Node* next;
    Message msg;
    sockaddr_in recvAddr;
};

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
            front    = front->next;
            if (!front) rear = nullptr;
            return n;
        }

        bool empty()
        {
            if(front == nullptr) return true;
            else return false;
        }
};

class ActivityLog
{
    public:
        std::fstream MyFile;

        ActivityLog()
        {
            MyFile.open("localLog.txt", std::ios::in | std::ios::out | std::ios::app);  // Open for read & append
        }

        ~ActivityLog()
        {
            if (MyFile.is_open())
                MyFile.close();
        }

        std::string getCurrentTimeStamp()
        {
            auto now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            char buffer[20];  // "YYYY-MM-DD HH:MM:SS" = 19 chars + '\0'

            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));
            return std::string(buffer);
        }

        bool log(const Message msg, std::string txt)
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
                    MyFile << "PROMISE\t\t" << "[ACCEPT_NO:" << std::to_string(msg.lastAcceptedProposal) << "]\t\t" << "[ACCEPT_VAL:" << std::to_string(msg.lastAccpetedValue) << "]" << std::endl;
                else if(msg.msgType == MsgType::ACCEPTED)
                    MyFile << "ACCEPTED\t\t" << "[ACCPTD_NO:" << std::to_string(msg.lastAcceptedProposal) << "]" << std::endl;
                else if(msg.msgType == MsgType::PROPOSAL_OK)
                    MyFile << "PROPOSAL\t\t" << "[PRPSL_REQ:" << std::to_string(msg.proposalNumber) << "]" << std::endl;
                else if(msg.msgType == MsgType::PROPOSAL_OK)
                    MyFile << "PROPOSAL\t\t" << "[PRPSL_OK:" << std::to_string(msg.proposalNumber) << "]" << std::endl;
                else if(msg.msgType == MsgType::PROPOSER_DONE)
                    MyFile << "PROPOSER\t\t" << "[PRPSR_NO:" << std::to_string(msg.proposalNumber) << "]\t\t" << "[PRPSL_VAL:" << std::to_string(msg.proposalValue) << "]" << std::endl;
                else if(msg.msgType == MsgType::ACCEPTOR_DONE)
                    MyFile << "ACCEPTOR\t\t" << "[DONE]" << "]\t\t" << "[PRPSL_VAL:" << std::to_string(msg.proposalValue) << "]" << std::endl;
                return true;
            }
            return false;
        }
};


std::string GRAY  = "\033[90m";
std::string RESET = "\033[0m";
std::string BLUE  = "\033[34m";
std::mutex logMtx, str2typeMtx, type2strMtx, q_Mtx;
std::condition_variable q_cv;
std::set<in_addr> Participants;
std::unordered_map<in_addr, std::vector<in_addr>> commMap;
bool isProposer = false;
bool isCoordinator = false;
bool withCoordinator = false;
volatile bool exitThreads = false;

//For proposer
int proposerValue = 0;
long int highestAcceptedProposal = -1;
int myNodeID = 0;
in_addr coordinatorIP;
in_addr commonAcceptorIP;
in_addr myIP;

//For acceptor
long int minProposal = 0;
long int acceptedProposal = 0;
int acceptedValue = 0;

//For coordinator

//For receiver
RecvQueue recvQueue;

void extractMyIP();
void log(std::string text);
bool sendMessage(Message msg);
bool receiveMessage(RecvArgs recvArgs, Message& msg, sockaddr_in& recvAddr, int flags);
void waitForMinute(void);
bool waitTimeout(int timeoutDuration, std::chrono::time_point<std::chrono::steady_clock> msecTimeoutDuration);
bool prepare(socketAttributes sockAttr, Message msg);
bool accept(socketAttributes sockAttr, Message msg);
void backOff();
inline MsgType str2type(const std::string& s);
inline std::string type2str(const MsgType type);

void* coordinatorThread(void* arg);
void* proposerThread(void* arg);
void* acceptorThread(void* arg);

/*This function will update the max round based on last saved data*/
long int updateMaxRound(Message msg)
{
    long int last = 0;

    {
        std::fstream in("MaxRound.txt");
        long x = 0;
        while(in >> x) last = x;
    }

    last = std::max(last, (msg.proposalNumber >> 8));
    last = std::max(last, (msg.lastAcceptedProposal >> 8));
    last = last + 1;

    {
        std::fstream out("MaxRound.txt");
        out << last << std::endl;
    }

    last = (last << 8) | myNodeID;

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
                coordinatorIP = tempAddr.sin_addr;
                withCoordinator = true;
            }
            if(tempAddr.sin_addr.s_addr == myIP.s_addr)
            {
                if(role == "COORDINATOR") isCoordinator = true;
                if(role == "PROPOSER") isProposer = true;
            }
            if(role != "COORDINATOR")
            {
                
                Participants.insert(tempAddr.sin_addr);
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
            if(msg.isReq) sendAddr.sin_addr = msg.recvIP;
            else sendAddr.sin_addr = msg.sendIP;
        }
        else
        {
            sendAddr.sin_addr = coordinatorIP;
        }
    }
    else
    {
        if(msg.isReq) sendAddr.sin_addr = msg.recvIP;
        else sendAddr.sin_addr = msg.sendIP;
    }
    
    int sendSock = socket(AF_INET, SOCK_DGRAM, 0);
    int sentBytes = sendto(sendSock, &msg, sizeof(msg), 0, (sockaddr*)(&sendAddr), sendAddrlen);
    if(sentBytes < 0)
    {
        perror("Error in sending data");
        exit(EXIT_FAILURE);
    }
    else
    {
        char IP_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sendAddr.sin_addr, IP_str, INET_ADDRSTRLEN);
        if(isProposer)
            log(GRAY+"Proposer: Sent [message:"+type2str(msg.msgType)+"] to [address:"+IP_str+"] at [port:"+std::to_string((uint16_t)ntohs(sendAddr.sin_port))+RESET);
        else if(isCoordinator)
            log(GRAY+"Coordinator: Sent [message:"+type2str(msg.msgType)+"] to [address:"+IP_str+"] at [port:"+std::to_string((uint16_t)ntohs(sendAddr.sin_port))+RESET);
        else
            log(GRAY+"Acceptor: Sent [message:"+type2str(msg.msgType)+"] to [address:"+IP_str+"] at [port:"+std::to_string((uint16_t)ntohs(sendAddr.sin_port))+RESET);
        return true;
    }
}

/*This function will receive message using UDP*/
bool receiveMessage(RecvArgs recvArgs, Message& msg, sockaddr_in& recvAddr, int flags)
{
    socklen_t recvAddrLen = sizeof(recvAddr);
    int receivedBytes = recvfrom(recvArgs.recvSock, &msg, sizeof(msg), flags, (sockaddr*)(&recvAddr), &recvAddrLen);
    if(receivedBytes < 0)
    {
        perror("Error in sending data");
        return false;
    }
    else
    {
        char IP_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &recvAddr.sin_addr, IP_str, INET_ADDRSTRLEN);
        
        //Update the maxRound for every received message
        (void)updateMaxRound(msg);

        if(isProposer)
            log(GRAY+"Proposer: Received [message:"+type2str(msg.msgType)+"] from [address:"+IP_str+"] at [port:"+std::to_string((uint16_t)ntohs(recvAddr.sin_port))+RESET);
        else if(isCoordinator)
            log(GRAY+"Coordinator: Received [message:"+type2str(msg.msgType)+"] from [address:"+IP_str+"] at [port:"+std::to_string((uint16_t)ntohs(recvAddr.sin_port))+RESET);
        else
            log(GRAY+"Acceptor: Received [message:"+type2str(msg.msgType)+"] from [address:"+IP_str+"] at [port:"+std::to_string((uint16_t)ntohs(recvAddr.sin_port))+RESET);
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
    myIP = tempAddr.sin_addr;
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
bool prepare(RecvArgs recvArgs, Message msg)
{
    for(const in_addr &sin_addr : Participants)
    {
        msg.recvIP = sin_addr;
        sendMessage(msg);
    }

    std::set<in_addr> TempList(Participants);
    bool timeoutStatus = false;
    {
        std::unique_lock<std::mutex> lock(q_Mtx);
        timeoutStatus = q_cv.wait_for(lock, std::chrono::milliseconds(2000), [&]{return !recvQueue.empty();});
        //q_cv.wait(lock, []{ return !recvQueue.empty(); });
    }

    if(!timeoutStatus) return false;

    Node* tempNode = recvQueue.dequeue();
    if(tempNode == nullptr){}
    else
    {
        if(tempNode->msg.msgType == MsgType::PROMISE)
            if(tempNode->msg.lastAccpetedValue  != 0)
                if(tempNode->msg.lastAcceptedProposal > highestAcceptedProposal)
                    proposerValue = tempNode->msg.lastAccpetedValue;
    }

    while(true)
    {
        if(waitTimeout(1, startTime)) break;

        sockaddr_in sendAddr;
        bool status = receiveMessage(sockAttr, msg, sendAddr, MSG_DONTWAIT);
        if(status)
        {
            if(msg.msgType == MsgType::PROMISE)
            {
                if(msg.lastAccpetedValue  != 0)
                    if(msg.lastAcceptedProposal > highestAcceptedProposal)
                        proposerValue = msg.lastAccpetedValue;
                char IP_Str[INET_ADDRSTRLEN];
                socklen_t recvAddr = sizeof(sendAddr);
                inet_ntop(AF_INET, &sendAddr.sin_addr, IP_Str, recvAddr);
                TempList.erase(IP_Str);
            }
        }
        if(TempList.size() >= (Participants.size() - (Participants.size()+1)/2))
            break;
    }
    if(TempList.size() >= (Participants.size() - (Participants.size()+1)/2))
        return true;
    else
        return false;
}

/*This function contains the logic for proposer's accept phase*/
bool accept(socketAttributes sockAttr, Message msg)
{
    for(const std::string &IP_Str : Participants)
    {
        inet_pton(AF_INET, IP_Str.c_str(), &sockAttr.address.sin_addr);
        sendMessage(sockAttr, msg);
    }
    std::set<std::string> TempList(Participants);
    bool acceptStatus = true;
    auto startTime = std::chrono::steady_clock::now();
    while(true)
    {
        if(waitTimeout(1, startTime)) break;

        sockaddr_in sendAddr;
        bool status = receiveMessage(sockAttr, msg, sendAddr, MSG_DONTWAIT);
        if(status)
        {
            if(msg.msgType == MsgType::ACCEPTED)
            {
                if(msg.proposalValue > proposerValue)
                    break;
                else
                {
                    char IP_Str[INET_ADDRSTRLEN];
                    socklen_t recvAddr = sizeof(sendAddr);
                    inet_ntop(AF_INET, &sendAddr.sin_addr, IP_Str, recvAddr);
                    TempList.erase(IP_Str);
                }
            }
        }
        if(TempList.size() >= (Participants.size() - (Participants.size()+1)/2))
            break;
    }
    if(TempList.size() >= (Participants.size() - (Participants.size()+1)/2))
        return true;
    else
        return false;
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
            bool status = receiveMessage(recvArgs, msg, recvAddr, MSG_DONTWAIT);
            if(status) break;
        }
        if(msg.msgType == MsgType::PROPOSER_DONE) break;
        
        {//Mutex lock for enqueuing node in recvQueue
            recvQueue.enqueue(msg, recvAddr);
        }
        q_cv.notify_one();
    }
    return nullptr;
}

/*This is coordinator's thread*/
void* coordinatorThread(void* arg)
{

    return nullptr;
}

/*This is acceptor's thread*/
void* acceptorThread(void* arg)
{
    RecvArgs commArgs = *(RecvArgs*)arg;
    while(true)
    {
        {
            std::unique_lock<std::mutex> lock(q_Mtx);
            q_cv.wait(lock, []{ return !recvQueue.empty(); });
        }
        Node* recvNode = recvQueue.dequeue();
        if(recvNode == nullptr){}
        else
        {
            if(recvNode->msg.isReq)
            {
                if(recvNode->msg.msgType == MsgType::ACCEPTOR_DONE)
                {
                    delete(recvNode);
                    break;
                }
                if(recvNode->msg.msgType == MsgType::PREPARE)
                {
                    recvNode->msg.isReq = false;
                    if(recvNode->msg.proposalNumber > minProposal) minProposal = recvNode->msg.proposalNumber;
                    sendMessage(recvNode->msg);
                }
                if(recvNode->msg.msgType == MsgType::ACCEPT)
                {
                    recvNode->msg.isReq = false;
                    if(recvNode->msg.proposalNumber > minProposal)
                    {
                        acceptedProposal = minProposal = recvNode->msg.proposalNumber;
                        acceptedValue = recvNode->msg.proposalValue;
                    }
                    sendMessage(recvNode->msg);
                }
            }
            delete(recvNode);
        }
    }
    return nullptr;
}

/*This is proposer's thread*/
void* proposerThread(void* arg)
{
    RecvArgs recvArgs = *(RecvArgs*)arg;
    Message msg = {0};
    msg.sendIP = myIP;
    msg.isReq = true;
    msg.proposalNumber = updateMaxRound(msg);
    msg.msgType = MsgType::PROPOSAL_REQ;
    MsgType currentState = MsgType::PREPARE;
    while(true)
    {
        if(currentState == MsgType::PROPOSER_DONE) break;
        switch (currentState)
        {
            case MsgType::PROPOSAL_REQ:
            {
                msg.proposalValue = myNodeID;
                sockaddr_in sendAddr;
                socklen_t sendAddrLen = sizeof(sendAddr);

                if(withCoordinator)
                {
                    msg.recvIP = coordinatorIP;
                    while(true)
                    {
                        sendMessage(msg);
                        {
                            std::unique_lock<std::mutex> lock(q_Mtx);
                            q_cv.wait(lock, []{ return !recvQueue.empty(); });
                        }
                        Node* recvNode = recvQueue.dequeue();
                        if(recvNode == nullptr) continue;
                        else if((recvNode->msg.isReq == false) && (recvNode->msg.msgType == MsgType::PROPOSAL_OK))
                        {
                            currentState = MsgType::PREPARE;
                            break;
                        }
                    }
                }
                else
                {
                    currentState = MsgType::PREPARE;
                    break;
                }

                break;
            }
            case MsgType::PREPARE:
            {
                msg.isReq = true;
                msg.msgType = MsgType::PREPARE;
                auto startTime = std::chrono::steady_clock::now();
                while(true)
                {
                    //Send PREPARE to all participants
                    for(auto &acceptorAddr : )
                    sendMessage
                    //Receive PROMISE from all participants

                    if(waitTimeout(5000, startTime)) break;
                }
                currentState = MsgType::ACCEPT;
                break;
            }
            case MsgType::ACCEPT:
            {
                if(accept(sockAttr, msg)) currentState = MsgType::PROPOSER_DONE;
                else currentState = MsgType::PROPOSAL_REQ;
                break;
            }
        }
    }
    return nullptr;
}

/*This main function will take care of socket configuration and thread binding*/
int main(int argc, char* argv[])
{
    if(argc != 2)
    {
        std::cerr << "Invalid number of arguments!!" << std::endl;
        std::cerr << "Valid order is \"" << argv[0] << " config.txt" << std::endl;
        exit(EXIT_FAILURE);
    }

    //Extract my IP address and nodeID
    extractMyIP();
    log(BLUE+"Main: Extracted my IP address"+RESET);
    //Parsing configFile
    parseConfig(argv[1]);
    log(BLUE+"Main: Parsed config file"+RESET);

    

    //Receive port configuration------------------------------------------------------
    sockaddr_in recvAddr;
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(RECV_PORT);
    
    if(withCoordinator && (!isCoordinator))
    {
        recvAddr.sin_addr = coordinatorIP;
    } 
    else
    {
        if(inet_pton(AF_INET, BROADCAST_ADDR, &recvAddr.sin_addr) < 0)
        {
            perror("Main: Error in setting BROADCAST_IP to recvAddr.");
            exit(EXIT_FAILURE);
        }
    }

    int recvSocket = socket(AF_INET, SOCK_DGRAM, 0);
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

        commMap[(in_addr){2063968448}] = {(in_addr){2063968448}, (in_addr){2080745664}};
        commMap[(in_addr){2131077312}] = {(in_addr){2131077312}, (in_addr){2114300096}};
        commonAcceptorIP = (in_addr){2097522880};
    }

    //Thread spawning------------------------------------------------------------------
    waitForMinute();
    if(isCoordinator)
    {//Coordinator and receive threads
        pthread_t CoordinatorThread, ReceiveThread;
        pthread_create(&ReceiveThread, nullptr, receiveThread, &commArgs);
        log(BLUE+"Main: Created receive thread"+RESET);
        pthread_create(&CoordinatorThread, nullptr, coordinatorThread, &commArgs);
        log(BLUE+"Main: Created coordinator thread"+RESET);
        pthread_join(ReceiveThread, nullptr);
        log(BLUE+"Main: Joined receive thread"+RESET);
        pthread_join(CoordinatorThread, nullptr);
        log(BLUE+"Main: Joined coordinator thread"+RESET);
    }
    else if(isProposer)
    {//Proposer, Acceptor and receive threads
        pthread_t ProposerThread, AcceptorThread, ReceiveThread;
        pthread_create(&ReceiveThread, nullptr, receiveThread, &commArgs);
        log(BLUE+"Main: Created receive thread"+RESET);
        pthread_create(&AcceptorThread, nullptr, acceptorThread, &commArgs);
        log(BLUE+"Main: Created acceptor thread"+RESET);
        pthread_create(&ProposerThread, nullptr, proposerThread, &commArgs);
        log(BLUE+"Main: Created proposer thread"+RESET);
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
        pthread_create(&ReceiveThread, nullptr, receiveThread, &commArgs);
        log(BLUE+"Main: Created receive thread"+RESET);
        pthread_create(&AcceptorThread, nullptr, acceptorThread, &commArgs);
        log(BLUE+"Main: Created acceptor thread"+RESET);
        pthread_join(ReceiveThread, nullptr);
        log(BLUE+"Main: Joined receive thread"+RESET);
        pthread_join(AcceptorThread, nullptr);
        log(BLUE+"Main: Joined receive thread"+RESET);
    }
    
    //Close socket
    close(commArgs.recvSock);

    log(BLUE+"Main: Execution completed"+RESET);
    log(BLUE+"Main: Press any key to exit"+RESET);
    std::cin.get();
    return 0;
}