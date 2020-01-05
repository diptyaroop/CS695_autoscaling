#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<netinet/in.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<unistd.h>
#include<map>
#include<utility>
#include<vector>
#include<arpa/inet.h>
#include<pthread.h>
#include<time.h>
#include<sys/time.h>
#include<signal.h>
#include <errno.h> 
#include <net/if.h>
#include <sys/ioctl.h>
#include "make-tokens.c"

/* STATUS CODE : 
100 : OK
101 : KEY ALREADY EXISTS
102 : KEY NOT FOUND
103 : SERVER NOT FOUND
104 : SERVER BUSY
105 : STATUS_END_CONNECTION
106 : MISC (others) */

#define STATUS_OK "[100]"
#define STATUS_KEY_ALREADY_EXISTS "[101]"
#define STATUS_KEY_NOT_FOUND "[102]"
#define STATUS_SERVER_NOT_FOUND "[103]"
#define STATUS_SERVER_BUSY "[104]"
#define STATUS_END_CONNECTION "[105]"
#define STATUS_MISC "[106]"
#define STATUS_SERVER_OVERLOAD "[107]"
#define STATUSMSG_LEN 6