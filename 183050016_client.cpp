/**
 *  KEY-VALUE CLIENT -- CS-744 ASSIGNMENT 4
 *  AUTHOR : DIPTYAROOP MAJI
 *  ROLL : 183050016
*/

// THIS WILL ACT AS LOAD GENERATOR & LOAD BALANCER

/*
* #####
* Server code used in VMs. Bugs present, but too lazy to fix all of them
* #####
*/

#include "headers.h"

using namespace std;

#define MAX_SERVER_NAME_LENGTH 16
#define BUF_LEN 256
#define MAX_INPUT_SIZE 4197
#define NUM_CMDS 4
#define MAX_CMD_LEN 11
#define PORT_NUM 8080
#define NUM_UNIQUE_KEYS 10000
#define MAX_VALUE_SIZE 1024
#define NUM_ALPHABETS 26
#define MAX_CLIENTS 100
#define MAX_SERVERS 100
#define MEGA_POW 1000000
#define NOTIF_PORT 6000
#define MAX_AVAILABLE_SERVERS 2
#define REASONABLE_TIME_GAP 3

typedef struct addr
{
	char serverName[MAX_SERVER_NAME_LENGTH];
	int threadID;
	int loadTestDuration;
} clientRequirements;

static struct timeval t0, t1;

map<const char *, int> cmdMap;
map<const char *, int>::iterator iter;
int clientFD[MAX_CLIENTS*MAX_AVAILABLE_SERVERS], port, n;
struct sockaddr_in serverAddr;
struct hostent *server;
char notifBuf[BUF_LEN];
char buf[MAX_CLIENTS*MAX_AVAILABLE_SERVERS][BUF_LEN+1];
char const *cmdList[] = {"create", "read", "update", "delete", "disconnect"};
char const alpha[] = "abcdefghijklmnopqrstuvwxyz";
pthread_t clientThreadPool[MAX_CLIENTS*MAX_AVAILABLE_SERVERS];
pthread_t notificationThread;
int numRequests[MAX_CLIENTS*MAX_AVAILABLE_SERVERS];
double responseTime[MAX_CLIENTS*MAX_AVAILABLE_SERVERS];
clientRequirements cliReq[MAX_CLIENTS*MAX_AVAILABLE_SERVERS];
int numClients=0;
int gKey;
bool timeout;
static int forceQuit = false;
static bool balanceLoad = false;
static int numClientsRemovedFromLoad = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static char nwIface[MAX_CMD_LEN];

bool connected();
void *clientThreadInit(void *);
int constructMessage(int, char [], char []);
void disconnectFromServer(clientRequirements *);
bool connectToServer(clientRequirements *, int);
void createKVPair(clientRequirements *);
void readKVPair(clientRequirements *);
bool updateKVPair(clientRequirements *);
bool deleteKVPair(clientRequirements *);
void communicateWithServer(clientRequirements *, const char *, char[]);
void cmdLineArgsValidityCheck(int, char **);
void signalHandler();
void *handleNotification(void *);

/*inline bool connected(int threadId)
{
	return isConnected;
}*/

void signalHandler(int sigNum)
{
	forceQuit = true;
	sleep(3);
	exit(0);
}

void *handleNotification(void *args)
{
	int serverFD, notifFD[MAX_SERVERS], port;
	int notifFDIdx = 0;
	struct sockaddr_in serverAddr, clientAddr;
	socklen_t clientLength;
	int threadIDArr[MAX_CLIENTS];

	char selfIPAddr[16];
    int fd;
    struct ifreq ifr;
    /*AF_INET - to define network interface IPv4*/
    /*Creating soket for it.*/
    fd = socket(AF_INET, SOCK_DGRAM, 0);     
    /*AF_INET - to define IPv4 Address type.*/
    ifr.ifr_addr.sa_family = AF_INET;
    memcpy(ifr.ifr_name, nwIface, IFNAMSIZ-1);
    /*Accessing network interface information by passing address using ioctl.*/
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);
    /*Extract IP Address*/
    strcpy(selfIPAddr,inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    printf("System IP Address is: %s\n",selfIPAddr);
	
	if((serverFD = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("[handleNotif] Error opening socket. Terminating!\n");
		exit(1);
	}
	memset(&serverAddr, 0, sizeof(serverAddr));
	port = NOTIF_PORT;
	serverAddr.sin_family = AF_INET;
	inet_pton(AF_INET, selfIPAddr, &(serverAddr.sin_addr));
	serverAddr.sin_port = htons(port);

	if(bind(serverFD, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0)
	{
		printf("Error in binding socket!\n");
		exit(1);
	}
	listen(serverFD, MAX_CLIENTS);
	
	int fdIndex = 0;
	// while(1)
	{
		//int fdIndex = 0;//findFreeFD();
		clientLength = sizeof(clientAddr);
		if((notifFD[notifFDIdx] = accept(serverFD, (struct sockaddr *) &clientAddr, &clientLength)) < 0)
			printf("Error in accepting client!\n");
		int nBytes = read(notifFD[notifFDIdx], notifBuf, BUF_LEN);
		if(nBytes < 0)
		{
			printf("Error reading from socket!\n");
			return NULL;
		}
		notifBuf[nBytes]='\0';
		printf("Notification received:%s\n", notifBuf);
		pthread_mutex_lock(&mutex);
		balanceLoad = true;
		notifFDIdx++;
		notifFDIdx = notifFDIdx % MAX_SERVERS;
		pthread_mutex_unlock(&mutex);
	
		while(numClientsRemovedFromLoad<numClients/2);
		int notifLength = 0, pos=0;
		for(pos=0;notifBuf[pos]!=' ';pos++)
			notifLength = notifLength*10+(notifBuf[pos]-'0');
		pos++;
		// printf("Message length = %d, pos = %d\n", notifLength, pos);
		char msg[2][BUF_LEN];
		memset(msg[0], 0, BUF_LEN);
		memset(msg[1], 0, BUF_LEN);
		int idx=0, j=0;
		for(int i=pos;i<pos+notifLength;i++)
		{
			msg[idx][j++]=notifBuf[i];
			if(notifBuf[i]==' ')
			{
				msg[idx][j]='\0';
				idx++;
				j=0;
			}
		}
		msg[idx][j]='\0';
		printf("%s\t%s\n", msg[0], msg[1]);
		balanceLoad = false;
		gettimeofday(&t1, 0);
		long elapsed = (t1.tv_sec-t0.tv_sec)*MEGA_POW + t1.tv_usec-t0.tv_usec;
		printf("Trying to connect to the second server...\n");
		sleep(REASONABLE_TIME_GAP);
		for(int i=1;i<numClients;i+=2)
		{
			memset(cliReq[i].serverName, 0, MAX_SERVER_NAME_LENGTH);
			strncpy(cliReq[i].serverName, msg[1], strlen(msg[1]));
			// cliReq[i].loadTestDuration -= elapsed;
			pthread_create(&clientThreadPool[i], NULL, clientThreadInit, (void *)&cliReq[i]);
		}
		while(!forceQuit && !timeout);
		printf("Closing server related FDs\n");
		close(serverFD);
		for(int i=0;i<notifFDIdx;i++)
			close(notifFD[i]);
	}

	return nullptr;	
}


int main(int argc, char *argv[])
{
	cmdLineArgsValidityCheck(argc, argv);
	int threadIDArr[MAX_CLIENTS];
	numClients = atoi(argv[3]);
	memset(nwIface, 0, MAX_CMD_LEN);
	strncpy(nwIface, argv[1], strlen(argv[1]));
	gKey=0;
	timeout=false;
	signal(SIGINT, signalHandler);

	pthread_create(&notificationThread, NULL, handleNotification, NULL);
	for(int i=0;i<numClients;i++)
	{
		memset(cliReq[i].serverName, 0, MAX_SERVER_NAME_LENGTH);
		strncpy(cliReq[i].serverName, argv[2], strlen(argv[2]));
		cliReq[i].loadTestDuration = atoi(argv[4]);
		cliReq[i].threadID = i;
		numRequests[i]=0;
		responseTime[i] = 0.0;
		pthread_create(&clientThreadPool[i], NULL, clientThreadInit, (void *)&cliReq[i]);
	}
	
	int loadTestDuration = atoi(argv[4]);
	gettimeofday(&t0, 0);
	struct timespec delay;    
    delay.tv_sec = loadTestDuration;
    delay.tv_nsec = 0; 
    nanosleep(&delay, NULL); // sleep for timer specified

    timeout=true; // set global timeout as true

    /*delay.tv_sec = 0;
    delay.tv_nsec = 500000000;
    nanosleep(&delay, NULL); */
    sleep(2);
    // now sleep for extra 2s , so that all threads can disconnect in the meantime

	printf("****************************\n");
	long sum = 0;
	double totalResponseTime = 0.0;
	for(int i=0;i<numClients;i++)
	{
		sum = (long)sum + numRequests[i];
		totalResponseTime = (double)totalResponseTime + responseTime[i];
	}
	printf("Number of clients = %d\n", numClients);
	printf("Load test duration (sec) = %d\n", loadTestDuration);
	printf("Throughput (reqs/sec) = %lf\n", sum*1.0/(loadTestDuration));
	printf("Average Response Time per request (sec) = %lf\n", totalResponseTime/(sum*1.0));
	printf("****************************\n");
	//pthread_join(clientThreadPool[0], NULL);
}

void *clientThreadInit(void *args)
{
	clientRequirements *cliReq = (clientRequirements *)args;
	bool isConnected=false;
	char cmdSeq[MAX_INPUT_SIZE+1];
	char value[MAX_VALUE_SIZE+1];
	printf("Thread ID = %d %s %d\n", cliReq->threadID, cliReq->serverName, cliReq->loadTestDuration);

	for(int i=0;i<MAX_VALUE_SIZE;i++)
		value[i] = alpha[rand()%NUM_ALPHABETS];
	value[MAX_VALUE_SIZE]='\0';
	
	while(1)
	{
		while(!isConnected)
		{
			isConnected = connectToServer(cliReq, PORT_NUM);
			printf("Thread %d : isConnected = %d\n", cliReq->threadID, isConnected);
		}

		// timeout, SIGINT or time to balance load. Now disconnect
		if(timeout || forceQuit || (balanceLoad && (cliReq->threadID & 1)))
		{
			if(balanceLoad)
			{
				// printf("Time to balance load by removing thread id %d\n", cliReq->threadID);
				numClientsRemovedFromLoad++;
			}
			communicateWithServer(cliReq, cmdList[NUM_CMDS], cmdSeq);
			break;
		}


		int cmdIndex = 0;
		for(int i=0;i<100 && !forceQuit;i++)
		{
			constructMessage(cmdIndex, cmdSeq, value);
			communicateWithServer(cliReq, cmdList[cmdIndex], cmdSeq);
			numRequests[cliReq->threadID]++;		
		}
		cmdIndex = 1;
		for(int i=0;i<100 && !forceQuit;i++)
		{
			constructMessage(cmdIndex, cmdSeq, value);
			communicateWithServer(cliReq, cmdList[cmdIndex], cmdSeq);
			numRequests[cliReq->threadID]++;		
		}
		cmdIndex = 2;
		for(int i=0;i<100 && !forceQuit;i++)
		{
			constructMessage(cmdIndex, cmdSeq, value);
			communicateWithServer(cliReq, cmdList[cmdIndex], cmdSeq);
			numRequests[cliReq->threadID]++;		
		}
		cmdIndex = 3;
		for(int i=0;i<100 && !forceQuit;i++)
		{
			constructMessage(cmdIndex, cmdSeq, value);
			communicateWithServer(cliReq, cmdList[cmdIndex], cmdSeq);
			numRequests[cliReq->threadID]++;		
		}
		/*struct timespec delay;    
        delay.tv_sec = 0;
        delay.tv_nsec = 100000000;
        nanosleep(&delay, NULL);*/
	}
	printf("Thread id %d done.\n", cliReq->threadID);
	return nullptr;	
}

int constructMessage(int cmdIndex, char cmdSeq[], char value[])
{
	//int cmdIndex = rand()%NUM_CMDS;
	memset(cmdSeq, 0, MAX_INPUT_SIZE);
	strcpy(cmdSeq, cmdList[cmdIndex]);
	char keyStr[5], valueLenStr[5];
	
	if (cmdIndex == 1 || cmdIndex == 3) // read OR delete
	{
		//srand(3);
		int key = rand()%NUM_UNIQUE_KEYS + 1;
		sprintf(keyStr, "%d", key);
		keyStr[strlen(keyStr)]='\0';
		strcat(strcat(cmdSeq," "), keyStr);
		cmdSeq[strlen(cmdSeq)]='\0';
		return cmdIndex;
	}
	else // create OR update
	{
		int key = gKey+1;//rand()%NUM_UNIQUE_KEYS + 1;
		gKey = (gKey+1)%NUM_UNIQUE_KEYS;
		sprintf(keyStr, "%d", key);
		keyStr[strlen(keyStr)]='\0';
		strcat(strcat(cmdSeq," "), keyStr);
		int valueLength = MAX_VALUE_SIZE; //rand()%MAX_VALUE_SIZE + 1;
		sprintf(valueLenStr, "%d", valueLength);
		valueLenStr[strlen(valueLenStr)]='\0';
		strcat(strcat(cmdSeq," "), valueLenStr);
		strcat(strcat(cmdSeq," "), value);
		cmdSeq[strlen(cmdSeq)]='\0';
		return cmdIndex;
	}
	return -1;
}

void cmdLineArgsValidityCheck(int argc, char *argv[])
{
	if(argc!=5)
	{
		printf("Usage : ./<executable> <nw_iface> <server_ip> <num_threads> <load_test_duration>\n");
		exit(0);		
	}
	/*else if(argc==2)
	{
		if(strcmp(argv[1], "interactive"))
		{
			printf("Usage : ./<executable> <interactive/batch> <filename, if batch>\n");
			exit(0);
		}	
	}
	else
	{
		printf("Usage : ./<executable> <interactive/batch> <filename, if batch>\n");
		exit(0);
	}*/
}

bool connectToServer(clientRequirements *cliReq, int serverPort)
{
	/*if(connected())
	{
		printf("Already connected to server!\n");
		return;
	}*/
	char *serverName = cliReq->serverName;
	if((clientFD[cliReq->threadID] = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("[connToServer] Error opening socket. Terminating client!\n");
		return false;
	}
	server = gethostbyname(serverName);
	if(!server)
	{
		printf("Error! No such host to connect to!\n");
		return false;
	}
	//port=atoi(serverPort);
	port = serverPort;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	memcpy((char *)&serverAddr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
	serverAddr.sin_port = htons(port);
	if(connect(clientFD[cliReq->threadID], (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
	{
		printf("Cannot connect client to server: %s %d\n", strerror(errno), errno);
		return false;
	}
	memset(buf[cliReq->threadID], 0, BUF_LEN);
	if(read(clientFD[cliReq->threadID], buf[cliReq->threadID], BUF_LEN) < 0)
		printf("Error reading from socket!\n");
	//printf("thread %d has returned from connect() & read()\n", cliReq->threadID);
	if(!strcmp(STATUS_OK, buf[cliReq->threadID]))
	{
		printf("connect OK %d to server %s\n", cliReq->threadID, cliReq->serverName);
		//isConnected=true;
	}
	else if(!strcmp(STATUS_SERVER_BUSY, buf[cliReq->threadID]))
	{
		printf("Server busy. Please try again later.\n");
		close(clientFD[cliReq->threadID]);
		printf("Client [%d] disconnected!\n", cliReq->threadID);
		return false;
	}
	return true;
}

void communicateWithServer(clientRequirements *cliReq, const char *token, char cmdSeq[])
{
	int expectedLen = strlen(cmdSeq);
	// printf("Expected length = %d\n", expectedLen);
	/*if(!strcmp(token, "connect"))
	{
		char **tmpTokens = tokenize(cmdSeq);
		connectToServer(tmpTokens[1], tmpTokens[2]);
		return;
	}*/

	/*if(!connected())
	{
		printf("Client is not connected to any server!\n");
		return;
	}*/
	if(!strcmp(token, "disconnect"))
	{
		printf("disconnecting...\n");
		strcpy(cmdSeq, STATUS_END_CONNECTION);
		cmdSeq[STATUSMSG_LEN-1]='\0';
	}

	char cmdSeqWithLen[MAX_INPUT_SIZE+1];
	sprintf(cmdSeqWithLen, "%d", expectedLen);
	expectedLen=expectedLen+1+strlen(cmdSeqWithLen);
	strcat(strcat(cmdSeqWithLen, " "), cmdSeq);
	int bytesWritten=0;
	while(bytesWritten<expectedLen)
	{
		int nBytes = write(clientFD[cliReq->threadID], (cmdSeqWithLen+bytesWritten), expectedLen-bytesWritten);
		if (nBytes < 0)
		{
			printf("Error writing into socket!\n");
			return;
		}
		bytesWritten+=nBytes;
	}
	
	timeval startTime;
	timeval endTime;
	gettimeofday(&startTime, NULL);
    if(!strcmp(token, "create"))
	{
		createKVPair(cliReq);
	}
	else if(!strcmp(token, "read"))
	{
		readKVPair(cliReq);
	}
	else if(!strcmp(token, "update"))
	{
		updateKVPair(cliReq);
		/*if(!updateKVPair())
			printf("Could not update key!\n");*/
	}
	else if(!strcmp(token, "delete"))
	{
		deleteKVPair(cliReq);
		/*if(!deleteKVPair())
			printf("Could not delete key!\n");*/
	}
	else if(!strcmp(token, "disconnect"))
	{
		close(clientFD[cliReq->threadID]);
		printf("Client [%d] disconnected from server %s!\n", cliReq->threadID, cliReq->serverName);
	}
	gettimeofday(&endTime, NULL);
	double st = startTime.tv_usec + (unsigned long long)startTime.tv_sec * MEGA_POW;
	double et = endTime.tv_usec + (unsigned long long)endTime.tv_sec * MEGA_POW;
	
	responseTime[cliReq->threadID] = responseTime[cliReq->threadID] + (et-st)/(MEGA_POW*1.0);
}

void createKVPair(clientRequirements *cliReq)
{
	//printf("creating KV pair\n");
	memset(buf[cliReq->threadID], 0, BUF_LEN);
	if(read(clientFD[cliReq->threadID], buf[cliReq->threadID], BUF_LEN) < 0)
		printf("Error reading from socket!\n");
	/*if(!strcmp(STATUS_KEY_ALREADY_EXISTS, buf[cliReq->threadID]))
	{
		printf("keys exists\n");
	}
	else if(!strcmp(STATUS_OK, buf[cliReq->threadID]))
	{
		printf("OK\n");
	}*/
}

void readKVPair(clientRequirements *cliReq)
{
	int valueLength = 0;
	//printf("value len = %d\n", valueLength);
	char *value = NULL;//(char *) malloc(valueLength+1);
	//memset(value, 0, valueLength+1);
	int bytesRead = 0, index=0;
	int curLen=0;
	while(bytesRead < valueLength+index+1)
	{
		
		memset(buf[cliReq->threadID], 0, BUF_LEN);
		int nBytes = read(clientFD[cliReq->threadID], buf[cliReq->threadID], BUF_LEN);
		if(nBytes < 0)
		{
			printf("Error reading from socket!\n");
			return;
		}
		buf[cliReq->threadID][nBytes]='\0';
		bytesRead += nBytes;
		if(valueLength==0)
		{
			if(!strcmp(STATUS_KEY_NOT_FOUND, buf[cliReq->threadID]))
			{
				//printf("no key\n");
				return;
			}
			index=0;
			while(buf[cliReq->threadID][index]!=' ' && index<nBytes)
				index++;
			if(index>0)
			{
				char *valueSize = (char *)malloc(index+1);
				memcpy(valueSize, &buf[cliReq->threadID], index);
				valueSize[index]='\0';
				valueLength = atoi(valueSize);
				value = (char *) malloc(valueLength+1);
				memcpy(value, &buf[cliReq->threadID][index+1], nBytes-(index+1));
				value[nBytes-(index+1)]='\0';
				curLen=nBytes-(index+1);
			}
		}
		else
		{
			curLen+=nBytes;
			strcat(value, buf[cliReq->threadID]);
			value[curLen]='\0';
		}
		//printf("tmp val = #####%s##### %d\n", value, curLen);
	}
	/*if(value==NULL)
	{
		printf("value NULL, but how?\n");
		sleep(100);
		return;
	}*/
	value[valueLength]='\0';
	//printf("Value : %s\n", value);
}

bool updateKVPair(clientRequirements *cliReq)
{	
	bool retVal=true;
	memset(buf[cliReq->threadID], 0, BUF_LEN);
	if(read(clientFD[cliReq->threadID], buf[cliReq->threadID], BUF_LEN) < 0)
		printf("Error reading from socket!\n");
	if(!strcmp(STATUS_KEY_NOT_FOUND, buf[cliReq->threadID]))
	{
		//printf("no key\n");
		retVal=false;
	}
	/*else if(!strcmp(STATUS_OK, buf[cliReq->threadID]))
	{
		printf("Key updated!\n");
	}*/
	return retVal;
}

bool deleteKVPair(clientRequirements *cliReq)
{
	bool retVal=true;
	memset(buf[cliReq->threadID], 0, BUF_LEN);
	if(read(clientFD[cliReq->threadID], buf[cliReq->threadID], BUF_LEN) < 0)
		printf("Error reading from socket!\n");
	if(!strcmp(STATUS_KEY_NOT_FOUND, buf[cliReq->threadID]))
	{
		//printf("no key\n");
		retVal=false;
	}
	/*else if(!strcmp(STATUS_OK, buf[cliReq->threadID]))
	{
		printf("Key deleted successfully!\n");
	}*/
	return retVal;
}

void disconnectFromServer(clientRequirements *cliReq)
{
	//isConnected=false;
	close(clientFD[cliReq->threadID]);
	printf("Client [%d] disconnected!\n", cliReq->threadID);
}