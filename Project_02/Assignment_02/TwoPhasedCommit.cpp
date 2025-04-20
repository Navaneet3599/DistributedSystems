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

#pragma pack(1)

#define ENABLE_LOGS true
#define MULTICAST_ADDR "239.0.0.1"
#define COORDINATOR_IP "192.168.5.123"
#define MCAST_PORT 12345
#define UNICAST_PORT 12346
#define BUFFER_SIZE 32



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

            return lastLine;
        }
};

std::mutex fileHandle, decisionRequestMtx, txnCompleteMtx, logMtx;
std::condition_variable cv;
bool isCoordinator = false;
bool receivedPrepare = false;
bool decisionRequest = false;
int currentTransactionID = 0;
int noOfRequests = 0;
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
    netMsg.isCoordinator = msg.isCoordinator;
    netMsg.transactionID = msg.transactionID;
    strncpy(netMsg.message, msg.message.c_str(), BUFFER_SIZE);
    int sentbytes = sendto(socketAttr.socket, &netMsg, sizeof(netMsg), 0, (sockaddr*)&socketAttr.addr, sizeof(struct sockaddr));

    if(sentbytes < 0)
    {
        perror("Error in sending data");
        exit(EXIT_FAILURE);
    }
}

/*UDP receive message*/
sockaddr_in receiveMessage(argStruct socketAttr, Message* msg)
{
    int receivedBytes = -1;
    sockaddr_in senderAddr{};
    auto start = std::chrono::steady_clock::now();
    std::string currentState = localLog.read();
    while(true)
    {
        currentState = localLog.read();
        {
            //Thread exit condition
            if((noOfRequests == 3) && (currentState == "IDLE"))
            {
                msg->message = "EXIT";
                break;
            }
        }

        if((msg->message == "INIT") && (waitTimeout(1, start))) break;
        
        socklen_t senderAddrLen = sizeof(senderAddr);
        NetMessage netMsg{};
        receivedBytes = recvfrom(socketAttr.socket, &netMsg, sizeof(netMsg), MSG_DONTWAIT, (sockaddr*)&senderAddr, &senderAddrLen);
        if(receivedBytes < 0)
        {
            if(errno == EWOULDBLOCK || errno == EAGAIN)
                continue;
            else if(msg->message == "READY")
            {
                if(waitTimeout(3, start))
                {
                    msg->isCoordinator = true;
                    msg->message = "GLOBAL_ABORT";
                    break;
                }
            }
            else
                perror("Error in receiving message");
        }
        else
        {
            msg->isCoordinator = netMsg.isCoordinator;
            msg->transactionID = netMsg.transactionID;
            msg->message = std::string(netMsg.message);
            if(msg->message == "DECISION_REQUEST")
            {
                if((currentState == "INIT_P") || (currentState == "GLOBAL_ABORT") || (currentState == "GLOBAL_COMMIT"))
                {
                    {
                        std::lock_guard<std::mutex> lock(decisionRequestMtx);
                        decisionRequest = true;
                    }
                    decisionRequestingIP = senderAddr;
                    cv.notify_one();
                    continue;    
                }
            }
            else
            {
                if((msg->message == "PREPARE") && (!isCoordinator))
                {
                    noOfRequests++;
                }
                break;
            }
        }
    }

    return senderAddr;
}

/*This function will send DECISION_REQUEST to other nodes and process accordingly*/
std::string requestingDecision(threadArg threadArguments)
{
    Message msg;
    std::string currentState = "";
    msg.message = "DECISION_REQUEST";
    sendMessage(threadArguments.mcast, msg);
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
            msg.isCoordinator = isCoordinator;//Update isCoordinator in main()
            msg.message = "ACK";
            currentState = "GLOBAL_ABORT";
            sendMessage(threadArguments.mcast, msg);
            break;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime);
        if (elapsed.count() >= 3)
        {
            std::cout << "Timeout reached" << std::endl;
            break;
        }
    }
    if((currentState != "GLOBAL_ABORT") && (voteCount.find("GLOBAL_COMMIT") != voteCount.end()))
    {
        msg.isCoordinator = isCoordinator;//Update isCoordinator in main()
        msg.message = "ACK";
        currentState = "GLOBAL_COMMIT";
        sendMessage(threadArguments.mcast, msg);
    }
    return currentState;
}

/*Global decision see through*/
void globalDecisionSeeThrough(threadArg threadArguments, std::string decision)
{
    Message msg(true, decision);
    std::chrono::time_point<std::chrono::steady_clock> startTime = std::chrono::steady_clock::now();//For retransmission

    std::set<std::string> askForVotes(Participants);
    bool status = false;
    
    sendMessage(threadArguments.mcast, msg);
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

        sockaddr_in recvAddr = receiveMessage(threadArguments.mcast, &msg);
        if(msg.message == "ACK")
        {
            askForVotes.erase(std::string(inet_ntoa(recvAddr.sin_addr)));
            if(askForVotes.size() == 0)
            {
                break;
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
        cv.wait(lock, []{return decisionRequest;});
        debugLog("Decision request thread got invoked");
        threadArguments.ucast.addr = decisionRequestingIP;
        Message msg;
        
        std::string fromLog = localLog.read();
        if(fromLog == "GLOBAL_COMMIT")
        {
            msg.message = fromLog;
            sendMessage(threadArguments.ucast, msg);
        }
        else if((fromLog == "INIT")||((fromLog == "GLOBAL_ABORT")))
        {
            msg.message = "GLOBAL_ABORT";
            sendMessage(threadArguments.ucast, msg);
        }
        decisionRequest = false;
    }
    return nullptr;
}

/*COORDINATOR thread*/
void* coordinatorThread(void* arg)
{
    threadArg threadArguments = *(threadArg*)arg;
    std::string currentState = "INIT_C";
    bool crashed = true;
    //implement INIT(user input), WAIT(send VOTE_REQUEST), DECIDE(send GLOBAL_COMMIT/GLOBAL_ABORT, back to INIT)
    while (true)
    {
        Message msg;
        switch(StateDescription[currentState])
        {
            case 0/*INIT_C*/:
            {
                localLog.log("INIT");
                msg.isCoordinator = true;
                msg.message = "PREPARE";
                sendMessage(threadArguments.mcast, msg);
                debugLog("Coordinator send PREPARE");
                noOfRequests++;
                currentState = "WAIT";
                break;
            }
            case 2/*WAIT*/:
            {
                localLog.log("WAIT");
                msg.isCoordinator = true;
                msg.message = "VOTE_REQUEST";
                sendMessage(threadArguments.mcast, msg);
                debugLog("Coordinator send VOTE_REQUEST");
                
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
                        debugLog("Coordinator issued a GLOBAL_ABORT(not enough votes)");
                        break;
                    }

                    debugLog("Coordinator is about to receive VOTES");
                    sockaddr_in recvAddr = receiveMessage(threadArguments.ucast, &msg);
                    debugLog("Coordinator received: "+msg.message);
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
                            }
                            break;
                        }
                        else
                        {
                            debugLog("Coordinator is still waiting for all votes");
                        }
                    }

                    status = waitTimeout(1, startTime);
                    if(status)
                    {
                        debugLog("Coordinator didn't receive enough votes, retransmitting VOTE_REQUEST");
                        //Ask for votes from the participants who didn't vote for VOTE_REQUEST
                        for(const std::string& IP_str:askForVotes)
                        {
                            sockaddr_in sendAddr;
                            int sendSocket;
                            Message resend;

                            sendAddr.sin_family = AF_INET;
                            sendAddr.sin_port = UNICAST_PORT;
                            inet_pton(AF_INET, IP_str.c_str(), &sendAddr.sin_addr);
                            
                            sendSocket = socket(AF_INET, SOCK_DGRAM, 0);

                            resend.isCoordinator = true;
                            resend.message = "VOTE_REQUEST";

                            sendto(sendSocket, &resend, sizeof(resend), 0, (struct sockaddr*)&sendAddr, sizeof(sendAddr));
                            debugLog("Sent VOTE_REQUEST(retransmission) to "+IP_str);
                        }
                    }
                }
                break;
            }
            case 4/*GLOBAL_COMMIT*/:
            {
                localLog.log("GLOBAL_COMMIT");
                globalDecisionSeeThrough(threadArguments, "GLOBAL_COMMIT");
                debugLog("Decided on GLOBAL_COMMIT");
                sleep(1);
                currentState = "INIT";
                break;
            }
            case 5/*GLOBAL_ABORT*/:
            {
                localLog.log("GLOBAL_ABORT");
                globalDecisionSeeThrough(threadArguments, "GLOBAL_ABORT");
                debugLog("Decided on GLOBAL_ABORT");
                sleep(1);
                currentState = "INIT";
                break;
            }
        }
        if(noOfRequests == 3)
            break;
    }
    return nullptr;
}

/*PARTICIPANT thread*/
void* participantThread(void* ptr)
{
    threadArg threadArguments = *(threadArg*)ptr;
    std::string currentState = "INIT_P";
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    bool crashed = true;
    while(true)
    {
        if(noOfRequests == 3)
            break;
        Message msg;
        switch (StateDescription[currentState])
        {
            case 1://INIT
            {//Implement DECISION_REQUEST servicing
                localLog.log("INIT");
                debugLog("Participant entered INIT");
                std::string previousState = localLog.read();
                if(previousState == "READY")
                {
                    localLog.log("RECOVERING");
                    currentState = requestingDecision(threadArguments);
                    if(currentState != "")
                        break;
                }
                
                if(!receivedPrepare)
                {
                    localLog.log("INIT");
                    msg.message = "INIT";

                    sockaddr_in recvAddress = receiveMessage(threadArguments.mcast, &msg);
                    
                    threadArguments.ucast.addr = recvAddress;
                    msg.isCoordinator = false;
                    if(msg.message == "INIT")
                    {
                        currentState = "GLOBAL_ABORT";
                        msg.message = "GLOBAL_ABORT";
                        debugLog("Participant aborting INIT state due to timeout");
                        //threadArguments.ucast.addr.sin_addr.s_addr = htonl(INADDR_ANY);//Participant will not transmit ABORT
                    }
                    else if(msg.message == "PREPARE")
                    {
                        currentState = "READY";
                        debugLog("Participant received PREPARE, moving to READY state");
                    }
                }
                else
                {
                    localLog.log("INIT");
                    currentState = "READY";
                    receivedPrepare = false;
                }
                break;
            }
            case 3://READY
            {
                localLog.log("READY");
                //READY activities
                Message msg;
                msg.isCoordinator = false;
                
                if(((std::rand())%100)%7 == 0) msg.message = "VOTE_COMMIT";//Make this as VOTE_ABORT in future
                else msg.message = "VOTE_COMMIT";
                debugLog("Participant is about to reply to VOTE_REQUEST to coordinator");
                sendMessage(threadArguments.ucast, msg);//Send local vote
                debugLog("Participant replied to VOTE_REQUEST and is awaiting GLOBAL command from coordinator");

                while(true)
                {
                    sockaddr_in recvAddress = receiveMessage(threadArguments.mcast, &msg);
                    if(msg.isCoordinator && ((msg.message == "GLOBAL_COMMIT") || (msg.message == "GLOBAL_ABORT")))
                    {
                        debugLog("Participant received GLOBAL decision from coordinator and will send ACK");
                        currentState = msg.message;
                        msg.isCoordinator = false;
                        msg.message = "ACK";
                        sendMessage(threadArguments.ucast, msg);
                        break;
                    }
                }
                break;
            }
            case 4://GLOBAL_COMMIT
            {
                localLog.log("GLOBAL_COMMIT");
                debugLog("Participant received GLOBAL_COMMIT");
                Message msg;
                msg.isCoordinator = false;
                msg.message = "ACK";
                sendMessage(threadArguments.ucast, msg);

                currentState = "IDLE";
                break;
            }
            case 5://GLOBAL_ABORT
            {
                localLog.log("GLOBAL_ABORT");
                debugLog("Participant received GLOBAL_ABORT");
                Message msg;
                msg.isCoordinator = false;
                msg.message = "ACK";
                sendMessage(threadArguments.ucast, msg);

                currentState = "IDLE";
                break;
            }
            case 6://IDLE
            {
                Message msg;
                while(true)
                {
                    sockaddr_in recvAddr = receiveMessage(threadArguments.mcast, &msg);
                    if(msg.message == "PREPARE")
                    {
                        currentState = "INIT_P";
                        receivedPrepare = true;
                        debugLog("Participant received PREPARE in IDLE state");
                        break;
                    }
                    else if((msg.message == "GLOBAL_COMMIT") || (msg.message == "GLOBAL_ABORT"))
                    {
                        msg.isCoordinator = false;
                        msg.message = "ACK";
                        debugLog("Participant received GLOBAL decision in IDLE state, most likely it is a case of lost ACK");
                        sendMessage(threadArguments.mcast, msg);
                    }
                    else if(msg.message == "EXIT")
                        break;
                }
                break;
            }
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
        debugLog("Created coordinator thread");
        pthread_join(CoordinatorThread, nullptr);
        debugLog("Coordinator thread rejoined with main");
    }
    else
    {
        pthread_t DecisionRequestThread, ParticipantThread;
        pthread_create(&DecisionRequestThread, nullptr, decisionRequestThread, (void*)(&threadArgument));
        pthread_create(&ParticipantThread, nullptr, participantThread, (void*)(&threadArgument));
        debugLog("Created coordinator and decisionRequest thread");
        pthread_join(DecisionRequestThread, nullptr);
        pthread_join(ParticipantThread, nullptr);
        debugLog("Participant and decisionRequest threads rejoined with main");
    }

    close(mcastAttributes.socket);
    close(ucastAttributes.socket);
    
    std::cout << "Press any key to end the program" << std::endl;
    std::cin.get();
    return 0;
}