/**
 *  KEY-VALUE SERVER -- CS-744 ASSIGNMENT 4
 *  AUTHOR : DIPTYAROOP MAJI
 *  ROLL : 183050016
*/


/*
FUTURE WORK : handle sigpipe (if client presses ctrl+c
			  update -> delete() followed by create()
*/

/*
* #####
* Server code used in VMs. Bugs present, but too lazy to fix all of them
* #####
*/

#include "headers.h"

using namespace std;

#define MAX_CLIENTS 100
#define BUF_LEN 256
#define MAX_INPUT_SIZE 4197
#define MAX_NUM_TOKENS 4

pthread_t clientThreadPool[MAX_CLIENTS];
//int fdIndex;
int numClients;
map<int, char *> keyValMap;
map<int, char *> keyValLenMap;
// 1 extra for surplus clients
int clientFDPool[MAX_CLIENTS+1]; // later, try to do in an elegant manner --> using free list
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mapMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t newClientCV[MAX_CLIENTS];
bool threadIdle[MAX_CLIENTS];

bool createKVPair(int, char **);
void readKVPair(int, char *);
bool updateKVPair(int , char **);
bool deleteKVPair(int, char *);
void parseCommand(int , char *);
void *communicateWithClient(void *);
int findFreeFD();

inline int findFreeFD()
{
	for(int i=0;i<MAX_CLIENTS;i++)
	{
		if(clientFDPool[i]==-1)
			return i;
	}
	return MAX_CLIENTS; // FDpool full
}

int main(int argc, char *argv[])
{
	int serverFD, port;
	struct sockaddr_in serverAddr, clientAddr;
	socklen_t clientLength;
	int threadIDArr[MAX_CLIENTS];

	if(argc!=2)
	{
		printf("Usage : ./<executable> <IP address>\n");
		exit(1);
	}

	if((serverFD = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("Error opening socket. Terminating!\n");
		exit(1);
	}

	keyValMap.clear();
	keyValLenMap.clear();
	for(int i=0;i<MAX_CLIENTS;i++)
	{
		newClientCV[i] = PTHREAD_COND_INITIALIZER;
		clientFDPool[i]=-1;
		threadIdle[i]=false;
	}
	memset(&serverAddr, 0, sizeof(serverAddr));
	port = 8080;
	serverAddr.sin_family = AF_INET;
	inet_pton(AF_INET, argv[1], &(serverAddr.sin_addr));
	serverAddr.sin_port = htons(port);

	if(bind(serverFD, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0)
	{
		printf("Error in binding socket!\n");
		exit(1);
	}
	printf("Key-value server started...\n[MAX NO. OF CONCURRENT CLIENTS ALLOWED = %d]\n\n", MAX_CLIENTS);
	listen(serverFD, MAX_CLIENTS);

	for(int i=0;i<MAX_CLIENTS;i++)
	{
		threadIDArr[i]=i+1;
		pthread_create(&clientThreadPool[i], NULL, communicateWithClient, (void *)&threadIDArr[i]);
	}
	numClients=0;
	int fdIndex = 0;
	while(1)
	{
		//int fdIndex = 0;//findFreeFD();
		clientLength = sizeof(clientAddr);
		if((clientFDPool[fdIndex] = accept(serverFD, (struct sockaddr *) &clientAddr, &clientLength)) < 0)
			printf("Error in accepting client!\n");
		/*int doubleCheckFDIndex = findFreeFD();
		if(fdIndex == MAX_CLIENTS) // no free threads/FDs available
		{
			if(doubleCheckFDIndex < MAX_CLIENTS)
			{
				clientFDPool[doubleCheckFDIndex] = dup(clientFDPool[fdIndex]);
				clientFDPool[fdIndex]=-1;
				fdIndex = doubleCheckFDIndex;
			}
			else // surplus clients
			{
				printf("Server busy!\n");
				if(write(clientFDPool[fdIndex], STATUS_SERVER_BUSY, STATUSMSG_LEN) < 0)
					printf("Error writing into socket\n");
				close(clientFDPool[fdIndex]); // closing socket connection with surplus client
				clientFDPool[fdIndex]=-1;
				continue;
			}
		}*/
		//printf("Client accepted.\n");
		pthread_mutex_lock(&mutex);
		threadIdle[fdIndex]=true;
		pthread_cond_signal(&newClientCV[fdIndex]);
		//printf("signalling thread %d\n", fdIndex+1);
		fdIndex++;
		fdIndex = fdIndex % MAX_CLIENTS;
		pthread_mutex_unlock(&mutex);
	}
	/*for(int i=0;i<MAX_CLIENTS;i++)
	{
		pthread_join(clientThreadPool[i], NULL);
		
		printf("Thread %d terminated !!!\n", i+1);
	}*/
	return 0;
}

void *communicateWithClient(void *args)
{
	int threadID = *((int *) args);
	int fdIndex;

	char cmdSeq[MAX_INPUT_SIZE+1];	
	char buf[BUF_LEN+1];
	while(1)
	{
		pthread_mutex_lock(&mutex);
		while(!threadIdle[threadID-1])
		{
			//printf("thread %d waiting\n", threadID);
			pthread_cond_wait(&newClientCV[threadID-1], &mutex);
		}
		//threadIdle=false;
		printf("Thread %d up\n", threadID);
		fdIndex = threadID-1;
		pthread_mutex_unlock(&mutex);

		

		if(write(clientFDPool[fdIndex], STATUS_OK, STATUSMSG_LEN) < 0)
			printf("Error writing into socket\n");
		do
		{
			memset(cmdSeq, 0, MAX_INPUT_SIZE+1);
			int cmdSeqLen=0;
			int index=0;
			bool cmdLenFound=false;
			int bytesRead=0;
			while(bytesRead < cmdSeqLen+index+1)
			{
				memset(buf, 0, BUF_LEN);
				int nBytes = read(clientFDPool[fdIndex], buf, BUF_LEN);
				if(nBytes < 0)
				{
					printf("Error reading from socket!\n");
					return NULL;
				}
				buf[nBytes]='\0';
				bytesRead += nBytes;
				if(!cmdLenFound)
				{
					index=0;
					while(buf[index]!=' ' && index<nBytes)
						index++;
					if(index>0)
					{
						char *cmdSize = (char *)malloc(index+1);
						memset(cmdSize, 0, index+1);
						memcpy(cmdSize, &buf, index);
						cmdSize[index]='\0';
						cmdSeqLen = atoi(cmdSize);
						cmdLenFound=true;
						memcpy(cmdSeq, &buf[index+1], nBytes-(index+1));
						cmdSeq[nBytes-(index+1)]='\0';
						free(cmdSize);
					}
				}
				else
				{
					strcat(cmdSeq, buf);
				}
			}
			cmdSeq[cmdSeqLen]='\0';
			//printf("Client [%d] msg : %s\tmsg_length = %d\n", threadID, cmdSeq, (int)strlen(cmdSeq));
			parseCommand(fdIndex, cmdSeq);
		} while(strcmp(cmdSeq, STATUS_END_CONNECTION));
		printf("client [%d] disconnected\n", threadID);
		// pthread_mutex_lock(&mutex);
		// close(clientFDPool[fdIndex]);
		// clientFDPool[fdIndex]=-1;
		// threadIdle[fdIndex]=false;
		// pthread_mutex_unlock(&mutex);
		break;
	}
}

char **tokenizeString(char **tokens, char *line)
{
    char *token = (char *)malloc(MAX_TOKEN_SIZE * sizeof(char));
    int i, tokenIndex = 0, tokenNo = 0, numPossibleTokens=MAX_NUM_TOKENS;
    int firstCmdFlag=0;

    //printf("%s ** %d\n", line, strlen(line));
    for(i =0; i <= strlen(line); i++){

        char readChar = line[i];
        if ((readChar == ' ' && tokenNo<numPossibleTokens) || readChar == '\0' || readChar == '\t' || readChar == '\n'){
            token[tokenIndex] = '\0';
            //printf("token = %s\n", token);
            if (tokenIndex != 0){
                tokens[tokenNo] = (char*)malloc(MAX_TOKEN_SIZE*sizeof(char));
                strcpy(tokens[tokenNo++], token);
                if(!firstCmdFlag)
                {
                    if((!strcmp(token, "create")) || (!strcmp(token, "update")))
                        numPossibleTokens = 3;
                    firstCmdFlag=1;
                }     
               //printf("**%s**\n", tokens[tokenNo-1]);
               tokenIndex = 0; 
            }
        } else {
            token[tokenIndex++] = readChar;
        }
    }
 
    free(token);
    tokens[tokenNo] = NULL ;
    //return tokens;
}


void parseCommand(int fdIndex, char *cmdSeq)
{
	if(!strcmp(cmdSeq, STATUS_END_CONNECTION))
	{
		numClients--;
	}
	else
	{
		char **tokens = (char **)malloc(MAX_NUM_TOKENS * sizeof(char *));
		tokenizeString(tokens, cmdSeq); // MEMORY LEAK. SOLUTION FOUND ???
		
		int numTokens=0;
		
		if(!strcmp(tokens[0], "create"))
		{
			if(createKVPair(fdIndex, tokens))
			{
				//printf("create key success!\n");
				if(write(clientFDPool[fdIndex], STATUS_OK, STATUSMSG_LEN) < 0)
					printf("Error writing into socket\n");
			}
			else
			{
				//printf("create key failure!\n");
				if(write(clientFDPool[fdIndex], STATUS_KEY_ALREADY_EXISTS, STATUSMSG_LEN) < 0)
					printf("Error writing into socket\n");
			}
		}
		else if(!strcmp(tokens[0], "read"))
		{
			readKVPair(fdIndex, tokens[1]);
		}
		else if(!strcmp(tokens[0], "update"))
		{
			if(updateKVPair(fdIndex, tokens))
			{
				//printf("update key success!\n");
				if(write(clientFDPool[fdIndex], STATUS_OK, STATUSMSG_LEN) < 0)
					printf("Error writing into socket\n");
			}
			else
			{
				//printf("update key failure!\n");
				if(write(clientFDPool[fdIndex], STATUS_KEY_NOT_FOUND, STATUSMSG_LEN) < 0)
					printf("Error writing into socket\n");
			}
		}
		else if(!strcmp(tokens[0], "delete"))
		{
			if(deleteKVPair(fdIndex, tokens[1]))
			{
				//printf("delete key success!\n");
				if(write(clientFDPool[fdIndex], STATUS_OK, STATUSMSG_LEN) < 0)
					printf("Error writing into socket\n");
			}
			else
			{
				//printf("delete key failure!\n");
				if(write(clientFDPool[fdIndex], STATUS_KEY_NOT_FOUND, STATUSMSG_LEN) < 0)
					printf("Error writing into socket\n");
			}
		}
		
		for(;tokens[numTokens];numTokens++);
		for(int i=numTokens-1;i>=0;i--)
		{
			if(tokens && tokens[i])
				free(tokens[i]);
		}
		if(tokens)
			free(tokens);
	}
}

bool createKVPair(int fdIndex, char **tokens)
{
	int key = atoi(tokens[1]);
	pthread_mutex_lock(&mapMutex);
	if(keyValMap.find(key)!=keyValMap.end())
	{
		pthread_mutex_unlock(&mapMutex);
		return false;
	}
	int valueLength = atoi(tokens[2]);
	//printf("%d %s %d *%s*\n", key, valueSize, valueLength, tokens[3]);
	char *value = (char *) malloc(valueLength+1); // WHAT TO DO ABOUT THIS ???
	memset(value, 0, valueLength+1);
	int index=3;
	while(tokens[index])
	{
		strcat(value, tokens[index]);
		index++;
	}
	keyValMap[key] = (char *) malloc(valueLength+1);
	keyValLenMap[key] = (char *) malloc(strlen(tokens[2])+1);
	strcpy(keyValMap[key], value);
	strcpy(keyValLenMap[key], tokens[2]);
	free(value);
	//keyValMap.insert(make_pair(key, value));
	//keyValLenMap.insert(make_pair(key, valueSize));
	pthread_mutex_unlock(&mapMutex);
	return true;
}

void readKVPair(int fdIndex, char *tmpBuf)
{
	int key = atoi(tmpBuf);
	pthread_mutex_lock(&mapMutex);
	map<int, char *>::iterator iter = keyValMap.find(key);	
	if(iter == keyValMap.end())
	{
		//printf("read key failure!\n");
		pthread_mutex_unlock(&mapMutex);
		int nBytes=0;
		do
		{
			nBytes = write(clientFDPool[fdIndex], STATUS_KEY_NOT_FOUND, STATUSMSG_LEN);
			if(nBytes<0)
			{
				printf("Error writing into socket\n");
				break;
			}
		} while(nBytes<STATUSMSG_LEN);
		return;
	}
	map<int, char *>::iterator iterLen = keyValLenMap.find(key);
	char *value = iter->second;
	char *valueSize = iterLen->second;
	int valueLength = atoi(valueSize);
	char valueWithLen[valueLength+strlen(valueSize)+2];
	strcpy(valueWithLen, valueSize);
	valueLength=valueLength+1+strlen(valueSize);
	strcat(strcat(valueWithLen, " "), value);
	int bytesWritten=0;
	while(bytesWritten<valueLength)
	{
		int nBytes = write(clientFDPool[fdIndex], valueWithLen+bytesWritten, valueLength-bytesWritten);
		if (nBytes < 0)
		{
			printf("Error writing into socket!\n");
			pthread_mutex_unlock(&mapMutex);
			return;
		}
		bytesWritten+=nBytes;
		//printf("%d**%d\n", bytesWritten, valueLength);
	}
	pthread_mutex_unlock(&mapMutex);
	//printf("read key success!\n");
}

bool updateKVPair(int fdIndex, char **tokens)
{
	int key = atoi(tokens[1]);
	pthread_mutex_lock(&mapMutex);
	map<int, char *>::iterator iter = keyValMap.find(key);	
	if(iter==keyValMap.end())
	{
		pthread_mutex_unlock(&mapMutex);
		return false;
	}
	map<int, char *>::iterator iterLen = keyValLenMap.find(key);
	char *valueSize = tokens[2];
	int valueLength = atoi(tokens[2]);
	char *value = (char *) malloc(valueLength+1);
	memset(value, 0, valueLength+1);
	int index=3;
	while(tokens[index])
	{
		strcat(value, tokens[index]);
		index++;
	}
	if(iter->second)
		free(iter->second);
	iter->second = (char *)malloc(valueLength+1);
	strcpy(iter->second, value);
	/*if(iterLen->second)
		free(iterLen->second);*/
	iterLen->second = (char *)malloc(strlen(valueSize+1));
	strcpy(iterLen->second, valueSize);
	free(value);
	pthread_mutex_unlock(&mapMutex);
	return true;
}

bool deleteKVPair(int fdIndex, char *tmpBuf)
{
	int key = atoi(tmpBuf);
	pthread_mutex_lock(&mapMutex);
	if(keyValMap.find(key)==keyValMap.end())
	{
		pthread_mutex_unlock(&mapMutex);
		return false;
	}
	keyValMap.erase(key);
	keyValLenMap.erase(key);
	pthread_mutex_unlock(&mapMutex);
	return true;
}