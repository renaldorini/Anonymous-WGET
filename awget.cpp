#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <errno.h> 
#include <netdb.h> 
#include <sys/types.h> 
#include <netinet/in.h> 
#include <sys/socket.h> 
#include <sys/stat.h> 
#include <arpa/inet.h> 
#include <ctype.h> 
#include "awget.h" 
#include <vector> 
#include <boost/algorithm/string.hpp> 
#include <iostream> 
#include <fstream>
using namespace boost;
using namespace std; 

int main(int argc, char *argv[]) {
    	string url;
	chainlist_packet packet;
    	//a chain file was passed in
	if(argc > 1)
		url = argv[1];
	string file;
    	if(argc == 4)
    	{
		if(strcmp(argv[2], "-c") != 0)
		{
			cout << "Error: Not correct startup." << endl;
			cout << "Expected: ./awget <URL> [-c chainfile]" << endl;
			exit(0);
		}
		file = argv[3];
    	}
    	//a chainfile has not been passed in
    	else if (argc == 2)
    	{
		file = "chaingang.txt";
    	}
	else
	{
		cout << "Expected: ./awget <URL> [-c chainfile]" << endl;
		exit(0);
	}
    	ifstream read(file);
	if ( !read.is_open() )
	{
		cout << "ERROR: awget failed to located chainfile" << endl;
		exit(0);
	}
	cout << "Request: " << url << endl;
    	//count the number of IP addresses and Ports in the text file
	string chainLine;
	vector<string> chainLines;
	getline(read, chainLine);
	unsigned short numOfAddr = stoi(chainLine);
	if (numOfAddr <= 0)
	{
		cout << "Error: Chainlist read error. Number too low" << endl;
		exit(0);
	}
	packet.numberChainlist = numOfAddr - 1;
	cout << "Chainlist is " << endl;
	unsigned short readChains = 0;
    	while(getline(read, chainLine)){
		cout << "<" << chainLine << ">" << endl;
		if ( chainLine.find(" ") > chainLine.size() )
		{
				cout << "Error: Problem parsing chainfile" << endl;
				exit(0);
		}
		chainLine = chainLine.replace(chainLine.find(" "), 1, ":");
		chainLines.push_back(chainLine);
		readChains++;
    	}
	if (readChains != numOfAddr)
	{
		cout << "Error: Chainlist read error. Mismatched number of entries" 
<< endl;
		exit(0);
	}
    	read.close();
	//Generate a random number between 0 and numOfAddr;
    	srand(time(NULL));
	int randIP = rand()%numOfAddr;
    	//Extract IP address and the port number from the line
	vector<string> ipAndPort;
	string nextStone = chainLines.at(randIP);
	cout << "Next SS is <" << nextStone << ">" << endl;
	chainLines.erase(chainLines.begin() + randIP);
	split(ipAndPort, nextStone, is_any_of(":"));
	string IP4 = ipAndPort[0];
	int port = stoi(ipAndPort[1]);
    	string chainlistStr("");
	for(int i = 0; i < chainLines.size(); i++)
	{
		chainlistStr = chainlistStr + " " + chainLines.at(i);
	}
	chainlistStr += " ";
	packet.chainlist = chainlistStr.c_str();
	packet.chainlistLength = chainlistStr.length();
	url += " ";
	packet.url = url.c_str();
	packet.urlLength = url.length();
    	//Create the socket and connect with the client//
    	char sendHeader[6];
    	char sendBuf[packet.chainlistLength + packet.urlLength + 2];
	memset(sendHeader, 0, 6);
	memset(sendBuf, 0, packet.chainlistLength + packet.urlLength + 2);
	unsigned short wrap = htons(packet.chainlistLength + 1);
    	memcpy(sendHeader, &wrap, 2);
	unsigned short wrapAgain = htons(packet.urlLength + 1);
    	memcpy(sendHeader + 2, &wrapAgain, 2);
   	unsigned short wrapThrice = htons(packet.numberChainlist);
   	memcpy(sendHeader + 4, &wrapThrice, 2);
   	memcpy(sendBuf, packet.chainlist + '\0', packet.chainlistLength + 1);
   	memcpy(sendBuf + packet.chainlistLength + 1, packet.url + '\0', 
packet.urlLength + 1);
    	char filename[256];
    	int clientSocket;
    	struct sockaddr_in remoteAddress;
   	FILE *recFile;
    	//remoteAddress
    	memset(&remoteAddress, 0, sizeof(remoteAddress));
    	remoteAddress.sin_family = AF_INET;
    	remoteAddress.sin_addr.s_addr=inet_addr(IP4.c_str());
    	remoteAddress.sin_port = htons(port);
    	//create socket
    	clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    	if (clientSocket == -1)
    	{
        	cout << "ERROR: CREATING THE SOCKET" << endl;
        	exit(0);
    	}
    	//connect
    	if (connect(clientSocket, (struct sockaddr *)&remoteAddress, sizeof(struct 
sockaddr)) == -1)
    	{
        	cout << "ERROR: CONNECTING TO THE SOCKET" << endl;
        	exit(0);
    	}
	if ( send(clientSocket, sendHeader, 6, 0) < 0)
	{
		cout << "ERROR: BAD SEND" << endl;
		exit(0);
	}
   	if( send(clientSocket, sendBuf, url.length() + chainlistStr.length() + 2, 0) 
< 0 )
	{
		cout << "ERROR: BAD SEND" << endl;
		exit(0);
	}
    	//receive file size
	bool fileTransfer = false;
	unsigned long fileSize = -1;
	unsigned short fileNameLength = -1;
	char fileHeader[6];
	cout << "Waiting for file..." << endl;
	if ( (recv(clientSocket, fileHeader, 6, 0) < 0) )
	{
		exit(1);
	}
	memcpy(&fileSize, fileHeader, 4);
	fileSize = ntohl(fileSize);
	memcpy(&fileNameLength, fileHeader + 4, 2);
	fileNameLength = ntohs(fileNameLength);
	if ( fileSize == 0 )
	{
		cout << "Error: wget failed to grab file correctly" << endl;
		exit(1);
	}
	char fileNameRecv[fileNameLength];
	recv(clientSocket, fileNameRecv, fileNameLength, 0);
	string fileName(fileNameRecv);
	trim(fileName);
	bool allPacketsTransferred = false;
	unsigned long recFileSize = 0;
	recFile = fopen(fileName.c_str(), "wb");
	cout << "Begin transmission from last stone" << endl;
	while(!allPacketsTransferred)
	{
		unsigned short packetSize = -1;
		char dataSizeBuffer[2];
		if ( (recv(clientSocket, dataSizeBuffer, 2, 0) < 0) )
		{
			exit(1);
		}
		memcpy(&packetSize, dataSizeBuffer, 2);
               	packetSize = ntohs(packetSize);
		char data[packetSize];
		if ( (recv(clientSocket, data, packetSize, MSG_WAITALL) < 0) )
		{
			exit(1);
		}
		fwrite(data, packetSize, 1, recFile);
		recFileSize += packetSize;
		if (recFileSize == fileSize)
			allPacketsTransferred = true;
	}
    	fclose(recFile);
    	close(clientSocket);
	cout << "Received file: " << fileName << endl;
	cout << "Goodbye!" << endl;
    	return 0;
}
