//in this utility.h header file make declaration for functions like msg_send and msg_recv and define these in utility.c
# include "utility.h" 

struct sockaddr_un cliaddr;
void SIGINTHandler(int sigNum);

int main( int argc, char **argv)
{
    int sockfd, retval;
    char serverName[256], serverIPAddr[16], traceMsg[APPODRMSGSIZE]; 
    struct hostent *hptr;
    fd_set rset;
    struct timeval tv;
    int k, srcPort;
    char recvdMsg[APPODRMSGSIZE], srcAddr[16], localVMName[256], serverPort[256];
 
    sockfd = Socket(AF_LOCAL, SOCK_DGRAM, 0);

    memset( &cliaddr, 0, sizeof(cliaddr)); 
    cliaddr.sun_family = AF_LOCAL;  
    strcpy(cliaddr.sun_path, "/tmp/alok.XXXXXX"); 

    if (mkstemp(cliaddr.sun_path) < 0) {
       printf("Unable to create temporary file for binding the client socket. Exiting ......\n");  
       exit(0); 
    }  
    //just in case the path was already in use unlink it to make this path available for bindings
    unlink(cliaddr.sun_path);

    //bind the socket created to the "well known port"(path)
    retval = bind(sockfd, (SA *)&cliaddr, sizeof(cliaddr));
    //in case the bindning fails unlink the file created befor exiting so that the temporary files do not proliferate
    if (retval < 0) {
       printf("Binding failed. Hence exiting the client\n");  
       printf("Before exiting, removing the temporary file created by unlinking it\n");
       unlink(cliaddr.sun_path);
       exit(1);
    }
    // Register signal to handle cleanup upon termination
    signal(SIGINT, SIGINTHandler);

    //get the name of the local VM
    gethostname(localVMName, sizeof(localVMName));

    for ( ; ; ) {
       serverName[0] = '\0';
       printf("Enter the name of the server(VM1 to VM10) or 'quit' to exit.\n");
       if (fgets(serverName, sizeof(serverName), stdin) == NULL) {
          printf("fgets returned null\n");
          continue;
       }
       if (serverName[strlen(serverName) - 1] == '\n') {
          serverName[strlen(serverName) - 1] = '\0';
       }

       if(!strcmp(serverName, "quit")) {
          unlink(cliaddr.sun_path);
          printf("Unlinking client temp file %s\n", cliaddr.sun_path);
          printf("Quitting.\n");
          exit(1);
       }

       if ((hptr = gethostbyname(serverName)) == NULL) {
           printf("Error in resolving the IP for the given servername. Please reenter your choice\n"); 
           continue;          
       }

       //fetch the canonical address i.e. address of the ethernet zero so that trace message can be sent 
       Inet_ntop(hptr->h_addrtype, hptr->h_addr_list[0], serverIPAddr, sizeof(serverIPAddr)); 
       //fill in the destination address structure and pass it to msg_send
       strcpy(traceMsg, "Seek Time");
       printf("Client at node %s sending request to server at %s\n", localVMName, serverName); 
       msg_send(sockfd, serverIPAddr, SERVERPORT,  traceMsg, 0);
       FD_ZERO(&rset);
       FD_SET(sockfd, &rset); 
       tv.tv_sec = 5;
       tv.tv_usec = 0 ; 
       k = Select( sockfd + 1, &rset, NULL, NULL, &tv);   
       if (k > 0) { 
          msg_recv(sockfd, recvdMsg, srcAddr, &srcPort);
          printf("Client at node %s received from %s %s", localVMName, serverName, recvdMsg); 
       } else if( k == 0) {
          printf("Client at node %s : timeout on response from %s\n", localVMName, serverName); 
          printf("Client %s resending request to server %s. Beginning forced rediscovery.\n", localVMName, serverName); 
          //resend the request
          msg_send(sockfd, serverIPAddr, SERVERPORT,  traceMsg, 1);
          //again block in select to receive response to the retransmitted request
          FD_ZERO(&rset);
          FD_SET(sockfd, &rset);
          tv.tv_sec = 5;
          tv.tv_usec = 0; 
          k = Select(sockfd + 1, &rset, NULL,NULL, &tv);
          if (k > 0) {
             msg_recv(sockfd, recvdMsg, srcAddr, &srcPort);
             printf("Client at node %s received from %s %s", localVMName, serverName, recvdMsg); 
          } else if( k == 0) {
             printf("Client at node %s : timeout on response from %s\n", localVMName, serverName);
             printf("Client %s timed out 2nd time waiting for response.\n", localVMName);
          } else {
             printf("Error in select. Exiting the client.\n");
             unlink(cliaddr.sun_path);
             exit(1);
          }
       } else { 
          printf("Error in Select. Exiting the client\n");
          unlink(cliaddr.sun_path);
          exit(1); 
       }
    }
}

void SIGINTHandler(int sigNum)
{
   printf("Unlinking client temp file %s\n", cliaddr.sun_path);
   unlink(cliaddr.sun_path);
   // Need to exit here otherwise program does not terminate.
   exit(1);
}


