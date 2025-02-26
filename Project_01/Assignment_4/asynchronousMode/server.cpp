#include <iostream>
#include <sstream>
#include <vector>
#include <queue>
#include <iomanip>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <arpa/inet.h>
#include <chrono>
#include <unistd.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_NO_TRANSFERS 50
#define MESSAGE_QUEUE_SIZE 60

typedef struct sockaddr ST_sockaddr;
typedef struct sockaddr_in ST_sockaddr_in;
typedef struct
{
    int client_socket;
    char clientIP[INET_ADDRSTRLEN];
}ST_ClientConnection;

typedef struct
{
    std::string req;
    std::string res;
}ST_ClientReq;


class MessageQueue
{
    private:
    std::queue<std::string> messageQueue;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    bool terminationRequest = false;

    public:
    std::string front()
    {
        pthread_mutex_lock(&mutex);
        std::string retVal = messageQueue.front();
        pthread_mutex_unlock(&mutex);
        return retVal;
    }

    void push(std::string newRequest)
    {
        pthread_mutex_lock(&mutex);
        messageQueue.push(newRequest);
        pthread_mutex_unlock(&mutex);
    }

    std::string pop()
    {
        std::string prevRequest;
        pthread_mutex_lock(&mutex);
        prevRequest = messageQueue.front();
        pthread_mutex_unlock(&mutex);
        messageQueue.pop();
        return prevRequest;
    }

    int size()
    {
        int size = 0;
        pthread_mutex_lock(&mutex);
        size = messageQueue.size();
        pthread_mutex_unlock(&mutex);
        return size;
    }

    bool setTerminationRequest()
    {
        pthread_mutex_lock(&mutex);
        terminationRequest = true;
        pthread_mutex_unlock(&mutex);
    }

    bool checkTerminationRequest()
    {
        bool reqStatus = false;
        pthread_mutex_lock(&mutex);
        reqStatus = terminationRequest;
        pthread_mutex_unlock(&mutex);
        return reqStatus;
    }

};


int noOfRequests = 0;
char buffer[BUFFER_SIZE];
MessageQueue client_requests;
std::unordered_map<std::string, ST_ClientReq> results_table;

void foo() {for(long i = 0; i < 50000; i++);}

std::string add(int a, int b) {return std::to_string(a+b);}

std::string sort(std::vector<int> arr)
{
    std::sort(arr.begin(), arr.end());
    std::stringstream ss;
    for(int i = 0; i < arr.size(); i++)
    {
        ss << (std::to_string(arr.at(i))+" ");
        if (i != (arr.size() - 1))
            ss << " ";
    }
    return ss.str();
}

std::string generateTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch())%1000000;
    std::tm localTime = *std::localtime(&now_time);
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y%m%d%H%M%S") << std::setw(6) << std::setfill('0') << now_us.count(); 
    
    return oss.str();
}

void* client_handler(void* stp_ClientConnection)
{
    int client_socket = ((ST_ClientConnection*)stp_ClientConnection)->client_socket;
    
    while(true)
    {
        int bytesReceived;
        char buffer[BUFFER_SIZE];
        bytesReceived = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if(bytesReceived > 0)
        {
            buffer[bytesReceived] = '\0';
            std::string client_message(buffer), request, response;
            std::stringstream ss(client_message);
            std::string message_type;
            ss >> message_type;
            if(strcmp(message_type.c_str(), "req") == 0)
            {
                request = client_message.substr(4);
                std::string timeHash = generateTimestamp();
                results_table[timeHash] = {request, ""};
                client_requests.push(timeHash);
                response = "ACK "+timeHash;
            }
            else if(strcmp(message_type.c_str(), "res") == 0)
            {
                request = client_message.substr(4);
                ST_ClientReq temp = results_table[request];
                if(temp.res.empty())
                    response = "NAK";
                else
                    response = temp.res;
            }
            else if(strcmp(message_type.c_str(), "78Be1")==0)
            {
                response = "78Be1";
                std::cout << "Client issued a termination request..." << std::endl;
                client_requests.setTerminationRequest();
                int sentBytes = send(client_socket, response.c_str(), response.length(), 0);
                if(sentBytes < 0)
                {
                    std::cerr << "Send failed..." << std::endl;
                    break;
                }
                break;
            }
            else
            {
                response = "Invalid request";
            }

            int sentBytes = send(client_socket, response.c_str(), response.length(), 0);
            if(sentBytes < 0)
            {
                std::cerr << "Send failed..." << std::endl;
                break;
            }
        }
        else
        {
            std::cerr << "Receive error..." << std::endl;
            break;
        }
    }
    close(client_socket);
}

void* request_handler(void* unused)
{
    while(true)
    {   
        if((client_requests.checkTerminationRequest() == true) && (client_requests.size() == 0))
            break;

        std::string timeHash = client_requests.front();
        ST_ClientReq temp = results_table[timeHash];
        std::string request = temp.req;
        std::stringstream ss(request);
        std::string operation, response;
        ss >> operation;


        if(strcmp(operation.c_str(), "add") == 0)
        {
            int a, b;
            ss >> a >> b;
            response = add(a, b);
        }
        else if(strcmp(operation.c_str(), "foo") == 0)
        {
            foo();
            response = " ";
        }
        else if(strcmp(operation.c_str(), "sort") == 0)
        {
            std::vector<int> arr;
            int num;
            while(ss >> num)
                arr.push_back(num);
            response = sort(arr);
        }

        temp.res = response;
        results_table[request] = temp;
        client_requests.pop();
    }
}

int main()
{
    int server_socket, client_socket;
    ST_sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(bind(server_socket, (ST_sockaddr*)&server_addr, client_addr_len) < 0)
    {
        std::cerr << "Bind failed..." << std::endl;
        return 1;
    }

    if(listen(server_socket, 1) < 0)
    {
        std::cerr << "Listen failed..." << std::endl;
        return 1;
    }

    std::cout << "Server is waiting for connections..." << std::endl;

    while(client_socket = accept(server_socket, (ST_sockaddr*)&client_addr, &client_addr_len))
    {
        std::cout << "Press \"Ctrl + C\" to terminate this instance..." << std::endl;
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::cout << "Server connected to " << clientIP << std::endl;
        ST_ClientConnection st_ClientConnection;
        st_ClientConnection.client_socket = client_socket;
        strcpy(st_ClientConnection.clientIP, clientIP);
        
        /*Call threads*/
        pthread_t reqThread, workerThread, respThread;

        pthread_create(&workerThread, nullptr, client_handler, &st_ClientConnection);
        pthread_create(&reqThread, nullptr, request_handler, &st_ClientConnection);

        pthread_detach(reqThread);
        pthread_detach(workerThread);
    }
    return 0;
}