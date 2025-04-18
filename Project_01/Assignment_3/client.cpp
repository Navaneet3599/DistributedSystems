#include <iostream>
#include <ctime>
#include <vector>
#include <arpa/inet.h>
#include <unistd.h>

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

int main(int argc, char* argv[])
{
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    ST_sockaddr_in server_addr, client_addr;
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    std::srand(std::time(0));
    std::string request = "", response = "";
    
    int loopBreaker = argc-1;
    if(argc < 2)
        loopBreaker = MAX_NO_REQUESTS;

    if(connect(client_socket, (ST_sockaddr*)&server_addr, sizeof(server_addr)) == 0)
    {
        while(true)
        {
            if(argc < 2)
            {
                int random_number = std::rand() % operations.size();
                request = operations.at(random_number);
            }
            else
            {
                for(int i = 1; i < argc; i++)
                    request = request + argv[i] + " ";
            }

            //request += '\n';
            std::cout << "Client<<" + request << std::endl;
            int bytesSent = send(client_socket, request.c_str(), request.length(), 0);
            if(bytesSent < 0)
            {
                std::cerr << "Send error..." << std::endl;
                std::cout << errno << std::endl;
                return 0;
            }
            
            char buffer[BUFFER_SIZE];
            int recvBytes = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if(recvBytes < 0)
            {
                std::cerr << "Read error..." << std::endl;
                std::cout << errno << std::endl;
                return 0;
            }
            std::cout << buffer << std::endl;

            noOfRequests++;
            if(noOfRequests == loopBreaker)
            {
                break;
            }
            sleep(0.5);
        }
        request = "78Be1";
        send(client_socket, request.c_str(), request.length(), 0);
    }
    sleep(1);
    close(client_socket);
    return 0;
}
