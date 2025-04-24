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
#include <condition_variable>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <set>
#include <iomanip>
#include <sstream>

#pragma pack(1)

#define ENABLE_LOGS true
#define MULTICAST_ADDR "239.0.0.1"
#define COORDINATOR_IP "192.168.5.123"
#define MCAST_PORT 12345
#define UNICAST_PORT 12346
#define BUFFER_SIZE 32
#define NUMBER_OF_ROUNDS 3

void debugLog(std::string message);

typedef struct
{
    int socket;
    struct sockaddr_in addr;
}argStruct;

typedef struct 
{
    argStruct mcast;
    argStruct ucast;
}threadArg;

typedef struct
{
    bool isCoordinator;
    int transactionID;
    char message[BUFFER_SIZE];
} NetMessage;

class Message
{
    public:
        bool isCoordinator;
        std::string message;
        int transactionID;
    
    Message()
    {
        isCoordinator = false;
        message = "INIT";
        //transactionID = currentTransactionID;
    }
    Message(bool iscoordinator, std::string msg)
    {
        isCoordinator = iscoordinator;
        message = msg;
        //transactionID = currentTransactionID;
    }
};

class Logger
{
    public:
        std::fstream MyFile;

        Logger()
        {
            MyFile.open("localLog.txt", std::ios::in | std::ios::out | std::ios::app);  // Open for read & append
        }

        ~Logger()
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

        bool log(const std::string message)
        {
            if (MyFile.is_open())
            {
                MyFile.clear();                       // Clear any error or EOF flags
                MyFile.seekp(0, std::ios::end);       // Force write pointer to end
                MyFile << getCurrentTimeStamp() << " ";
                MyFile << message << std::endl;
                return true;
            }
            return false;
        }

        std::string read()
        {
            if (!MyFile.is_open())
                return "";

            MyFile.clear();                           // Clear EOF or error flags
            MyFile.seekg(0, std::ios::end);           // Move read pointer to end

            std::streamoff endPos = MyFile.tellg();   // Convert to numeric-friendly type
            if (endPos == 0)                          // File is empty
                return "";

            std::streamoff pos = endPos - 1;
            char ch;
            std::string lastLine = "";

            // Walk backward to find the last newline
            while (pos >= 0)
            {
                MyFile.seekg(pos);
                MyFile.get(ch);

                if (ch == '\n' && pos != endPos - 1)   // Skip if last char is '\n'
                {
                    std::getline(MyFile, lastLine);
                    break;
                }

                --pos;

                if (pos < 0)                          // Reached beginning of file
                {
                    MyFile.seekg(0);
                    std::getline(MyFile, lastLine);
                    break;
                }
            }

            MyFile.clear();                          // Reset any EOF or error state
            MyFile.seekp(0, std::ios::end);          // Make sure log() can append next
            extractTransactionIDAndState(lastLine, lastLine);
            return lastLine;
        }

        void extractTransactionIDAndState(const std::string& logLine, /*int& transactionID,*/ std::string& state)
        {
            char tempLog[BUFFER_SIZE];
            std::string tempstring = "";
            for(int i = logLine.length()-1; i >= 0; i--)
            {
                if(logLine.at(i) == ' ')
                    break;
                    tempstring = logLine.at(i) + tempstring;
            }
            state = tempstring;
        }
};

std::mutex fileHandle, decisionRequestMtx, txnCompleteMtx, logMtx;
std::condition_variable cv;
bool isCoordinator = false;
bool receivedPrepare = false;
bool decisionRequest = false;
bool decisionRequired = false;
std::string currentState;
bool decisionRequestTermination = false;
bool onlyCoordinator = false;
bool multicastDecisionRequestFailed = false;
bool exitThread = false;
int currentTransactionID = 0;
int noOfRequests = 1;
bool decisionReached = false;
bool requestsCompleted = false;
Logger localLog;
sockaddr_in decisionRequestingIP;
std::unordered_map<std::string, int> StateDescription;
std::set<std::string> Participants;


void debugLog(std::string message)
{
    #if ENABLE_LOGS
    std::lock_guard<std::mutex> lock(logMtx);
    std::cout << message << std::endl;
    #endif
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

/*UDP send message*/
void sendMessage(argStruct socketAttr, Message msg)
{
    NetMessage netMsg{};
    socklen_t sendAddrlen = sizeof(socketAttr.addr);
    if(msg.message == "PREPARE")
    {
        currentTransactionID ++;
    }
    msg.transactionID = currentTransactionID;
    netMsg.isCoordinator = isCoordinator;
    netMsg.transactionID = msg.transactionID;
    strncpy(netMsg.message, msg.message.c_str(), BUFFER_SIZE-1);
    netMsg.message[BUFFER_SIZE - 1] = '\0';
    int sentbytes = sendto(socketAttr.socket, &netMsg, sizeof(netMsg), 0, (sockaddr*)&socketAttr.addr, sendAddrlen);
    if(sentbytes < 0)
    {
        perror("Error in sending data");
        exit(EXIT_FAILURE);
    }
    else
    {
        char ipStr[INET_ADDRSTRLEN];  // Buffer for IPv4 string
        if (inet_ntop(AF_INET, &(socketAttr.addr.sin_addr), ipStr, INET_ADDRSTRLEN) == nullptr)
        {
            perror("inet_ntop");
        }
        if(isCoordinator)
        {
            debugLog("Coordinator: Sent message [Message: " + msg.message + "] [isCoordinator: " + std::to_string(msg.isCoordinator) + "] [transactionID: " + std::to_string(msg.transactionID) + "] in " + currentState);
            debugLog("Coordinator: Sent message to [IP Address: " + std::string(ipStr) + "] [Port: " + std::to_string(ntohs(socketAttr.addr.sin_port)) + "]");
        }
        else
        {
            debugLog("Participant: Sent message [Message: " + msg.message + "] [isCoordinator: " + std::to_string(msg.isCoordinator) + "] [transactionID: " + std::to_string(msg.transactionID) + "] in " + currentState);
            debugLog("Participant: Sent message to [IP Address: " + std::string(ipStr) + "] [Port: " + std::to_string(ntohs(socketAttr.addr.sin_port)) + "]");
        }
            
    }
}

/*UDP receive message*/
sockaddr_in receiveMessage(argStruct socketAttr, Message* msg)
{
    int receivedBytes = -1;
    sockaddr_in senderAddr{};
    auto start = std::chrono::steady_clock::now();
    std::string tempState = localLog.read();
    while(true)
    {
        if((!isCoordinator) && (msg->message == "INIT") && (waitTimeout(3, start))) break;      //Timeout for participant's INIT phase
        if(!decisionRequest && (decisionRequired) && (currentState == "READY") && (waitTimeout(3, start))) break;   //Timeout for reception from coordinator in DECISION_REQUEST
        if(!decisionRequest && !multicastDecisionRequestFailed && !decisionRequired && (waitTimeout(3, start)) && (currentState == "READY"))           //Timeout for reception from participants in DECISION_REQUEST
        {
            debugLog("Participent: Timeout reached for DECISION_REQUEST.");
            msg->message = "DECISION_REQUEST_TIMEOUT";
            break;
        }
        if((!isCoordinator) && exitThread && waitTimeout(5, start)) break;      //Inducing a timeout after exitThread is set
        if(isCoordinator && !onlyCoordinator && waitTimeout(3, start)) break;
        
        socklen_t senderAddrLen = sizeof(senderAddr);
        NetMessage netMsg{};
        receivedBytes = recvfrom(socketAttr.socket, &netMsg, sizeof(netMsg), MSG_DONTWAIT, (sockaddr*)&senderAddr, &senderAddrLen);
        if(receivedBytes < 0)
        {
            if(errno == EWOULDBLOCK || errno == EAGAIN)
                continue;
            else if((currentState == "READY") && (!decisionRequest))
            {
                if(waitTimeout(3, start))
                {
                    msg->isCoordinator = true;//CHeck......................
                    msg->message = "GLOBAL_ABORT";
                    break;
                }
            }
            else if((msg->message == "EXIT") && (waitTimeout(7, start)))
                    break;
            else
                perror("Error in receiving message");
        }
        else
        {
            msg->isCoordinator = netMsg.isCoordinator;
            msg->transactionID = netMsg.transactionID;
            msg->message = std::string(netMsg.message);

            char ipStr[INET_ADDRSTRLEN];  // Buffer for IPv4 string
            if (inet_ntop(AF_INET, &(senderAddr.sin_addr), ipStr, INET_ADDRSTRLEN) == nullptr)
            {
                perror("inet_ntop");
            }
            if(isCoordinator)
            {
                debugLog("Coordinator: Received message [Message: " + msg->message + "] [isCoordinator: " + std::to_string(msg->isCoordinator) + "] [transactionID: " + std::to_string(msg->transactionID) + "] in " + currentState);
                debugLog("Coordinator: Received message from [IP Address: " + std::string(ipStr) + "] [Port: " + std::to_string(ntohs(senderAddr.sin_port)) + "]");
                if((msg->transactionID != currentTransactionID) || (msg->message == "PREPARE") || (msg->message == "READY") || (msg->message == "DECISION_REQUEST") || (msg->message == "GLOBAL_COMMIT") || (msg->message == "GLOBAL_ABORT") || (msg->message == "VOTE_REQUEST"))
                    continue;
                else
                    break;
            }
            else
            {
                if((msg->message == "ACK") || (msg->message == "VOTE_COMMIT") || msg->message == "VOTE_ABORT")
                continue;
                debugLog("Participant: Received message [Message: " + msg->message + "] [isCoordinator: " + std::to_string(msg->isCoordinator) + "] [transactionID: " + std::to_string(msg->transactionID) + "] in " + currentState);
                debugLog("Participant: Received message from [IP Address: " + std::string(ipStr) + "] [Port: " + std::to_string(ntohs(senderAddr.sin_port)) + "]");
                if((msg->message == "PREPARE") && ((currentState == "INIT_P") || (currentState == "IDLE")))
                {
                    noOfRequests++;
                    currentTransactionID = msg->transactionID;
                    break;
                }

                if((msg->message == "READY") || (msg->message == "ACK") || (msg->message == "VOTE_ABORT") || (msg->message == "VOTE_COMMIT"))
                    continue;
                    
                if((msg->message != "DECISION_REQUEST") && ((msg->message != "EXIT")) && (msg->transactionID != currentTransactionID))
                    continue;

                if(decisionRequest && (currentState == "READY"))
                {
                    if(msg->message == "DECISION_REQUEST")
                        continue;
                }
                if((!msg->isCoordinator) && (msg->message == "DECISION_REQUEST") && (!decisionRequest))
                {
                    if((tempState == "INIT_P") || (tempState == "GLOBAL_ABORT") || (tempState == "GLOBAL_COMMIT"))
                    {
                        {
                            std::lock_guard<std::mutex> lock(decisionRequestMtx);
                            decisionRequest = true;
                        }
                        
                        cv.notify_one();
                        continue;    
                    }
                }
                else if((!isCoordinator) && (currentState == "READY") && !((msg->message == "GLOBAL_COMMIT") || (msg->message == "GLOBAL_ABORT")))
                {
                    continue;
                }
                else if((!isCoordinator) && (currentState == "READY") && ((msg->message == "GLOBAL_COMMIT") || (msg->message == "GLOBAL_ABORT")))
                {
                    break;
                }
            }            
        }
    }
    return senderAddr;
}

/*This function will send DECISION_REQUEST to other nodes and process accordingly*/
std::string requestingDecision(threadArg threadArguments)
{
    Message msg;
    std::string tempState = "";
    msg.isCoordinator = isCoordinator;
    msg.message = "DECISION_REQUEST";

    /*Address specification for mcasting*/
    sockaddr_in mcastAddr{};
    mcastAddr.sin_family = AF_INET;
    mcastAddr.sin_port = htons(MCAST_PORT);
    inet_pton(AF_INET, MULTICAST_ADDR, &mcastAddr.sin_addr);


    sendMessage({threadArguments.mcast.socket, mcastAddr}, msg);
    debugLog("Participant: Sent DECISION_REQUEST message to all participants in multicast group");
    threadArguments.ucast.addr.sin_addr.s_addr = htonl(INADDR_ANY);
    std::unordered_map<std::string, int> voteCount;
    auto startTime = std::chrono::steady_clock::now();
    while(true)
    {
        receiveMessage(threadArguments.ucast, &msg);

        if(voteCount.find(msg.message) == voteCount.end())
            voteCount[msg.message] = 1;
        else
            voteCount[msg.message] += 1;
        
        if(voteCount.find("GLOBAL_ABORT") != voteCount.end())
        {
            debugLog("Participant: Received GLOBAL_ABORT in DECISION_REQUEST mode");
            tempState = "GLOBAL_ABORT";
            break;
        }

        if(waitTimeout(3, startTime))
        {
            debugLog("Participant: Didn't receive GLOBAL_ABORT/GLOBAL_COMMIT within timeout(3s)");
            break;
        }
    }
    if((tempState != "GLOBAL_ABORT") && (voteCount.find("GLOBAL_COMMIT") != voteCount.end()))
    {
        debugLog("Participant: Received GLOBAL_COMMIT in DECISION_REQUEST mode");
        tempState = "GLOBAL_COMMIT";
    }
    else if(msg.message == "DECISION_REQUEST_TIMEOUT")
    {
        debugLog("Participant: DECISION_REQUEST_TIMEOUT");
        tempState = "DECISION_REQUEST_TIMEOUT";
    }
    return tempState;
}

/*Global decision see through*/
void globalDecisionSeeThrough(threadArg threadArguments, std::string decision)
{
    Message msg(true, decision);
    std::chrono::time_point<std::chrono::steady_clock> startTime = std::chrono::steady_clock::now();//For retransmission

    std::set<std::string> askForVotes(Participants);
    bool status = false;
    
    sendMessage(threadArguments.mcast, msg);
    onlyCoordinator = false;
    while(true)
    {
        status = waitTimeout(5, startTime);//Coordinator will check for 5 seconds to receive all ACKs, then it will stop
        if(status)
        {
            std::cout << "Coordinator didn't receive ACKs from [";
            for(const std::string &IPstr:askForVotes)
            {
                std::cout << IPstr + " ";
            }
            std::cout << "]" << std::endl;
            break;
        }

        debugLog("Coordinator: Listening on PORT(" + std::to_string(ntohs(threadArguments.ucast.addr.sin_port))+"), ADDR("+inet_ntoa(threadArguments.ucast.addr.sin_addr)+")");
        sockaddr_in recvAddr = receiveMessage(threadArguments.ucast, &msg);
        if(msg.message == "ACK")
        {
            askForVotes.erase(std::string(inet_ntoa(recvAddr.sin_addr)));
            if(askForVotes.size() == 0)
            {
                debugLog("Coordinator: Received all ACKs");
                break;
            }
        }
    }
    onlyCoordinator = false;
    if(status && (askForVotes.size() != 0))
    {
        for(const std::string& IP_str:askForVotes)
        {
            sockaddr_in sendAddr;
            int sendSocket;
            NetMessage resend;

            sendAddr.sin_family = AF_INET;
            sendAddr.sin_port = htons(UNICAST_PORT);
            inet_pton(AF_INET, IP_str.c_str(), &sendAddr.sin_addr);
            
            sendSocket = socket(AF_INET, SOCK_DGRAM, 0);

            resend.isCoordinator = true;
            strcpy(resend.message, decision.c_str());
            resend.transactionID = currentTransactionID;

            /*Sending message to participants*/
            sendto(sendSocket, &resend, sizeof(resend), 0, (struct sockaddr*)&sendAddr, sizeof(sendAddr));
            close(sendSocket);
            debugLog("Coordinator: Sent "+decision+"(retransmission) to "+IP_str);

            /*Receiving messages from participants*/
            threadArguments.ucast.addr.sin_addr.s_addr = htonl(INADDR_ANY);
            threadArguments.ucast.addr.sin_port = htons(UNICAST_PORT);
            sockaddr_in recvAddr = receiveMessage(threadArguments.ucast, &msg);
            if(msg.message == "ACK")
            {
                askForVotes.erase(std::string(inet_ntoa(recvAddr.sin_addr)));
                if(askForVotes.size() == 0)
                {
                    debugLog("Coordinator: Received all ACKs");
                    break;
                }
            }
        }
    }
}

/*DECISION_REQUEST thread*/
void* decisionRequestThread(void* arg)
{
    threadArg threadArguments = *((threadArg*)arg);
    while(true)
    {
        std::unique_lock<std::mutex> lock(decisionRequestMtx);
        cv.wait(lock, []{return (decisionRequest|decisionRequestTermination);});
        if(decisionRequest)
        {
            debugLog("Participant: Decision request thread got invoked");
            threadArguments.ucast.addr = decisionRequestingIP;
            threadArguments.ucast.addr.sin_port = htons(UNICAST_PORT);
            Message msg;
            
            std::string fromLog = localLog.read();
            if(fromLog == "GLOBAL_COMMIT")
            {
                msg.message = "GLOBAL_COMMIT";
                sendMessage(threadArguments.ucast, msg);
            }
            else if((fromLog == "INIT")||((fromLog == "GLOBAL_ABORT")))
            {
                msg.message = "GLOBAL_ABORT";
                sendMessage(threadArguments.ucast, msg);
            }
            decisionRequest = false;
        }
        if(decisionRequestTermination)
            break;
    }
    return nullptr;
}

/*PARTICIPANT thread*/
void* participantThread(void* ptr)
{
    threadArg threadArguments = *(threadArg*)ptr;
    currentState = "INIT_P";
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    bool crashed = true;
    Message msg;
    while(true)
    {
        if(exitThread == true)
        {
            #if 0
            debugLog("Processing exit");
            auto now = std::chrono::system_clock::now();
            std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
            std::cout << "Current Time: " << std::put_time(std::localtime(&currentTime), "%Y-%m-%d %H:%M:%S") << std::endl;
            receiveMessage(threadArguments.mcast, &msg);
            currentTime = std::chrono::system_clock::to_time_t(now);
            std::cout << "Current Time: " << std::put_time(std::localtime(&currentTime), "%Y-%m-%d %H:%M:%S") << std::endl;
            break;
            #else
            // In your exitThread==true block:
            int fd = threadArguments.mcast.socket;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            struct timeval tv { 5, 0 };    // 5 seconds

            int ret = select(fd+1, &rfds, nullptr, nullptr, &tv);
            if (ret > 0 && FD_ISSET(fd, &rfds)) {
                // exactly one packet available—grab it
                NetMessage net;
                sockaddr_in peer;
                socklen_t plen = sizeof(peer);
                int n = recvfrom(fd, &net, sizeof(net), 0,
                                (sockaddr*)&peer, &plen);
                if (n>0 && std::string(net.message)=="DECISION_REQUEST") {
                    // service it immediately
                    std::string last = localLog.read();
                    std::string reply = (last=="GLOBAL_COMMIT"?"GLOBAL_COMMIT":"GLOBAL_ABORT");
                    Message m(false, reply);
                    threadArguments.ucast.addr = peer;
                    threadArguments.ucast.addr.sin_port = htons(UNICAST_PORT);
                    sendMessage(threadArguments.ucast, m);
                }
            }
            // else ret==0: timed out, no request
            localLog.log("EXIT");
            break;
            #endif
        }
        
        switch (StateDescription[currentState])
        {
            case 1://INIT
            {//Implement DECISION_REQUEST servicing
                localLog.log("INIT");
                debugLog("Participant: Entered INIT");
                std::string previousState = localLog.read();
                if(previousState == "READY")
                {
                    localLog.log("RECOVERING");
                    currentState = requestingDecision(threadArguments);
                    if(currentState != "")
                        break;
                }
                else if((previousState == "GLOBAL_COMMIT") || (previousState == "GLOBAL_ABORT"))
                {
                    Message msg;
                    msg.isCoordinator = false;
                    msg.message = "ACK";
                    threadArguments.ucast.addr.sin_addr.s_addr = inet_addr(COORDINATOR_IP);
                    threadArguments.ucast.addr.sin_port = htons(UNICAST_PORT);
                    sendMessage(threadArguments.ucast, msg);
                }
                
                if(!receivedPrepare)
                {
                    msg.message = "INIT";

                    sockaddr_in recvAddress = receiveMessage(threadArguments.mcast, &msg);
                    
                    threadArguments.ucast.addr.sin_addr = recvAddress.sin_addr;
                    msg.isCoordinator = false;
                    if(msg.message == "INIT")
                    {
                        currentState = "GLOBAL_ABORT";
                        msg.message = "GLOBAL_ABORT";
                        debugLog("Participant: Aborting INIT state due to timeout");
                        //threadArguments.ucast.addr.sin_addr.s_addr = htonl(INADDR_ANY);//Participant will not transmit ABORT
                    }
                    else if(msg.message == "PREPARE")
                    {
                        localLog.log("PREPARE");
                        currentState = "READY";
                        debugLog("Participant: Received PREPARE, moving to READY state");
                    }
                }
                else
                {
                    currentState = "READY";
                    receivedPrepare = false;
                }
                break;
            }
            case 3://READY
            {
                localLog.log("READY");
                //READY activities;
                msg.isCoordinator = false;
                
                if(((std::rand())%100)%7 == 0) msg.message = "VOTE_ABORT";//Simulating random ABORTs
                else msg.message = "VOTE_COMMIT";
                threadArguments.ucast.addr.sin_addr.s_addr = inet_addr(COORDINATOR_IP);
                decisionRequired = true;
                debugLog("Participant: Replied to VOTE_REQUEST[" + msg.message + "] and is awaiting GLOBAL command from coordinator");
                sendMessage(threadArguments.ucast, msg);//Send local vote
                localLog.log("VOTED");
                msg.message = "READY";
                

                sockaddr_in recvAddress = receiveMessage(threadArguments.mcast, &msg);
                //msg.message = "FAULT INJECTION FOR PACKET LOSS";
                debugLog("Participant: Received ->"+ msg.message + " as GLOBAL decision from coordinator");
                if(msg.isCoordinator && ((msg.message == "GLOBAL_COMMIT") || (msg.message == "GLOBAL_ABORT")))
                {
                    decisionRequired = false;
                    debugLog("Participant: Received valid GLOBAL decision from coordinator and will send ACK");
                    currentState = msg.message;
                    msg.isCoordinator = false;
                    msg.message = "ACK";
                    threadArguments.ucast.addr = recvAddress;
                    threadArguments.ucast.addr.sin_port = htons(UNICAST_PORT);
                    sendMessage(threadArguments.ucast, msg);
                }
                else
                {
                    debugLog("Participant: About to enter into DECISION_REQUEST logic");
                    decisionRequired = false;
                    localLog.log("DECISION_REQUEST");
                    std::string currentMessage = requestingDecision(threadArguments);

                    if(currentMessage == "DECISION_REQUEST_TIMEOUT")
                    {
                        multicastDecisionRequestFailed = true;
                        debugLog("Participant: Failed to receive GLOBAL decision from Coordinator and other participants, so entering into a while loop for Coordinator");
                        while(true)
                        {
                            msg.message = "READY";
                            threadArguments.ucast.addr.sin_addr.s_addr = htonl(INADDR_ANY);
                            threadArguments.ucast.addr.sin_port = htons(UNICAST_PORT);
                            sockaddr_in recvAddress = receiveMessage(threadArguments.ucast, &msg);
                            if(msg.isCoordinator && ((msg.message == "GLOBAL_COMMIT") || (msg.message == "GLOBAL_ABORT")))
                            {
                                debugLog("Participant: Received GLOBAL decision from coordinator and will send ACK");
                                currentState = msg.message;
                                msg.isCoordinator = false;
                                msg.message = "ACK";
                                threadArguments.ucast.addr = recvAddress;
                                threadArguments.ucast.addr.sin_port = htons(UNICAST_PORT);
                                sendMessage(threadArguments.ucast, msg);
                                break;
                            }
                        }
                        multicastDecisionRequestFailed = false;
                    }
                    else
                    {
                        multicastDecisionRequestFailed = false;
                        currentState = currentMessage;
                    }
                }

                break;
            }
            case 4://GLOBAL_COMMIT
            {
                localLog.log("GLOBAL_COMMIT");
                debugLog("Participant: Received GLOBAL_COMMIT");
                msg;
                msg.isCoordinator = false;
                msg.message = "ACK";
                threadArguments.ucast.addr.sin_addr.s_addr = inet_addr(COORDINATOR_IP);
                sendMessage(threadArguments.ucast, msg);

                if(noOfRequests > NUMBER_OF_ROUNDS)
                {
                    msg.message = "EXIT";
                    exitThread = true;
                    debugLog("Participant: Will exit from the loop, completed all transactions");
                }
                currentState = "IDLE";
                break;
            }
            case 5://GLOBAL_ABORT
            {
                localLog.log("GLOBAL_ABORT");
                debugLog("Participant: Received GLOBAL_ABORT");
                msg;
                msg.isCoordinator = false;
                msg.message = "ACK";
                threadArguments.ucast.addr.sin_addr.s_addr = inet_addr(COORDINATOR_IP);
                sendMessage(threadArguments.ucast, msg);

                if(noOfRequests > NUMBER_OF_ROUNDS)
                {
                    msg.message = "EXIT";
                    exitThread = true;
                    debugLog("Participant: Will exit from the loop, completed all transactions");
                }
                currentState = "IDLE";
                break;
            }
            case 6://IDLE
            {
                msg.message = "IDLE";
                localLog.log("IDLE");
                while(true)
                {
                    debugLog("Participant: Entered into IDLE mode");
                    sockaddr_in recvAddr = receiveMessage(threadArguments.mcast, &msg);
                    
                    if(msg.message == "PREPARE")
                    {
                        currentState = "INIT_P";
                        receivedPrepare = true;
                        debugLog("Participant: Received PREPARE in IDLE state");
                        break;
                    }
                    else if((msg.message == "GLOBAL_COMMIT") || (msg.message == "GLOBAL_ABORT"))
                    {
                        msg.isCoordinator = false;
                        msg.message = "ACK";
                        debugLog("Participant: Received GLOBAL decision in IDLE state, most likely it is a case of lost ACK");
                        debugLog("Participant: Retransimitting ACK");
                        sendMessage(threadArguments.mcast, msg);
                    }
                }
                break;
            }
        }
    }
    debugLog("Participant: Exiting participant thread");
    return nullptr;
}

/*COORDINATOR thread*/
void* coordinatorThread(void* arg)
{
    threadArg threadArguments = *(threadArg*)arg;
    currentState = "INIT_C";
    bool crashed = true;
    //implement INIT(user input), WAIT(send VOTE_REQUEST), DECIDE(send GLOBAL_COMMIT/GLOBAL_ABORT, back to INIT)
    Message msg;
    while (true)
    {
        switch(StateDescription[currentState])
        {
            case 0/*INIT_C*/:
            {
                std::string previousState = localLog.read();
                msg.isCoordinator = true;
                
                if(previousState == "INIT")
                {
                    currentState = "GLOBAL_ABORT";
                    break;
                }
                else if(previousState == "WAIT")
                {
                    currentState = "WAIT";
                    break;
                }
                else if(previousState == "GLOBAL_COMMIT")
                {
                    currentState = "GLOBAL_COMMIT";
                    break;
                }
                else if(previousState == "GLOBAL_ABORT")
                {
                    currentState = "GLOBAL_ABORT";
                    break;
                }
                else
                {
                    localLog.log("INIT");
                }

                msg.isCoordinator = true;
                msg.message = "PREPARE";
                sendMessage(threadArguments.mcast, msg);
                debugLog("Coordinator: Sent PREPARE");
                
                currentState = "WAIT";
                break;
            }
            case 2/*WAIT*/:
            {
                localLog.log("WAIT");
                msg.isCoordinator = true;
                msg.message = "VOTE_REQUEST";
                sendMessage(threadArguments.mcast, msg);
                debugLog("Coordinator: Sent VOTE_REQUEST");
                
                std::chrono::time_point<std::chrono::steady_clock> startTime = std::chrono::steady_clock::now();//For global abort

                std::set<std::string> askForVotes(Participants);
                std::unordered_map<std::string, int> votes;
                bool status = false;
                while(true)
                {
                    status = waitTimeout(3, startTime);
                    if(status)
                    {
                        msg.isCoordinator = true;
                        msg.message = "GLOBAL_ABORT";
                        currentState = "GLOBAL_ABORT";
                        debugLog("Coordinator: Issued a GLOBAL_ABORT(not enough votes)");
                        break;
                    }

                    debugLog("Coordinator: Listening for votes on PORT(" + std::to_string(ntohs(threadArguments.ucast.addr.sin_port))+"), ADDR("+inet_ntoa(threadArguments.ucast.addr.sin_addr)+")");
                    sockaddr_in recvAddr = receiveMessage(threadArguments.ucast, &msg);

                    if((msg.message == "VOTE_COMMIT")||(msg.message == "VOTE_ABORT"))
                    {
                        askForVotes.erase(std::string(inet_ntoa(recvAddr.sin_addr)));
                        votes[msg.message] ++;
                        if(askForVotes.size() == 0)
                        {
                            currentState = "GLOBAL_ABORT";
                            if(votes["VOTE_COMMIT"] == Participants.size())
                            {
                                currentState = "GLOBAL_COMMIT";
                                debugLog("Coordinator: Decided on global commit");
                            }
                            break;
                        }
                        else
                        {
                            debugLog("Coordinator: Is still waiting for all votes");
                        }
                    }

                    status = waitTimeout(1, startTime);
                    if(status)
                    {
                        debugLog("Coordinator: Didn't receive enough votes, retransmitting VOTE_REQUEST");
                        //Ask for votes from the participants who didn't vote for VOTE_REQUEST
                        for(const std::string& IP_str:askForVotes)
                        {
                            sockaddr_in sendAddr;
                            int sendSocket;
                            
                            NetMessage resendNetMessage;
                            sendAddr.sin_family = AF_INET;
                            sendAddr.sin_port = htons(UNICAST_PORT);
                            inet_pton(AF_INET, IP_str.c_str(), &sendAddr.sin_addr);
                            
                            sendSocket = socket(AF_INET, SOCK_DGRAM, 0);

                            resendNetMessage.isCoordinator = true;
                            strcpy(resendNetMessage.message, "VOTE_REQUEST");

                            sendto(sendSocket, &resendNetMessage, sizeof(resendNetMessage), 0, (struct sockaddr*)&sendAddr, sizeof(sendAddr));
                            close(sendSocket);
                            debugLog("Coordinator: Sent VOTE_REQUEST(retransmission) to "+IP_str);
                        }
                    }
                }
                break;
            }
            case 4/*GLOBAL_COMMIT*/:
            {
                localLog.log("GLOBAL_COMMIT");
                debugLog("Coordinator: Decided on GLOBAL_COMMIT");
                globalDecisionSeeThrough(threadArguments, "GLOBAL_COMMIT");
                sleep(1);
                char flushMsg[128];
                currentState = "INIT_C";
                noOfRequests++;
                while (true) {
                    socklen_t addrLen = sizeof(threadArguments.ucast.addr);
                    ssize_t bytesReceived = recvfrom(threadArguments.ucast.socket, &flushMsg, sizeof(flushMsg), 
                                                    MSG_DONTWAIT, (struct sockaddr*)&threadArguments.ucast.addr, &addrLen);
                    if (bytesReceived <= 0) {
                        break;
                    }
                    debugLog("Coordinator: Flushed old messages after receiving required ACKs for GLOBAL_COMMIT");
                }
                localLog.log("DONE");
                break;
            }
            case 5/*GLOBAL_ABORT*/:
            {
                localLog.log("GLOBAL_ABORT");
                debugLog("Coordinator: Decided on GLOBAL_ABORT");
                globalDecisionSeeThrough(threadArguments, "GLOBAL_ABORT");
                sleep(1);
                char flushMsg[128];
                currentState = "INIT_C";
                noOfRequests++;
                while (true) {
                    socklen_t addrLen = sizeof(threadArguments.ucast.addr);
                    ssize_t bytesReceived = recvfrom(threadArguments.ucast.socket, &flushMsg, sizeof(flushMsg), 
                                                    MSG_DONTWAIT, (struct sockaddr*)&threadArguments.ucast.addr, &addrLen);
                    if (bytesReceived <= 0) {
                        break;
                    }
                    debugLog("Coordinator: Flushed old messages after receiving required ACKs for GLOBAL_COMMIT");
                }
                localLog.log("DONE");
                break;
            }
        }
        if(noOfRequests > NUMBER_OF_ROUNDS)
        {
            localLog.log("EXIT");
            break;
        }
    }
    return nullptr;
}

/*For thread spawning and management*/
int main(int argc, char* argv[])
{
    if((argc < 2)||(argc > 2) && !((std::string(argv[1]) == "coordinator")||(std::string(argv[1]) == "participant")))
    {
        std::cerr << "Valid arguments are: " << argv[0] << " [coordinator|participant]" << std::endl;
        return -1;
    }

    isCoordinator = false;
    if(std::string(argv[1]) == "coordinator")
        isCoordinator = true;

    //Multicast socket configuration
    //--------------------------------------------------------------------------------------------------------
    struct sockaddr_in mcastAddr{};
    int mcastSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if(mcastSocket < 0)
    {
        perror("Multicast socket creation failed");
        return -1;
    }

    mcastAddr.sin_family = AF_INET;
    mcastAddr.sin_port = htons(MCAST_PORT);

    argStruct mcastAttributes = {mcastSocket, mcastAddr};

    if(isCoordinator)
    {
        inet_pton(AF_INET, MULTICAST_ADDR, &mcastAttributes.addr.sin_addr);
        if(bind(mcastAttributes.socket, (sockaddr*)&mcastAttributes.addr, sizeof(mcastAttributes.addr)) < 0) {
            perror("Receiver port binding failed");
            return -1;
        }
    }
    else
    {
        ip_mreq mreq{};
        inet_pton(AF_INET, MULTICAST_ADDR, &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if(setsockopt(mcastAttributes.socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
        {
            std::cerr << "Receiver failed to join multicast group." << std::endl;
            return -1;
        }
        unsigned char loopback = 0;
        if (setsockopt(mcastSocket, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) < 0) {
            perror("Disabling multicast loopback failed");
        }
        if(bind(mcastAttributes.socket, (sockaddr*)&mcastAttributes.addr, sizeof(mcastAttributes.addr)) < 0) {
            perror("Receiver port binding failed");
            return -1;
        }
    }
    //--------------------------------------------------------------------------------------------------------

    //Unicast socket configuration
    //--------------------------------------------------------------------------------------------------------
    struct sockaddr_in ucastAddr{};
    int ucastSocket = socket(AF_INET, SOCK_DGRAM, 0);

    if(ucastSocket < 0)
    {
        perror("Receiver socket creation failed");
        return -1;
    }

    ucastAddr.sin_family = AF_INET;
    ucastAddr.sin_port = htons(UNICAST_PORT);
    ucastAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    argStruct ucastAttributes = {ucastSocket, ucastAddr};
    int dont_route = 1;
    if (setsockopt(ucastAttributes.socket, SOL_SOCKET, SO_DONTROUTE, &dont_route, sizeof(dont_route)) < 0) {
        perror("setsockopt(SO_DONTROUTE) failed");
    }
    if(bind(ucastAttributes.socket, (sockaddr*)&ucastAttributes.addr, sizeof(ucastAttributes.addr)) < 0) {
        perror("Unicast port binding failed");
        return -1;
    }
    
    //--------------------------------------------------------------------------------------------------------

    StateDescription["INIT_C"]  = 0;
    StateDescription["INIT_P"]  = 1;
    StateDescription["WAIT"]    = 2;
    StateDescription["READY"]   = 3;
    StateDescription["GLOBAL_COMMIT"]  = 4;
    StateDescription["GLOBAL_ABORT"]   = 5;
    StateDescription["IDLE"]   = 6;

    Participants.insert("192.168.5.124");
    Participants.insert("192.168.5.125");
    //Participants.insert("192.168.5.126");

    threadArg threadArgument = {mcastAttributes, ucastAttributes};

    //Thread spawning
    waitForMinute();
    //--------------------------------------------------------------------------------------------------------
    if(isCoordinator)
    {
        pthread_t CoordinatorThread;
        pthread_create(&CoordinatorThread, nullptr, coordinatorThread, (void*)(&threadArgument));
        debugLog("Main: Created coordinator thread");
        pthread_join(CoordinatorThread, nullptr);
        debugLog("Main: Coordinator thread rejoined with main");
    }
    else
    {
        pthread_t DecisionRequestThread, ParticipantThread;
        pthread_create(&DecisionRequestThread, nullptr, decisionRequestThread, (void*)(&threadArgument));
        pthread_create(&ParticipantThread, nullptr, participantThread, (void*)(&threadArgument));
        debugLog("Main: Created participant and decisionRequest thread");
        
        pthread_join(ParticipantThread, nullptr);
        debugLog("Main: Participant thread joined");
        {
            std::lock_guard<std::mutex> lock(decisionRequestMtx);
            decisionRequestTermination = true;
            cv.notify_one();
        }
        pthread_join(DecisionRequestThread, nullptr);
        debugLog("Main: Participant and decisionRequest threads rejoined with main");
    }

    close(mcastAttributes.socket);
    close(ucastAttributes.socket);
    
    std::cout << "Press any key to end the program" << std::endl;
    std::cin.get();
    return 0;
}