#include <stdio.h>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <csignal>
#include <thread>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <fstream>
#include "awget.h"

using namespace std;
using namespace boost;

#define DEFAULTPORT 9000

int serverSock = -1;

void cleanExit(int exitCode, string message)
{
	cout << message << endl;
	if (serverSock != -1)
		close(serverSock);
	exit(exitCode);
}

void sendSystemWget(string url)
{
	string command = "wget " + url;
	const char* wgetMessage = command.c_str();
	system(wgetMessage);
}

string createFinalRequestUrl(string url)
{
	string prefix("");
	trim(url);
        if( contains(url, ("://")) )
        {
		prefix = url.substr(0, url.find("://") + 3);
                url = url.substr(url.find("://") + 3);
        }
        int countSlashes = count(url.begin(), url.end(), '/');
        if ( countSlashes < 2 )
        {
                if( countSlashes == 0 )
                {
                        url = url + "/";
                }
		else if(url.find_last_of("/") != url.size() -1)
		{
			return prefix + url;
		}
                url = url + "index.html";
        }
	url = prefix + url;
	return url;
}

string parseFileName(string url)
{
	return url.substr(url.find_last_of("/") + 1);
}

vector<string> splitChainlistFromLastStone(char* chainlistChar)
{
	string chainlist = string(chainlistChar);

	vector<string> splitChainlist;
	trim(chainlist);
	split(splitChainlist, chainlist, is_any_of(" "), token_compress_on);

	return splitChainlist;
}

int selectRandomStoneIndex(int max)
{
	srand(time(NULL));

	return rand() % max;
}



int connectToNextStone(string ip, int port)
{
	struct sockaddr_in nextStoneAddr;
	nextStoneAddr.sin_family = AF_INET;
	inet_pton(AF_INET, ip.c_str(), &nextStoneAddr.sin_addr.s_addr);
	nextStoneAddr.sin_port = htons(port);
	int nextStoneSock;

        if ( (nextStoneSock = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
        {
                string errorMessage("Error: Unable to open socket");
                cleanExit(1, errorMessage);
        }
        if ( (connect(nextStoneSock, (struct sockaddr*)&nextStoneAddr, sizeof(nextStoneAddr))) < 0 )
        {
                string errorMessage("Error: Unable to bind socket");
                cleanExit(1, errorMessage);
        }

	return nextStoneSock;
}

int displayHelpInfo() 
{
	cout << "Welcome to Stepping Stone!" << endl << endl;
	cout << "To start the stepping stone type ./ss" << endl;
	cout << "The flag '-p' can be used to define a port other than teh default 9000" << endl << endl;
	cleanExit(1, "");
}


void handleConnectionThread(int previousStoneSock) 
{
	int nextStoneSock;
	char messageHeaderBuffer[6];
	chainlist_packet packetInfo;

	if ( recv(previousStoneSock, messageHeaderBuffer, 6, 0) < 0 )
	{
		cleanExit(1, "Error: recv failed");
	}
	cout << endl << endl;
	unsigned short sizeChainlist = -1;
	memcpy(&sizeChainlist, messageHeaderBuffer, 2);
	packetInfo.chainlistLength = ntohs(sizeChainlist);

	unsigned short sizeUrl = -1;
	memcpy(&sizeUrl, messageHeaderBuffer + 2, 2);
	packetInfo.urlLength = ntohs(sizeUrl);

	unsigned short numStonesLeft = -1;
	memcpy(&numStonesLeft, messageHeaderBuffer + 4, 2);
	numStonesLeft = ntohs(numStonesLeft);
	packetInfo.numberChainlist = numStonesLeft;

	char messageBuffer[packetInfo.urlLength + packetInfo.chainlistLength];
	if ( recv(previousStoneSock, messageBuffer, packetInfo.urlLength + packetInfo.chainlistLength, 0) < 0 )
	{
		cleanExit(1, "Error: recv failed");
	}

	char chainlist[packetInfo.chainlistLength + 1];
	memcpy(chainlist, messageBuffer, packetInfo.chainlistLength);

	char url[packetInfo.urlLength + 1];
	memcpy(url, messageBuffer + packetInfo.chainlistLength, packetInfo.urlLength);
	cout << "Request: " << url << endl;

	if(packetInfo.numberChainlist > 0)
	{
		vector<string> chainlistSplit = splitChainlistFromLastStone(chainlist);
		cout << "Chainlist is: " << endl;
		for(int i = 0; i < numStonesLeft; i++)
		{
			cout << "<" << chainlistSplit.at(i) << ">" << endl;
		}
		int nextStoneIndex = selectRandomStoneIndex(numStonesLeft);
		string nextStone = chainlistSplit.at(nextStoneIndex);
		chainlistSplit.erase(chainlistSplit.begin() + nextStoneIndex);
		cout << "Next SS is <" << nextStone << ">" << endl;

		vector<string> nextStoneIpAndPort;
		split(nextStoneIpAndPort, nextStone, is_any_of(":"));

		nextStoneSock = connectToNextStone(nextStoneIpAndPort[0], stoi(nextStoneIpAndPort[1]));

		string combineChainlist("");
		for(int i = 0; i < chainlistSplit.size(); i++)
		{
			combineChainlist += " " + chainlistSplit.at(i) ;
		}

		char chainlistHeader[6];
		char chainlistPacket[packetInfo.urlLength + combineChainlist.length()];
		memset(chainlistHeader, 0, 6);
		memset(chainlistPacket, 0, packetInfo.urlLength + combineChainlist.length());

		unsigned short wrapChainlistSize = htons(combineChainlist.length());
		memcpy(chainlistHeader, &wrapChainlistSize, 2);
		memcpy(chainlistHeader + 2, &sizeUrl, 2);
		unsigned short wrapNumber = htons(chainlistSplit.size());
		memcpy(chainlistHeader + 4, &wrapNumber, 2);
		memcpy(chainlistPacket, combineChainlist.c_str(), combineChainlist.length());
		memcpy(chainlistPacket + combineChainlist.length(), url, packetInfo.urlLength);

		if ( (send(nextStoneSock, chainlistHeader, 6, 0)) < 0)
			cout << "Error: Bad send" << endl;

		if ( (send(nextStoneSock, chainlistPacket, packetInfo.urlLength + combineChainlist.length(), 0)) < 0)
			cout << "Error: Bad send" << endl;

		bool fileTransfer = false;
		unsigned long fileSize = 0;
		unsigned short fileNameLength = 0;
		char fileHeader[6];
		cout << "Waiting for file..." << endl;
		if ( (recv(nextStoneSock, fileHeader, 6, 0) < 0) )
		{
			cleanExit(1, "Error: Failed to read file header");
		}

		memcpy(&fileSize, fileHeader, 4);
		fileSize = ntohl(fileSize);

		// If file size = 0 then system call was bad.
		if( fileSize == 0 )
		{
			cout << "Warning: Something went wrong with system call forwarding packet" << endl;
			if( (send(previousStoneSock, fileHeader, 6, 0) <0) )
			{
				cleanExit(1, "Error: Bad send");
			}
			return;
		}
		memcpy(&fileNameLength, fileHeader + 4, 2);
		fileNameLength = ntohs(fileNameLength);

		send(previousStoneSock, fileHeader, 6, 0);
		char fileName[fileNameLength];
		recv(nextStoneSock, fileName, fileNameLength, 0);

		cout << "Relaying " << fileName << endl;
		send(previousStoneSock, fileName, fileNameLength, 0);

		bool allPacketsTransferred = false;
		unsigned long recFileSize = 0;
		while(!allPacketsTransferred)
		{
			unsigned short packetSize = -1;
			char dataSizeBuffer[2];
			if ( (recv(nextStoneSock, dataSizeBuffer, 2, 0) < 0) )
			{
				cleanExit(1, "Error: Failed to read data size");
			}

			memcpy(&packetSize, dataSizeBuffer, 2);
                	packetSize = ntohs(packetSize);
			send(previousStoneSock, dataSizeBuffer, 2, 0);

			char data[packetSize];
			if ( (recv(nextStoneSock, data, packetSize, MSG_WAITALL) < 0) )
			{
				cleanExit(1, "Error: Data read went wrong");
			}
			send(previousStoneSock, data, packetSize, 0);

			recFileSize += packetSize;

			if (recFileSize == fileSize)
				allPacketsTransferred = true;
		}
		close(nextStoneSock);
		cout << "Finished relaying..." << endl << "Going back to listening" << endl;
	}
	else
	{
		string requestUrl = createFinalRequestUrl(url);
		string fileName = parseFileName(requestUrl);
		cout << "Issuing wget for <" << fileName << ">" << endl;
		sendSystemWget(requestUrl);
		trim(fileName);
		cout << "File received. Opening <" << fileName << ">" << endl;
		ifstream file(fileName, ios::in|ios::binary);
		unsigned long fileSize = 0;
		if(!file.is_open())
		{ 
			cout << "Error: Unable to open file" << endl;
		}
		else
		{
			file.seekg(0, file.end);
			fileSize = file.tellg();
			cout << "File Size: " << fileSize << endl;
			file.seekg(0, file.beg);
		}

		// Send file header information
		char fileHeader[fileName.length() + 7];
		memset(fileHeader, 0, fileName.length() + 6);

		unsigned long wrapSize = htonl(fileSize);
		memcpy(fileHeader, &wrapSize, 4);
		unsigned short wrapFileNameSize = htons(fileName.length() + 1);
		memcpy(fileHeader + 4, &wrapFileNameSize, 2);
		fileName += " ";
		memcpy(fileHeader + 6, fileName.c_str() + '\0', fileName.length());

		if ( (send(previousStoneSock, fileHeader, fileName.length() + 6, 0)) < 0 )
			cleanExit(1, "Error: Bad send");
		if(fileSize == 0)
			return;

		int bufferSize = 1000;
		char dataRead[bufferSize];

		cout << "Beginning transfer of file... " << endl;
		while( !file.eof() )
		{
			file.read(dataRead, bufferSize) ;
			unsigned short bytesRead = file.gcount();

			// Wrap message size then send
			char dataHeader[2];
			memset(dataHeader, 0, 2);
			unsigned short wrap = htons(bytesRead);
			memcpy(dataHeader, &wrap, 2);
			send(previousStoneSock, dataHeader, 2, 0);

			char dataMessage[bytesRead];
			memset(dataMessage, 0, bytesRead);

			memcpy(dataMessage, dataRead, bytesRead);

			if ( (send(previousStoneSock, dataMessage, bytesRead, 0)) < 0 )
				cleanExit(1, "Error: Bad send");
		}
		cout << "File transfer finished. Cleaning up" << endl;
		file.close();
		cout << "Finished transmitting file" << endl;
		cout << "Removing " << fileName << endl;
		system( ("rm " + fileName).c_str() );
	}

	close(previousStoneSock);
}

void signalHandler(int signal)
{
	string message("Error: Handled signal interruption");
	cleanExit(signal, message);
}

// Expecting Call: ./ss [-p port]
int main(int argc, char* argv[]) 
{
	// handle signals
	signal(SIGINT/SIGTERM/SIGKILL, signalHandler);

	struct sockaddr_in sin;
    	int port;

	if(argc == 1)
	{
        	port = DEFAULTPORT;
    	}
	else
	{
        	if(argc != 3 || strcmp(argv[1], "-p") != 0)
		{
           		displayHelpInfo();
        	}
        	port = atoi(argv[2]);
    	}

	char hostname[256];

	if(gethostname(hostname, sizeof hostname) != 0)
	{
		string errorMessage("Error: Could not find hostname");
		cleanExit(1, errorMessage);
	}
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htons(INADDR_ANY);
	sin.sin_port = htons(port);

	// I modified code found from beej guide that showed how to get ip from hostname
    	struct addrinfo hints, *res, *p;
    	char ipstr[INET_ADDRSTRLEN];

    	memset(&hints, 0, sizeof hints);
    	hints.ai_family = AF_INET; 
    	hints.ai_socktype = SOCK_STREAM;
    	getaddrinfo(hostname, NULL, &hints, &res);

    	for(p = res;p != NULL; p = p->ai_next) 
    	{
        	if (p->ai_family == AF_INET) 
		{
           	 	struct sockaddr_in *ip = (struct sockaddr_in *)p->ai_addr;
            		void* addr = &(ip->sin_addr);
            		inet_ntop(AF_INET, &(ip->sin_addr), ipstr, sizeof ipstr);
            		break;
		}
    	}
  	freeaddrinfo(res);

	cout << "Stepping Stone " << ipstr << ":" << port << endl;

	if ( (serverSock = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
	{
		string errorMessage("Error: Unable to open socket");
		cleanExit(1, errorMessage);
	}
	if ( (bind(serverSock, (struct sockaddr*)&sin, sizeof(sin))) < 0 )
	{
		string errorMessage("Error: Unable to bind socket");
		cleanExit(1, errorMessage);
	}

	listen(serverSock, 5);

	while(true)
	{
		cout << "Listening for new connection..." << endl;
		int incomingSock;
		struct sockaddr_in clientAddr;
		socklen_t addrSize = sizeof clientAddr;
		if ( (incomingSock = accept(serverSock, (struct sockaddr*)&clientAddr, &addrSize)) < 0 )
		{
			string errorMessage("Error: Problem accepting client.");
			cleanExit(1, errorMessage);
		}
		string ipString(ipstr);
		char remoteIp[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(clientAddr.sin_addr), remoteIp, INET_ADDRSTRLEN);
		cout << "Connection from: " << remoteIp << ":" << ntohs(clientAddr.sin_port) << endl;
		//handleConnectionThread(incomingSock);
		thread ssSockThread(handleConnectionThread, incomingSock);
		ssSockThread.detach();
	}

    	return 0;
}
