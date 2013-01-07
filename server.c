# include "utility.h"

int main( int argc, char **argv)
{
     struct sockaddr_un servaddr;
     struct hostent *hptr;
     struct in_addr senderIp;
     char serverName[50];
     char recvdMsg[APPODRMSGSIZE], srcAddr[16], sendMsg[APPODRMSGSIZE];
     int srcPort, sockfd;
     time_t ticks;
 
     sockfd = Socket(AF_LOCAL, SOCK_DGRAM, 0);
     unlink(SERVERPATH);

     memset( &servaddr, 0, sizeof(servaddr)); 
     servaddr.sun_family = AF_LOCAL;  
     strcpy(servaddr.sun_path,SERVERPATH);

     Bind(sockfd, (SA *)&servaddr, sizeof(servaddr));
     //get the host name for the server
     gethostname(serverName, sizeof(serverName));

     for( ; ;) {
         msg_recv(sockfd, recvdMsg, srcAddr, &srcPort);
         bzero(&senderIp, sizeof(senderIp));
         Inet_pton(AF_INET, srcAddr, &senderIp);
         hptr = gethostbyaddr(&senderIp, sizeof(struct in_addr), AF_INET); 
         if (hptr == NULL) {
             printf(" Could not fetch the name of the client requesting the time\n");
             continue;
         } else {
             printf("Server at node %s responding to request from %s\n", serverName, hptr->h_name);
             ticks = time(NULL);
             snprintf(sendMsg, sizeof(sendMsg), "The time on %s is %24s", serverName, ctime(&ticks));
             msg_send(sockfd, srcAddr, srcPort, sendMsg, 0);             
         }
     }
}
