#include <iostream>
#include <ctime>
#include <vector>
#include <queue>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <pthread.h>

#define SERVER_IP "127.0.0.1"  // Localhost
#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_NO_REQUESTS 25
#define NUMBER_OF_CLIENT_THREADS 1

typedef struct sockaddr ST_sockaddr;
typedef struct sockaddr_in ST_sockaddr_in;

std::vector<std::string> operations =   {
                                            "add 11958 14725",
                                            "add 2160 5204",
                                            "add 528 640",
                                            "add 2394 8908",
                                            "add 15155 11534",
                                            "add 9017 11657",
                                            "add 11862 208",
                                            "add 3005 15471",
                                            "add 4537 16107",
                                            "add 551 10171",
                                            "sort 4630 560 10434 9399 12365 4579 9141 8983",
                                            "sort 9145 5634 13628 10184 1173 10441 2726 270 12629 1294 9117 6223 15152 5943",
                                            "sort 4029 2521 12296 11556 2065",
                                            "sort 3180 8389 5425 13442 14458 15992 3330 7494 7779 14858 7023 5124 3747 1747 1895 5391 3693",
                                            "sort 13939 8807 1232 8315 6393 1526 8611 14089 7863",
                                            "sort 4541 13402 6248 6773 12682 13400 13508 14506 12894 818 12534 4181",
                                            "sort 4351 13129 14075 10369 15162 3906 9431 16266 3419 7978 10796 13515 5264 15131 14306",
                                            "sort 5156 9789 10102 15111 11802 14231 180 14432 7632",
                                            "sort 14710 643 8856 9845 4679 15817 13189 12906 6916 14805 3056 4134 11164 13537 2908 11946 11708 9076 14664 1098 3617 3809",
                                            "sort 4694 10246 9200 13482 8098 6500",
                                            "foo",
                                            "foo",
                                            "foo",
                                            "foo",
                                            "foo",
                                            "foo",
                                            "foo",
                                            "foo",
                                            "foo",
                                            "foo"
                                        };
int noOfRequests = 0;
std::queue<std::string> client_requests;
std::queue<std::string> server_requests;
std::queue<std::string> server_response;
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

int main()
{
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    ST_sockaddr_in server_addr, client_addr;
    std::srand(std::time(0));
    int random_number;
    std::string request = "", response = "";

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    

    for(int i = 0; i < MAX_NO_REQUESTS ; i++)
    {
        random_number = std::rand() % operations.size();
        client_requests.push(operations.at(random_number));
        random_number = std::rand() % operations.size();
        client_requests.push(operations.at(random_number));
    }
    

    if(connect(client_socket, (ST_sockaddr*)&server_addr, sizeof(server_addr)) == 0)
    {
        while(true)
        {
            if(client_requests.size() != 0)
            {
                request = client_requests.front();
                std::cout << "Client-Client<<" + request << std::endl;
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
                client_requests.pop();
                response = "Client-Client>>" + operation + " " + response;
            }

            /*Sending requests to server*/
            if(server_requests.size() != 0)
            {
                request = server_requests.front();
                std::cout << "Client-Server<<" + request << std::endl;
                int bytesSent = send(client_socket, ("req "+request).c_str(), request.length()+4, 0);
                if(bytesSent < 0)
                {
                    std::cerr << "Send error..." << std::endl;
                    std::cout << errno << std::endl;
                    return 0;
                }

                memset(buffer, 0, BUFFER_SIZE);
                int recvBytes = recv(client_socket, buffer, BUFFER_SIZE, 0);
                if(recvBytes < 0)
                {
                    std::cerr << "Read error..." << std::endl;
                    std::cout << errno << std::endl;
                    return 0;
                }
                std::cout << buffer << std::endl;
                if(strncmp(buffer, "ACK", 3) == 0)
                {
                    std::cout << "Request in message queue..." << std::endl;
                    std::string temp(buffer);
                    std::stringstream ss(buffer);
                    std::string timeHash;
                    ss >> timeHash;
                    ss >> timeHash;
                    server_requests.pop();
                    server_response.push(timeHash); //send rest here
                }
                else if(strcmp(buffer, "NAK") == 0)
                {
                    std::cout << "Server is busy..." << std::endl;
                }

            }
            
            /*Receiving responses from server*/
            if(server_response.size() != 0)
            {
                request = server_response.front();
                std::cout << "Client-Server<<" + request << std::endl;
                int bytesSent = send(client_socket, ("res "+request).c_str(), request.length()+4, 0);
                if(bytesSent < 0)
                {
                    std::cerr << "Send error..." << std::endl;
                    std::cout << errno << std::endl;
                    return 0;
                }

                memset(buffer, 0, BUFFER_SIZE);
                int recvBytes = recv(client_socket, buffer, BUFFER_SIZE, 0);
                if(recvBytes < 0)
                {
                    std::cerr << "Read error..." << std::endl;
                    std::cout << errno << std::endl;
                    return 0;
                }

                if(strcmp(buffer, "NAK") == 0)
                    std::cout << "Computation in progress at server..." << std::endl;
                else
                    std::cout << buffer << std::endl;
            }

            if((client_requests.size() == 0) && (server_requests.size() == 0) && (server_response.size() == 0))
                break;
            sleep(0.5);
        }
        request = "78Be1";
        send(client_socket, request.c_str(), request.length(), 0);
    }
    sleep(1);
    close(client_socket);
    return 0;
}
