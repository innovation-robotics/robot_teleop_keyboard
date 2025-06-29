/*
 *  RPLIDAR
 *  Ultra Simple Data Grabber Demo App
 *
 *  Copyright (c) 2009 - 2014 RoboPeak Team
 *  http://www.robopeak.com
 *  Copyright (c) 2014 - 2019 Shanghai Slamtec Co., Ltd.
 *  http://www.slamtec.com
 *
 */
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <netdb.h>
#include <iostream>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <mutex>
#include <vector>
#include <sys/time.h>

#include "network_to_serial_bridge/arduino_comms_serial.hpp"

char c;

int GetKeyPress()
{
    struct termios oldSettings, newSettings;

    tcgetattr( fileno( stdin ), &oldSettings );
    newSettings = oldSettings;
    newSettings.c_lflag &= (~ICANON & ~ECHO);
    tcsetattr( fileno( stdin ), TCSANOW, &newSettings );    

    while ( 1 )
    {
        fd_set set;
        struct timeval tv;

        tv.tv_sec = 10;
        tv.tv_usec = 0;

        FD_ZERO( &set );
        FD_SET( fileno( stdin ), &set );

        int res = select( fileno( stdin )+1, &set, NULL, NULL, &tv );

        if( res > 0 )
        {
            printf( "Input available\n" );
            read( fileno( stdin ), &c, 1 );
        }
        else if( res < 0 )
        {
            perror( "select error" );
            break;
        }
        else
        {
            printf( "Select timeout\n" );
        }
    }

    tcsetattr( fileno( stdin ), TCSANOW, &oldSettings );
    return 0;
}

void Terminate(const int &server_fd, const int &new_socket)
{
    close(new_socket);
    shutdown(server_fd, SHUT_RDWR);
}

bool InitServer(int &server_fd, int &new_socket, int port, std::string label)
{
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
  
    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    {
        perror("socket failed");
        return false;
    }
  
    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) 
    {
        perror("setsockopt");
        return false;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
  
    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) 
    {
        perror("bind failed");
        return false;
    }
    
    std::cout << label << " bind successfully" << std::endl;
    
    if (listen(server_fd, 3) < 0) 
    {
        perror("listen");
        return false;
    }
    std::cout << label << " listen successfully" << std::endl;

    if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) 
    {
        perror("accept");
        return false;
    }
    std::cout << label << " accept successfully" << std::endl;

    return true;
}

std::vector<std::string> SplitBuffer(std::string s, std::string delimiter)
{
	std::vector<std::string> v;
	size_t pos = 0;
	std::string token;
	while ((pos = s.find(delimiter)) != std::string::npos) 
    {
		token = s.substr(0, pos);
		v.push_back(token);
		s.erase(0, pos + delimiter.length());
	}

	if(!v.empty())
	{
		v.push_back(s);
	}

	return v;
}

bool ReadNetworkMessage(const int &socket, std::string &msg)
{
    try
    {
        char buffer[1024];
        bzero(buffer, 1024);
        int n = read(socket, buffer, 1024);
        printf("received %s\n", buffer);
        
        if(n<=0)
        {
            printf("error2\n");
            return false;
        }
        msg = buffer;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }

    return true;
}

bool SendNetworkMessage(const int &socket, const std::string &msg)
{
    try
    {
        int n = send(socket, msg.c_str(), msg.length(), MSG_NOSIGNAL);
        if (n < 0)
        {
            printf("error1\n");
            return false;
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }

    return true;
}

std::vector<double> joint;
std::vector<double> delta;
ArduinoComms arm_comms_, base_comms;

void IncrementArmJoint(int joint_idx)
{
    joint[joint_idx] += delta[joint_idx];

    std::stringstream ss;
    ss << "$" << joint[0];
    for(int i = 1; i < joint.size(); i++)
    {
      ss << "," << joint[i];
    }
    ss << '\n';
    // std::string s = send_msg_get_response(ss.str());
    arm_comms_.send_msg(ss.str());
    std::cout << "increment joint: " << joint_idx << std::endl;
}

void DecrementArmJoint(int joint_idx)
{
    joint[joint_idx] -= delta[joint_idx];
    std::stringstream ss;
    ss << "$" << joint[0];
    for(int i = 1; i < joint.size(); i++)
    {
      ss << "," << joint[i];
    }
    ss << '\n';
    // std::string s = send_msg_get_response(ss.str());
    arm_comms_.send_msg(ss.str());
    std::cout << "decrement joint: " << joint_idx << std::endl;
}

int main(int argc, char *argv[])
{
    if(argc < 4)
    {
        return 0;
    }

    std::string arm_device(argv[1]);
    std::string arm_baud_rate_s(argv[2]);
    std::string base_device(argv[3]);
    std::string base_baud_rate_s(argv[4]);
    int arm_baud_rate = std::stoi(arm_baud_rate_s);
    int base_baud_rate = std::stoi(base_baud_rate_s);
    arm_comms_.connect(arm_device, arm_baud_rate, 1000);
    base_comms.connect(base_device, base_baud_rate, 1000);

    std::cout << "arm device : " << arm_device << std::endl;
    std::cout << "arm baud_rate : " << arm_baud_rate << std::endl;
    std::cout << "base device : " << base_device << std::endl;
    std::cout << "base baud_rate : " << base_baud_rate << std::endl;
    
    float stepsPerRad[7] = {10185.916357881,5602.253996835,22190.746351098,1018.591635788,5653.183578624,1018.591635788,1.0};  // microsteps/revolution (using 16ths) from observation, for each motor
    joint.resize(7);
    delta.resize(7);
    
    for(int i = 0; i < 6; i++)
    {
        joint[i] = 0.0;
        delta[i] = stepsPerRad[i]/500.0;
    }
    
    joint[6] = 90.0;
    delta[6] = 1.0;

    std::thread th1(GetKeyPress);
    th1.detach();

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    base_comms.send_msg("r\r");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    while(1)
    {
        char input;
        std::cin >> input;
        if(input == 'a')
        {
            IncrementArmJoint(0);
        }
        else if(input == 's')
        {
            IncrementArmJoint(1);
        }
        else if(input == 'd')
        {
            IncrementArmJoint(2);
        }
        else if(input == 'f')
        {
            IncrementArmJoint(3);
        }
        else if(input == 'g')
        {
            IncrementArmJoint(4);
        }
        else if(input == 'h')
        {
            IncrementArmJoint(5);
        }
        else if(input == 'j')
        {
            IncrementArmJoint(6);
        }
        else if(input == 'z')
        {
            DecrementArmJoint(0);
        }
        else if(input == 'x')
        {
            DecrementArmJoint(1);
        }
        else if(input == 'c')
        {
            DecrementArmJoint(2);
        }
        else if(input == 'v')
        {
            DecrementArmJoint(3);
        }
        else if(input == 'b')
        {
            DecrementArmJoint(4);
        }
        else if(input == 'n')
        {
            DecrementArmJoint(5);
        }
        else if(input == 'm')
        {
            DecrementArmJoint(6);
        }
        else if(input == 'r')
        {
            std::stringstream ss;
            ss << "$r" << '\n';
            arm_comms_.send_msg(ss.str());
            std::cout << "reset" << std::endl;
        }
        else if(input == '8')
        {
            std::stringstream ss;
            ss << "m " << 50 << " " << 50 << '\r';
            base_comms.send_msg(ss.str());
            std::cout << "forward" << std::endl;
        }
        else if(input == '2')
        {
            std::stringstream ss;
            ss << "m " << -50 << " " << -50 << '\r';
            base_comms.send_msg(ss.str());
            std::cout << "backward" << std::endl;
        }
        else if(input == '6')
        {
            std::stringstream ss;
            ss << "m " << 25 << " " << -25 << '\r';
            base_comms.send_msg(ss.str());
            std::cout << "right" << std::endl;
        }
        else if(input == '4')
        {
            std::stringstream ss;
            ss << "m " << -25 << " " << 25 << '\r';
            base_comms.send_msg(ss.str());
            std::cout << "left" << std::endl;
        }
        else if(input == '5')
        {
            std::stringstream ss;
            ss << "m " << 0 << " " << 0 << '\r';
            base_comms.send_msg(ss.str());
            std::cout << "stop" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::string response = base_comms.send_msg_get_response("e\r", true);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    }

    return 0;
}
