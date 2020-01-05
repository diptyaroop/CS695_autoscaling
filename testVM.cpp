/* Compile using g++ testVM.cpp -o testVM -lvirt -lpthread*/

/* Comments:
* 1. vhost taking up CPU. So, util inside is 68% (as seen in htop), but in host
*       it is showing 100%
* 2. Say, the system is a KV server. Now, when server2 boots up, it won't have
*       any valid entries. How to handle that? Should server1 periodically update
*       server2?
* 3. CPU util will decrease in server1 while increase in server2. Also, earlier
*       server1 was overloaded but now load is balanced. So, throughput should increase (??)
*/

#include <bits/stdc++.h>
#include <unistd.h>
#include <pthread.h>
#include<netinet/in.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<arpa/inet.h>
#include<time.h>
#include<sys/time.h>
#include<signal.h>
#include <errno.h> 
#include <net/if.h>
#include <sys/ioctl.h>
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

using namespace std;

/* MACROS start */
#define SUCCESS 0
#define FAILURE (-1)
#define MAX_XML_DESC_SIZE 10000
#define MAX_STR_SIZE 3000
#define DOMAIN_RUNNING 1
#define DOMAIN_INACTIVE 0
#define TIME_PERIOD 5
#define THRESHOLD 80
#define DENOM 1000000
#define NANOSEC_DENOM 1000000000
#define MAX_AVAILABLE_SERVERS 2
#define NOTIF_PORT 6000
#define STATUS_SERVER_OVERLOAD "[107]"
#define SECOND_SERVER_IP "192.168.122.145"
/* MACROS end */

/* Prototypes start */
void signalHandler();
void cleanup();
int connectToHypervisor();
int lookupServerDomain(virDomainPtr *domServer, char *serverDomainName, 
        char *domainXMLDesc);
int launchDomain(virDomainPtr *domServer, char *domainXMLDesc);
void *autoScale(void *args);
unsigned long long getCurrentCPUStats();
void getAllDomainStats();
void sendNotificationToLB();
/* Prototypes end */

static int activeServerCnt = 1;
static int numCpusAllocatedToFirstServer = -1;
unsigned long long prevTime = 0;
char server1DomainName[MAX_STR_SIZE];
char server2DomainName[MAX_STR_SIZE];
char domain1XMLDesc[MAX_XML_DESC_SIZE];
char domain2XMLDesc[MAX_XML_DESC_SIZE];
virDomainPtr domServer1 = nullptr;
virDomainPtr domServer2 = nullptr;
virConnectPtr conn = nullptr;
virStoragePoolPtr strPool = nullptr;
virNodeCPUStatsPtr params = nullptr;
virDomainInfoPtr domInfo = nullptr;
string uri = "qemu:///system";
int connectionFlags = 0;
unsigned int domCreateFlags =  0;//VIR_DOMAIN_START_AUTODESTROY;
unsigned long long now1,then1;
struct timeval t1,then, now;
static char clientIPAddr[16];

void signalHandler()
{
    cleanup();
    return;
}

int connectToHypervisor()
{
    if (!(conn = virConnectOpenAuth(uri.c_str(),
                                    virConnectAuthPtrDefault,
                                    connectionFlags)))
    {
        return FAILURE;
    }
    return SUCCESS;
}

int lookupServerDomain(virDomainPtr *domServer, char *serverDomainName, 
        char *domainXMLDesc)
{
    printf("Server_domain_name = %s\n", serverDomainName);
    if(!(*domServer = virDomainLookupByName(conn, serverDomainName)))
    {
        printf("Cannot lookup server\n");
        return FAILURE;
    }
    char *tmpDomainDesc = virDomainGetXMLDesc(*domServer, 0);
    if(!tmpDomainDesc || tmpDomainDesc[0]=='\0')
    {
        printf("Unable to fetch domain desription\n");
        return FAILURE;
    }
    memset(domainXMLDesc, 0, MAX_XML_DESC_SIZE);
    strncpy(domainXMLDesc, tmpDomainDesc, strlen(tmpDomainDesc));
    return SUCCESS;
}

int launchDomain(virDomainPtr *domServer, char *domainXMLDesc)
{
    if(!(*domServer = virDomainDefineXML(conn, domainXMLDesc)))
    {
        printf("Failed to define domain\n");
        return FAILURE;
    }
    else
        printf("Domain defined.\n");
    int ret = virDomainIsActive(*domServer);
    if(ret != FAILURE)
    {
        if(ret == DOMAIN_RUNNING)
        {
            printf("Domain running already. Not launching again\n");
            return SUCCESS;
        }
    }
    else
    {
        printf("Cannot check if domain is active. Aborting operations\n");
        return FAILURE;
    }
    if(virDomainCreate(*domServer) == SUCCESS)
        printf("Domain launched successfully\n");
    else
        return FAILURE;
    return SUCCESS;
}

unsigned long long getCurrentCPUStats()
{
    int nparams = 0;
    int cpuNum = VIR_NODE_CPU_STATS_ALL_CPUS;
    // if (virNodeGetCPUStats(conn, cpuNum, NULL, &nparams, 0) == SUCCESS && 
    //         nparams != 0)
    // {
    //     printf("nparams = %d\n", nparams);
    //     if ((params = (virNodeCPUStatsPtr) malloc(sizeof(virNodeCPUStats) * nparams)) == NULL)
    //         return FAILURE;
    //     memset(params, 0, sizeof(virNodeCPUStats) * nparams);
    //     if (virNodeGetCPUStats(conn, cpuNum, params, &nparams, 0) != SUCCESS)
    //         return FAILURE;
    //     for(int i=0;i<nparams;i++)
    //     {
    //         printf("%s: %llu\n", params[i].field, params[i].value);
    //     }
    //     printf("Prev = %llu, cur = %llu\n", prevTime, params[0].value + params[1].value);
    //     printf("U+K diff = %lf\n", (params[0].value + params[1].value - prevTime)*1.0/NANOSEC_DENOM);
    //     prevTime = params[0].value + params[1].value;
    //     // printf("Total CPU time (k + u) = %llu\n", params[0].value + params[1].value/DENOM);
    // }
    // printf("\n\n");
    domInfo = (virDomainInfoPtr) malloc(sizeof(virDomainInfo));
    int ret = virDomainGetInfo(domServer1, domInfo);
    if(ret != SUCCESS)
        return (unsigned long long)FAILURE;
    printf("maxMem = %lu, memory = %lu, nrVirtCpu = %d\n", domInfo->maxMem, 
            domInfo->memory, domInfo->nrVirtCpu);
    if (numCpusAllocatedToFirstServer == -1)
        numCpusAllocatedToFirstServer = domInfo->nrVirtCpu;
    return domInfo->cpuTime;
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("Usage: sudo ./testVM <server_name_1> <server_name_2> <client_ip>\n");
        printf("Example: sudo ./testVM ubuntu18.04 server2 10.129.2.101\n");
        exit(0);
    }
    printf("Starting...\n");
    if (connectToHypervisor() == FAILURE)
    {
        printf("Failed to connect to hypervisor\n");
        exit(0);
    }
    else
        printf("Connected successfully to hypervisor\n");

    memset(server1DomainName, 0, MAX_STR_SIZE);
    strncpy(server1DomainName, argv[1], strlen(argv[1]));
    memset(server2DomainName, 0, MAX_STR_SIZE);
    strncpy(server2DomainName, argv[2], strlen(argv[2]));
    memset(clientIPAddr, 0, 16);
    strncpy(clientIPAddr, argv[3], strlen(argv[3]));
    if (lookupServerDomain(&domServer1, server1DomainName, domain1XMLDesc) == FAILURE)
    {
        printf("Domain lookup failed\n");
    }
    else
    {
        printf("Lookup successful. Domain object & desc returned.\n");
        if (launchDomain(&domServer1, domain1XMLDesc) == FAILURE)
            printf("Failed to launch domain\n");
    }
    printf("\n\nStarting to monitor server 1 (Threshold = %d %%)...\n", THRESHOLD);
    unsigned long long prevCpuTime = 0;
    bool falsePositive = true;
    unsigned long long curCpuTime = 0llu;
    double cpuUtil = 0.0;
    while(1)
    {
        printf("\n\n\n######## Getting CPU info/stats ##########\n");
        curCpuTime = getCurrentCPUStats();
        if(prevCpuTime != 0)
        {
            // printf("prevTime = %llu, curTime = %llu\n", prevCpuTime, curCpuTime);
            cpuUtil = 100.0 / TIME_PERIOD * (curCpuTime - prevCpuTime);
            cpuUtil /= NANOSEC_DENOM;
            printf("CPU utilization (%%) = %.2lf\n", cpuUtil/numCpusAllocatedToFirstServer);
        }
        prevCpuTime = curCpuTime;
        printf("#####################################\n");
        if(cpuUtil >= THRESHOLD*1.0*numCpusAllocatedToFirstServer)
        {
            if(falsePositive)
            {
                printf("CPU load above threshold\n");
                falsePositive = false;
            }
            else
            {
                printf("CPU load consistently above threshold.\n");
                if(activeServerCnt == MAX_AVAILABLE_SERVERS)
                {
                    printf("Max available servers running. No scope for further improvement.\n");
                }
                else
                {
                    printf("Autoscaling...\n");
                    pthread_t autoScaleThread;
                    int ret = 0;
                    do
                    {
                        ret = pthread_create(&autoScaleThread, NULL, autoScale, NULL);
                        if(ret)
                        {
                            printf("Error: unable to create thread %d\n", ret);
                        }
                    } while(ret);
                    activeServerCnt++;
                }
            }
        }
        else
        {
            falsePositive = true;
        }

        // getAllDomainStats();
        sleep(TIME_PERIOD);
    }
    cleanup();
    return 0;
}

void *autoScale(void *args)
{
    if (lookupServerDomain(&domServer2, server2DomainName, domain2XMLDesc) == FAILURE)
    {
        printf("Domain lookup failed\n");
    }
    else
    {
        printf("Lookup successful. Domain object & desc returned.\n");
        // printf("%s", domain2XMLDesc);
        if (launchDomain(&domServer2, domain2XMLDesc) == FAILURE)
            printf("Failed to launch domain\n");
    }
    sleep(TIME_PERIOD);
    sendNotificationToLB();
    printf("Notification message sent to client\n");
    return nullptr;
}

void sendNotificationToLB()
{
    int sock = 0, valread; 
    struct sockaddr_in serv_addr; 
    char finalMsg[MAX_STR_SIZE];
    string tmpMsg;
    memset(finalMsg, 0, MAX_STR_SIZE);
    tmpMsg = STATUS_SERVER_OVERLOAD;
    tmpMsg = tmpMsg + " " + SECOND_SERVER_IP;
    int tmpMsgLen = tmpMsg.length();
	sprintf(finalMsg, "%d", tmpMsgLen);
	// tmpMsgLen=tmpMsgLen+1+strlen(finalMsg);
	strcat(strcat(finalMsg, " "), tmpMsg.c_str());
    char buffer[1024] = {0}; 
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    { 
        printf("\n Socket creation error \n"); 
        return; 
    } 
   
    serv_addr.sin_family = AF_INET; 
    serv_addr.sin_port = htons(NOTIF_PORT); 
       
    // Convert IPv4 and IPv6 addresses from text to binary form 
    if(inet_pton(AF_INET, clientIPAddr, &serv_addr.sin_addr)<=0)  
    { 
        printf("\nInvalid address/ Address not supported \n"); 
        return; 
    } 
   
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) 
    { 
        printf("\nConnection Failed \n"); 
        return; 
    } 
    send(sock , finalMsg , strlen(finalMsg) , 0 );
}

void cleanup()
{
    printf("Inside cleanup\n");
    if(virDomainFree(domServer1) == FAILURE)
        printf("Failed to free domain (VM)\n");
    else
        printf("Freed domain (VM)\n");
    if(virConnectClose(conn) == SUCCESS)
        printf("Successfully closed connection with hypervisor\n");
    else
        printf("Failed to close connection with hypervisor\n");
}