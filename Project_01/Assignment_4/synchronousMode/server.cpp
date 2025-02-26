#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_NO_TRANSFERS 50
#define NUMBER_OF_SERVER_THREADS 5

typedef struct sockaddr ST_sockaddr;
typedef struct sockaddr_in ST_sockaddr_in;
typedef struct
{
    int client_socket;
    char clientIP[INET_ADDRSTRLEN];
}ST_ClientConnection;

int noOfRequests = 0;
char buffer[BUFFER_SIZE];

void foo() {for(long i = 0; i < 10000; i++);}
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

void client_handler(void* stp_ClientConnection)
{
    int client_socket = ((ST_ClientConnection*)stp_ClientConnection)->client_socket;
    int bytesReceived;

    while(true)
    {
        memset(buffer, 0, BUFFER_SIZE);
        bytesReceived = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if(bytesReceived > 0)
        {
            buffer[bytesReceived] = '\0';
            std::string request(buffer, bytesReceived);
            //std::cout << request << std::endl;

            if(strcmp(request.c_str(), "78Be1") == 0)
            {
                std::cout << "Client issued a termination request..." << std::endl;
                break;
            }

            std::stringstream ss(request);
            std::string operation, response;
            ss >> operation;
            
            //std::cout << "Requested operation is " << operation << std::endl;

            if(strcmp(operation.c_str(), "add") == 0)
            {
                int a, b;
                ss >> a >> b;
                response = add(a, b);
            }
            else if(strcmp(operation.c_str(), "foo") == 0)
            {
                foo();
                response = "";
            }
            else if(strcmp(operation.c_str(), "sort") == 0)
            {
                std::vector<int> arr;
                int num;
                while(ss >> num)
                    arr.push_back(num);
                response = sort(arr);
            }
            response = "Server>>" + operation + " " + response;
            int sentBytes = send(client_socket, response.c_str(), response.length(), 0);
            if(sentBytes < 0)
            {
                std::cerr << "Send error..." << std::endl;
                std::cerr << errno << std::endl;
            }
        }
        else
        {
            std::cerr << "Receive error..." << std::endl;
            std::cerr << errno << std::endl;
            return;
        }
    }
    close(client_socket);
    std::cout << "Connnection terminated with " << ((ST_ClientConnection*)stp_ClientConnection)->clientIP << std::endl;
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

    if(listen(server_socket, NUMBER_OF_SERVER_THREADS) < 0)
    {
        std::cerr << "Listen failed..." << std::endl;
        return 1;
    }

    std::cout << "Server is waiting for connections..." << std::endl;
    while(client_socket = accept(server_socket, (ST_sockaddr*)&client_addr, &client_addr_len))
    {
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::cout << "Server connected to " << clientIP << std::endl;
        ST_ClientConnection st_ClientConnection;
        st_ClientConnection.client_socket = client_socket;
        strcpy(st_ClientConnection.clientIP, clientIP);
        client_handler(&st_ClientConnection);
    }
    return 0;
}