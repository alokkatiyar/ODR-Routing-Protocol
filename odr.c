#include "utility.h"

PortPathTableElement *portPathTableHead = NULL;
PendingProcessedMsgElement *pendingMsgTableHead = NULL;
PendingProcessedMsgElement *processedMsgTableHead = NULL;
RoutingTableElement *routingTableHead = NULL;

unsigned int selfCanonicalIP;
char selfCanonicalHostName[256];

InterfaceInfo interfaceInfo[10];
int interfaceInfoCount = 0;

int domainSocket = 0;
int StaleTime = 0;
// 20 seconds for non permanent port path table entry
int PortPathStaleTime = 20;

int BroadcastId = 0;
int MonotonicPortNum = 1;

unsigned char BroadcastAddress[6] = {0xff ,0xff, 0xff, 0xff, 0xff, 0xff};

void ProcessRREQPacket(OdrPacket *rreqPacket, int arrivalInterface, struct ethhdr *ethernetHeader);
void ProcessRREPPacket(OdrPacket *rrepPacket);
void ProcessAppPacket(OdrPacket *appPacket, int forceRediscovery);

void HandleDomainSocketRequest();
void HandlePFPacketRequest(int sockfd, int arrivalInterface);

static int
GetNextBroadcastId() {
   return BroadcastId++;
}

int main(int argc, char **argv)
{
    int maxfdp1;
    struct sockaddr_un odrBindAddr;
    fd_set fdset;
    int i;
 
    if( argc != 2 )
    {
       printf("Incorrect invocation syntax. Correct Usage ./odr <staleness>\n");
       return;
    }
    StaleTime = atoi(argv[1]);
    printf("The staleness time for entry in the ODR routing table is %d\n", StaleTime);

    //Get here the list of all the hardware interfaces for this client using the get_hw_addrs function
    ConstructInterfaceInfo();

    //make the initial entries in the portPathTable for the permanent entries i.e. the path and the port number of the server process running on the system

    PortPathTableAddEntry(SERVERPATH, SERVERPORT, 1);

    printf("ODR Process running on host %s\n", selfCanonicalHostName);

    //create a domain socket for getting the requests and delivering packet to local applications
    domainSocket = Socket(AF_LOCAL, SOCK_DGRAM, 0);

    //unlink the ODR_PATH in case it is linked to any previous run of the server
    unlink(ODRPATH);
    bzero(&odrBindAddr, sizeof(odrBindAddr));
    strcpy(odrBindAddr.sun_path, ODRPATH);
    odrBindAddr.sun_family = AF_LOCAL;
    Bind(domainSocket, (SA *)&odrBindAddr, sizeof(odrBindAddr));
    FD_ZERO(&fdset);

    while(1) {
       FD_ZERO(&fdset);
       maxfdp1 = domainSocket;
       FD_SET(domainSocket, &fdset);
       for (i = 0; i < interfaceInfoCount; i++) {
          FD_SET(interfaceInfo[i].sockfd, &fdset);
          maxfdp1 = max(maxfdp1, interfaceInfo[i].sockfd);
       }

       maxfdp1 = maxfdp1 + 1;
       //wait in a blocking select in both the sockets so that as soon as one of them becomes readable we can proceed
       Select(maxfdp1, &fdset, NULL, NULL, NULL);
       if (FD_ISSET(domainSocket, &fdset)) {

          HandleDomainSocketRequest();
          
             //perform operations for input packets from the Applications (client or server)
          //processApplicationRequest(  pfSocket, domainSock);
       }

       for (i = 0; i < interfaceInfoCount; i++) {
          if ( FD_ISSET(interfaceInfo[i].sockfd, &fdset)) {
             //perfom operations for input packets from network coming through PF_Packet sockets
             HandlePFPacketRequest(interfaceInfo[i].sockfd, interfaceInfo[i].interfaceInfo->if_index);
          }
       }
    }
}


void HandlePFPacketRequest(int sockfd,           // pf sockfd
                           int arrivalInterface) // Interface index on which packet arrived
{
   EthernetPacket etherPacket;
   int retval;
   char odrPacketSource[256];
   char odrPacketDest[256];
   OdrPacket *odrPacket;
   struct ethhdr *ethernetHeader;

   memset(&etherPacket, 0, sizeof (etherPacket));

   retval = recvfrom(sockfd, (char *)&etherPacket, sizeof (etherPacket), 0, NULL, NULL); 
   if (retval < 0) {
      printf("%s: Error in recvfrom.\n", __FUNCTION__);
      return; 
   }

   // Make local ptr point to the received odr packet;
   odrPacket = &etherPacket.odr;
   ethernetHeader = &etherPacket.header;
   GetHostByAddr(odrPacket->hdr.sourceIP, odrPacketSource);
   GetHostByAddr(odrPacket->hdr.destIP, odrPacketDest);

   if (odrPacket->hdr.type == OdrPacketTypeRREQ) {
      printf("ODR at node %s: received RREQ packet source: %s dest: %s.\n", selfCanonicalHostName, odrPacketSource, odrPacketDest);
      ProcessRREQPacket(odrPacket, arrivalInterface, ethernetHeader);
   } else if (odrPacket->hdr.type == OdrPacketTypeRREP) {
      OdrPacket *rrepPacket = odrPacket;
      printf("ODR at node %s: received RREP packet source: %s dest: %s\n", selfCanonicalHostName, odrPacketSource, odrPacketDest);
      // Increment hop count by 1
      rrepPacket->hdr.hopCount += 1;
      // Add an entry for the source in our routing table We know how to reach this sourceIP from the arrival interface using the h_source hw address.
      // Note that hop count is being updated because the routing table might already have entry for the source which is better than arrivalInterface.
      rrepPacket->hdr.hopCount = RoutingTableAddOrUpdateEntry(rrepPacket->pkt.rrep.forceRediscovery, rrepPacket->hdr.sourceIP, arrivalInterface, ethernetHeader->h_source, rrepPacket->hdr.hopCount);
      ProcessRREPPacket(rrepPacket);
   } else if (odrPacket->hdr.type == OdrPacketTypeAPP) {
      OdrPacket *appPacket = odrPacket;
      printf("ODR at node %s: received APP packet source: %s dest: %s.\n", selfCanonicalHostName, odrPacketSource, odrPacketDest);
      // Increment hop count by 1
      appPacket->hdr.hopCount += 1;
      // Add an entry for the source in our routing table We know how to reach this sourceIP from the arrival interface using the h_source hw address.
      // Note that hop count is being updated because the routing table might already have entry for the source which is better than arrivalInterface.
      appPacket->hdr.hopCount = RoutingTableAddOrUpdateEntry(0, appPacket->hdr.sourceIP, arrivalInterface, ethernetHeader->h_source, appPacket->hdr.hopCount);
      ProcessAppPacket(appPacket, 0);
   } else {
      printf("ODR at node %s: received an odr packet type which it does not understand. Exiting\n", selfCanonicalHostName);
      exit(0);
   }

   return;
}


void ProcessRREQPacket(OdrPacket *rreqPacket,
                       int arrivalInterface,
                       struct ethhdr *ethernetHeader)
{
    PendingProcessedMsgElement *processedRREQRecord;
    OdrPacket *newOrStoredRreqPacket = NULL;
    RoutingTableElement *routeEntry1 = NULL;	
    RoutingTableElement *routeEntry2 = NULL;	

    //check if this RREQ packet was generated by us in such a case ignore the RREQ
    if (rreqPacket->hdr.sourceIP == selfCanonicalIP) {
       printf("ODR at node %s: received rreq generated by our node hence ignoring it.\n", selfCanonicalHostName);
       return;
    }

    //check if the RREQ is a duplicate RREQ ie it has been already processed by the node 
    //get the source IP of the RREQ and the Broadcast Id from the Packet header
    processedRREQRecord = PendingProcessedMsgTableGetEntry(&processedMsgTableHead, rreqPacket->hdr.sourceIP, rreqPacket->pkt.rreq.broadcastId);
    if (processedRREQRecord) {
       newOrStoredRreqPacket = (OdrPacket *)(processedRREQRecord->msgData);
    }
    //increment the hopCount of the arrived RREQ
    rreqPacket->hdr.hopCount++;
    if (newOrStoredRreqPacket != NULL) {
       //This portion of the code is for a duplicate RREQ request
       //check if the current hop Count is less than stored hop count 
       if (rreqPacket->hdr.hopCount < newOrStoredRreqPacket->hdr.hopCount) {
          newOrStoredRreqPacket->hdr.hopCount = rreqPacket->hdr.hopCount;
          newOrStoredRreqPacket->pkt.rreq.rrepSent = rreqPacket->pkt.rreq.rrepSent;
       } else {
          return;
       }
    } else {
       newOrStoredRreqPacket = (OdrPacket *)calloc (1, sizeof (*newOrStoredRreqPacket));

       newOrStoredRreqPacket->hdr.type = OdrPacketTypeRREQ;
       newOrStoredRreqPacket->hdr.sourceIP = rreqPacket->hdr.sourceIP;
       newOrStoredRreqPacket->hdr.destIP = rreqPacket->hdr.destIP;
       newOrStoredRreqPacket->hdr.hopCount = rreqPacket->hdr.hopCount;
       newOrStoredRreqPacket->pkt.rreq.forceRediscovery = rreqPacket->pkt.rreq.forceRediscovery;
       newOrStoredRreqPacket->pkt.rreq.broadcastId = rreqPacket->pkt.rreq.broadcastId;
       newOrStoredRreqPacket->pkt.rreq.rrepSent = rreqPacket->pkt.rreq.rrepSent;

       //add the RREQ to the list of RREQ's that have already been sent
       PendingProcessedMsgTableAddEntry(&processedMsgTableHead, newOrStoredRreqPacket->hdr.sourceIP, newOrStoredRreqPacket->pkt.rreq.broadcastId, (void *)newOrStoredRreqPacket);
   }
   /*update the Routing table for building the reverse path in the routing table in case it is not present or modifying the routing table in case the current RREQ has a lower hop count than the entry in the table*/
   newOrStoredRreqPacket->hdr.hopCount = RoutingTableAddOrUpdateEntry(newOrStoredRreqPacket->pkt.rreq.forceRediscovery, newOrStoredRreqPacket->hdr.sourceIP, arrivalInterface, ethernetHeader->h_source, newOrStoredRreqPacket->hdr.hopCount);
     
   routeEntry1 = RoutingTableGetEntryForDestination(newOrStoredRreqPacket->hdr.destIP);
   if (newOrStoredRreqPacket->hdr.destIP == selfCanonicalIP) {
      printf("ODR at node %s: received rreq targeted for self. Sending corresponding rrep\n", selfCanonicalHostName);
      if (newOrStoredRreqPacket->pkt.rreq.rrepSent != 1) {
         /*Creating RREP packet*/
         OdrPacket *newRrepPacket = NULL;
         newRrepPacket = (OdrPacket *)calloc (1, sizeof (OdrPacket));
	
         newRrepPacket->hdr.type = OdrPacketTypeRREP;
         newRrepPacket->hdr.sourceIP = newOrStoredRreqPacket->hdr.destIP;
         newRrepPacket->hdr.destIP = newOrStoredRreqPacket->hdr.sourceIP;
         newRrepPacket->hdr.hopCount = 0;
         newRrepPacket->pkt.rrep.forceRediscovery = newOrStoredRreqPacket->pkt.rrep.forceRediscovery;
         newRrepPacket->pkt.rrep.broadcastId = newOrStoredRreqPacket->pkt.rreq.broadcastId;

         routeEntry2 = RoutingTableGetEntryForDestination(newRrepPacket->hdr.destIP);
         if (routeEntry2) {   
            /*call function to send the RREP on predecided reverse path*/
            SendODRAppOrRREPMessageToNeighbor(routeEntry2, newRrepPacket);		
            newOrStoredRreqPacket->pkt.rreq.rrepSent = 1;
         } else {
            printf("ODR at node %s: Routing entry not found for sending the rrep.\n", selfCanonicalHostName);
         }
         free(newRrepPacket);
      } else {
         printf("ODR at node %s: rrep already sent for the received rreq.\n", selfCanonicalHostName);
      }
   } else {
      if (newOrStoredRreqPacket->pkt.rreq.forceRediscovery == 1) {
         /*call RREQ broadcast function*/
         printf("ODR at node %s: rreq force discovery is set.\n", selfCanonicalHostName);
      } else if (routeEntry1 == NULL) {
         printf("ODR at node %s: routing table has no entry for received rreq. Broadcasting to generate the route.\n", selfCanonicalHostName);
      } else {
         if (newOrStoredRreqPacket->pkt.rreq.rrepSent != 1) {
            if (routeEntry1 != NULL) {
               OdrPacket *newRrepPacket = NULL;
               newRrepPacket = (OdrPacket *)calloc (1, sizeof (OdrPacket));

               newRrepPacket->hdr.type = OdrPacketTypeRREP;
               newRrepPacket->hdr.sourceIP = routeEntry1->destIP;
               newRrepPacket->hdr.destIP = newOrStoredRreqPacket->hdr.sourceIP;
               newRrepPacket->hdr.hopCount = routeEntry1->hopCount;
               newRrepPacket->pkt.rrep.forceRediscovery = newOrStoredRreqPacket->pkt.rreq.forceRediscovery;
               newRrepPacket->pkt.rrep.broadcastId = newOrStoredRreqPacket->pkt.rreq.broadcastId;

               routeEntry2 = RoutingTableGetEntryForDestination(newRrepPacket->hdr.destIP);
               if (routeEntry2) {   
                  /*call function to send the RREP on predecided reverse path*/
                  SendODRAppOrRREPMessageToNeighbor(routeEntry2, newRrepPacket);		
                  newOrStoredRreqPacket->pkt.rreq.rrepSent = 1;
               } else {
                  printf("ODR at node %s: Routing entry not found for sending the rrep.\n", selfCanonicalHostName);
               }
               free(newRrepPacket);
            }
         } else {
            printf("ODR at node %s: rrep already sent for the received rreq.\n", selfCanonicalHostName);
         }	
      }
      SendRREQMessageToNeighbors(newOrStoredRreqPacket, arrivalInterface);
   }
   return;
}


void ProcessRREPPacket(OdrPacket *rrepPacket)
{
   /*find out if the RREP packet was destined for the current Node . in such a case find out the RREQ for which this RREP was generated and take action accordingly*/
   OdrPacket *odrPacketFromRREPList; 
   /*if this is the case fetch the Packet(RREP or App msg) in the stored list for which the RREQ was generated in response to which we have receive this RREP*/
   if (rrepPacket->hdr.destIP == selfCanonicalIP) {
      PendingProcessedMsgElement *tempStoredEntry;
      tempStoredEntry = PendingProcessedMsgTableGetEntry(&pendingMsgTableHead, rrepPacket->hdr.destIP, rrepPacket->pkt.rrep.broadcastId);
      if (tempStoredEntry != NULL) {
         odrPacketFromRREPList = (OdrPacket*)(tempStoredEntry->msgData);
         if (odrPacketFromRREPList->hdr.type == OdrPacketTypeRREP) {
            //This RREP is for a RREQ that we generated in response to a RREP that we were supposed to forward and for which we did not have a path
            //first of all delete this entry from the stored table and now 
            PendingProcessedMsgTableDeleteEntry(&pendingMsgTableHead, rrepPacket->hdr.destIP, rrepPacket->pkt.rrep.broadcastId);
	        //Now process the RREP just fetched from the list
            ProcessRREPPacket(odrPacketFromRREPList);
         } else if(odrPacketFromRREPList->hdr.type == OdrPacketTypeAPP) {
            //This RREP is for a RREQ that we generated in response to a Appmessage that we were supposed to forward and for which we did not have a path
            //first of all delete this entry from the stored table and now 
            PendingProcessedMsgTableDeleteEntry(&pendingMsgTableHead, rrepPacket->hdr.destIP, rrepPacket->pkt.rrep.broadcastId);
	        //Now process the App message just fetched from the list
            ProcessAppPacket(odrPacketFromRREPList, 0);                
         }
         free(odrPacketFromRREPList);
         return;
      } else {
         printf("ODR at node %s: No entry found in the buffered messages list. The entry corresponding to this rrep has already been processed.\n", selfCanonicalHostName);  
         return; 
      }
   } else {
      //Destination IP is not equal to the current node's canonical IP this implies we will try to look for routing table entry and forward this RREP
      RoutingTableElement *routeForRREP;
      routeForRREP = RoutingTableGetEntryForDestination(rrepPacket->hdr.destIP);
      if (routeForRREP == NULL) {
         //This implies that there is no route for the RREP in the routing table so just store the RREP in the RREP list and make a new RREQ 
         OdrPacket rreqPacket;
         OdrPacket *rrepPacketCopy;
         
         rreqPacket.hdr.type = OdrPacketTypeRREQ;
         // We are the originator of this RREQ packet
         rreqPacket.hdr.sourceIP = selfCanonicalIP;
         rreqPacket.hdr.destIP = rrepPacket->hdr.destIP;
         rreqPacket.hdr.hopCount = 0;
         rreqPacket.pkt.rreq.forceRediscovery = rrepPacket->pkt.rrep.forceRediscovery;
         rreqPacket.pkt.rreq.broadcastId = GetNextBroadcastId();

         rrepPacketCopy = (OdrPacket *)calloc(1, sizeof(OdrPacket));
         memcpy(rrepPacketCopy, rrepPacket, sizeof(OdrPacket));

         printf("ODR at node %s: No route present for the destination specified in the rrep. Generating a new rreq\n", selfCanonicalHostName);
         //Save the RREP in the list of rreps and messages
         PendingProcessedMsgTableAddEntry(&pendingMsgTableHead, selfCanonicalIP, rrepPacket->pkt.rrep.broadcastId , (void *)rrepPacket);
         // Now flood the new RREQ to the neighbours
         SendRREQMessageToNeighbors(&rreqPacket, 0);
         return;     
      } else {
         //Route is present . Simply forward the RREP to the next hop 
         SendODRAppOrRREPMessageToNeighbor(routeForRREP, rrepPacket); 
         return;
      }
   }
}


void ProcessAppPacket(OdrPacket *appPacket,
                      int forceRediscovery)
{
   char peerName[256];
   if(selfCanonicalIP == appPacket->hdr.destIP) {
      AppOdrPacket packet;
      struct sockaddr_un unAddr;
      int retval;
      
      PortPathTableElement *portPathTableElement = PortPathTableGetEntry(appPacket->pkt.app.destPort);
      if (NULL == portPathTableElement) {
         printf("ODR at node %s: could not find an entry for the dest port %d\n", selfCanonicalHostName, appPacket->pkt.app.destPort);
         return;
      }

      memset(&unAddr, 0, sizeof (unAddr));
      unAddr.sun_family = AF_LOCAL;
      strcpy(unAddr.sun_path, portPathTableElement->sunPath);

      memset(&packet, 0, sizeof (packet));
      packet.destIP = appPacket->hdr.sourceIP;
      packet.port = appPacket->pkt.app.sourcePort;
      memcpy(packet.msgData, appPacket->pkt.app.msgData, APPODRMSGSIZE);

      GetHostByAddr(appPacket->hdr.sourceIP, peerName);
      if(appPacket->pkt.app.destPort != SERVERPORT) {
         printf("ODR at node %s: sending reply from local server for the client at node %s.\n", selfCanonicalHostName, peerName);
      } else {
         printf("ODR at node %s: sending request from client at node %s for the local server.\n", selfCanonicalHostName, peerName);
      }

      retval = sendto(domainSocket, (char*)&packet, sizeof(packet), 0, (SA*)&unAddr, sizeof(unAddr));
      if (retval < 0) {
         printf("%s: Error in sendto.\n", __FUNCTION__);
      }
   } else {
      RoutingTableElement * routingTableElement;

      int rediscoverRoute;
      if (forceRediscovery) {
         printf("ODR at node %s: Force rediscovery is set for the application payload packet.\n", selfCanonicalHostName);
      }
      routingTableElement = RoutingTableGetEntryForDestination(appPacket->hdr.destIP);
      if (routingTableElement == NULL) {
         GetHostByAddr(appPacket->hdr.destIP, peerName);
         printf("ODR at node %s: could not find an entry in routing table for destination %s.\n", selfCanonicalHostName, peerName);
      }

      if (!routingTableElement || forceRediscovery) {
         OdrPacket rreqPacket;
         OdrPacket *appPacketCopy;

         memset(&rreqPacket, 0, sizeof (rreqPacket));

         rreqPacket.hdr.type = OdrPacketTypeRREQ;
         // We are the originator of this App packet
         rreqPacket.hdr.sourceIP = selfCanonicalIP;
         rreqPacket.hdr.destIP = appPacket->hdr.destIP;
         // We are the originator of this App packet
         rreqPacket.hdr.hopCount = 0;
         rreqPacket.pkt.rreq.forceRediscovery = forceRediscovery;
         rreqPacket.pkt.rreq.broadcastId = GetNextBroadcastId();
         printf("ODR at node %s: saving application payload and broadcasting RREQ message\n", selfCanonicalHostName);

         appPacketCopy = (OdrPacket *)calloc(1, sizeof(OdrPacket));
         memcpy(appPacketCopy, appPacket, sizeof(OdrPacket));
         PendingProcessedMsgTableAddEntry(&pendingMsgTableHead, selfCanonicalIP, rreqPacket.pkt.rreq.broadcastId, (void *)appPacketCopy);
         SendRREQMessageToNeighbors(&rreqPacket, 0);
      } else {
         SendODRAppOrRREPMessageToNeighbor(routingTableElement, appPacket);
      }
   }
}


void HandleDomainSocketRequest()
{
   struct sockaddr_un unAddr;
   int retval;
   AppOdrPacket packet;
   char peerName[256];
   int unAddrLen;
   int odrPort;

   memset(&packet, 0, sizeof (packet));
   memset(&unAddr, 0, sizeof (unAddr));
   unAddrLen = sizeof (struct sockaddr_un);
   retval = recvfrom(domainSocket, (char *)&packet, sizeof(packet), 0, (SA*)&unAddr, &unAddrLen); 
   if (retval < 0) {
      printf("%s: Error in recvfrom.\n", __FUNCTION__);
      return;
   }

   odrPort = PortPathTableAddSunPath(unAddr.sun_path);

   GetHostByAddr(packet.destIP, peerName);
   if (packet.port != SERVERPORT) {
      printf("ODR at node %s: received reply from server for client at node %s.\n", selfCanonicalHostName, peerName);
   } else {
      printf("ODR at node %s: received request from client for the server at node %s.\n", selfCanonicalHostName, peerName);
   }

   if (selfCanonicalIP == packet.destIP) {
      PortPathTableElement *portPathTableElement = PortPathTableGetEntry(packet.port);
      if (NULL == portPathTableElement) {
         printf("ODR at node %s: could not find an entry for the dest port %d\n", selfCanonicalHostName, packet.port);
         return;
      }
   
      memset(&unAddr, 0, sizeof (unAddr));
      unAddr.sun_family = AF_LOCAL;
      strcpy(unAddr.sun_path, portPathTableElement->sunPath);
      if(packet.port != SERVERPORT) {
         printf("ODR at node %s: sending reply from server for the client at node %s.\n", selfCanonicalHostName, peerName);
      } else {
         printf("ODR at node %s: sending request from client for the server at node %s.\n", selfCanonicalHostName, peerName);
      }
      packet.port = odrPort;
      retval = sendto(domainSocket, (char*)&packet, sizeof(packet), 0, (SA*)&unAddr, sizeof(unAddr));
      if (retval < 0) {
         printf("Error in sendto. Please check if the server and client process running on %s\n", selfCanonicalHostName);
      }
   } else {
      OdrPacket appPacket;
      
      memset(&appPacket, 0, sizeof (appPacket));
      appPacket.hdr.type = OdrPacketTypeAPP;
      // We are the originator of this App packet
      appPacket.hdr.sourceIP = selfCanonicalIP;
      appPacket.hdr.destIP = packet.destIP;
      // We are the originator of this App packet
      appPacket.hdr.hopCount = 0;
      appPacket.pkt.app.sourcePort = odrPort;
      appPacket.pkt.app.destPort = packet.port;
      memcpy(appPacket.pkt.app.msgData, packet.msgData, APPODRMSGSIZE);
      RoutingTableDeleteStaleEntries();
      ProcessAppPacket(&appPacket, packet.forceRediscovery);
   }        
}


void GetHostByAddr(unsigned int IPAddr,
                   char *hostName)
{
   struct in_addr ipv4addr;
   struct hostent *temp;
   bzero(&ipv4addr, sizeof (ipv4addr));

   ipv4addr.s_addr = IPAddr;
   temp = gethostbyaddr(&ipv4addr, sizeof (ipv4addr), AF_INET);
   strcpy(hostName, temp->h_name);
   return;
}


void ConstructInterfaceInfo()
{
   struct hwa_info *hptr, *kptr;
   int i;
   struct sockaddr_in* tempAddr;
   struct sockaddr_ll pfAddr;
   char macAddr[64];

   hptr = get_hw_addrs();
   for(kptr = hptr ; kptr != NULL; kptr = kptr->hwa_next) {
      if(!strcmp(kptr->if_name, "eth0")) {
         tempAddr = (struct sockaddr_in*)kptr->ip_addr;
         selfCanonicalIP = tempAddr->sin_addr.s_addr;
         GetHostByAddr(selfCanonicalIP, selfCanonicalHostName);
         continue;
      }
      // Ignore loopback and aliased addresses like eth0:1
      if (!strcmp(kptr->if_name, "lo") || !strncmp(kptr->if_name, "eth0", 4)) {
         continue;
      }

      interfaceInfo[interfaceInfoCount].interfaceInfo = kptr;
      interfaceInfo[interfaceInfoCount].sockfd = Socket(PF_PACKET, SOCK_RAW, htons(PROTOCOLNUM));
      if(interfaceInfo[interfaceInfoCount].sockfd < 0 ) {
          printf("Error creating Socket\n");
          exit(-1);
      }

      pfAddr.sll_family = PF_PACKET;
      pfAddr.sll_ifindex = kptr->if_index;
      pfAddr.sll_protocol = htons(PROTOCOLNUM);
      Bind(interfaceInfo[interfaceInfoCount].sockfd, (SA*)&pfAddr, sizeof(pfAddr));
      strcpy(interfaceInfo[interfaceInfoCount].IPAddress, Sock_ntop_host(kptr->ip_addr, sizeof(*tempAddr)));
      interfaceInfoCount++;
   }

   printf("***********Printing Interface Info For this Node Total:%d***************\n", interfaceInfoCount);
   for(i = 0;  i < interfaceInfoCount; i++) {
      sprintf(macAddr, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x:%.2x:%.2x", interfaceInfo[i].interfaceInfo->if_haddr[0] & 0xff, interfaceInfo[i].interfaceInfo->if_haddr[1] & 0xff, interfaceInfo[i].interfaceInfo->if_haddr[2] & 0xff, interfaceInfo[i].interfaceInfo->if_haddr[3] & 0xff, interfaceInfo[i].interfaceInfo->if_haddr[4] & 0xff, interfaceInfo[i].interfaceInfo->if_haddr[5] & 0xff, interfaceInfo[i].interfaceInfo->if_haddr[6] & 0xff, interfaceInfo[i].interfaceInfo->if_haddr[7] & 0xff);
      printf("***************************************************************\n");      
      printf("Interface Index  : %d\n", interfaceInfo[i].interfaceInfo->if_index);
      printf("Interface name   : %s\n", interfaceInfo[i].interfaceInfo->if_name);
      printf("Hardware Address : %s\n", macAddr);
      printf("IP Address       : %s\n", interfaceInfo[i].IPAddress);
      printf("PF socket fd     : %d\n", interfaceInfo[i].sockfd);
      printf("***************************************************************\n");      
   }  
}


void SendODRAppOrRREPMessageToNeighbor(RoutingTableElement *routingTableElement,
                                       OdrPacket * odrPacket)
{
   int i, pfsockFromWhichToSend, t;
   char srcName[256], destnName[256];
   //char *destnMACPtr ;
   EthernetPacket *ethrPck = (EthernetPacket *)calloc(1, sizeof(EthernetPacket));
   struct sockaddr_ll odrPfSockAddr;
   int retval;

   for(i = 0; i < interfaceInfoCount; i++) {
      if(routingTableElement->interface == interfaceInfo[i].interfaceInfo->if_index) {
         pfsockFromWhichToSend = interfaceInfo[i].sockfd;
         break;
      }
   }

   if (i == interfaceInfoCount) {
      printf("Unable to find interface to send the packet to.\n");
      return;
   }

   // Get the names of the source and destination VM's using GethostbyName function 
   memcpy(&(ethrPck->odr), odrPacket, sizeof(*odrPacket));

   GetHostByAddr(odrPacket->hdr.destIP, destnName);
   GetHostByAddr(odrPacket->hdr.sourceIP, srcName);
   odrPfSockAddr.sll_family = PF_PACKET;
   odrPfSockAddr.sll_protocol = htons(PROTOCOLNUM);
   odrPfSockAddr.sll_ifindex  = routingTableElement->interface;
   odrPfSockAddr.sll_hatype = ARPHRD_ETHER;
   odrPfSockAddr.sll_pkttype = PACKET_OTHERHOST;
   odrPfSockAddr.sll_halen = ETH_ALEN;
   odrPfSockAddr.sll_addr[0] = routingTableElement->nextHop[0] & 0xff;
   odrPfSockAddr.sll_addr[1] = routingTableElement->nextHop[1] & 0xff;
   odrPfSockAddr.sll_addr[2] = routingTableElement->nextHop[2] & 0xff;
   odrPfSockAddr.sll_addr[3] = routingTableElement->nextHop[3] & 0xff;
   odrPfSockAddr.sll_addr[4] = routingTableElement->nextHop[4] & 0xff;
   odrPfSockAddr.sll_addr[5] = routingTableElement->nextHop[5] & 0xff;
   //fill in the Ethernet header with the name 
   memcpy(ethrPck->header.h_dest, routingTableElement->nextHop, ETH_ALEN) ;
   memcpy(ethrPck->header.h_source, interfaceInfo[i].interfaceInfo->if_haddr, ETH_ALEN);
   ethrPck->header.h_proto = htons(PROTOCOLNUM);
   //printf("********************************************************************\n");
   /* 
   destnMACPtr = ethrPck->header.h_dest;
   printf("ODR at node %s : sending frame hdr src %s ", srcName, destnName);
   printf("dest addr :") ;
   for (t = 0; t <= 4; t++) {
         printf(".2x:", *destnMACPtr & 0xff);
         destnMACPtr++;
   }
   printf(".2x\n", *destnMACPtr & 0xff);*/
   //find whether it is a RREP message or Apppayload and then take the action accordingly
   if(odrPacket->hdr.type == OdrPacketTypeRREP) {
      printf("ODR at node %s: sending frame hdr src: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x dest: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n", selfCanonicalHostName,
             ethrPck->header.h_source[0] & 0xff, ethrPck->header.h_source[1] & 0xff, ethrPck->header.h_source[2] & 0xff, ethrPck->header.h_source[3] & 0xff, ethrPck->header.h_source[4] & 0xff, ethrPck->header.h_source[5] & 0xff,
             ethrPck->header.h_dest[0] & 0xff, ethrPck->header.h_dest[1] & 0xff, ethrPck->header.h_dest[2] & 0xff, ethrPck->header.h_dest[3] & 0xff, ethrPck->header.h_dest[4] & 0xff, ethrPck->header.h_dest[5] & 0xff
             );
      printf("ODR at node %s: ODR msg type: RREP src: %s dest: %s\n", selfCanonicalHostName, srcName, destnName);
   } else if (odrPacket->hdr.type == OdrPacketTypeAPP) {
      printf("ODR at node %s: sending frame hdr src: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x dest: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n", selfCanonicalHostName,
             ethrPck->header.h_source[0] & 0xff, ethrPck->header.h_source[1] & 0xff, ethrPck->header.h_source[2] & 0xff, ethrPck->header.h_source[3] & 0xff, ethrPck->header.h_source[4] & 0xff, ethrPck->header.h_source[5] & 0xff,
             ethrPck->header.h_dest[0] & 0xff, ethrPck->header.h_dest[1] & 0xff, ethrPck->header.h_dest[2] & 0xff, ethrPck->header.h_dest[3] & 0xff, ethrPck->header.h_dest[4] & 0xff, ethrPck->header.h_dest[5] & 0xff
             );
      printf("ODR at node %s: ODR msg type: APP src: %s dest: %s\n", selfCanonicalHostName, srcName, destnName);
   } else {
      printf("Dealing with unknown message type. Ignoring it.\n");
      goto exit;
   }
   //printf("**********************************************************************\n");
   //send the packet 
   retval = sendto(pfsockFromWhichToSend, ethrPck, sizeof(EthernetPacket), 0, (struct sockaddr *)&odrPfSockAddr, sizeof(struct sockaddr_ll));
   if (retval < 0) {
      printf("SendODRAppOrRREPMessageToNeighbor: Error in sendto\n");
   }
exit:
   free(ethrPck);
}

void SendRREQMessageToNeighbors(OdrPacket *rreq,
                                int arrivalInterface)
{
   int count, retval, actualCount = 0;
   char sourceName[256];
   char destinationName[256];	
   struct EthernetPacket ethernetFrame;
   struct sockaddr_ll socket_addr;

   GetHostByAddr(rreq->hdr.sourceIP, sourceName);
   GetHostByAddr(rreq->hdr.destIP, destinationName);
	
   printf("ODR at node %s: Sending broadcast RREQ message for src: %s and dest: %s\n", selfCanonicalHostName, sourceName, destinationName);
   for (count = 0; count < interfaceInfoCount; count++) {
      if (interfaceInfo[count].interfaceInfo->if_index == arrivalInterface) {
         printf("ODR at node %s: Ignoring arrival interface %d for broadcast\n", selfCanonicalHostName, arrivalInterface);
         continue;
      }
      memset(&socket_addr, 0, sizeof (socket_addr));
      /*Fill the socket_addr structure*/
      socket_addr.sll_family = PF_PACKET;
      socket_addr.sll_ifindex = interfaceInfo[count].interfaceInfo->if_index;
      socket_addr.sll_protocol = htons(PROTOCOLNUM);
      socket_addr.sll_hatype = ARPHRD_ETHER;
      socket_addr.sll_pkttype = PACKET_BROADCAST;
      socket_addr.sll_halen	= ETH_ALEN;
      memcpy(socket_addr.sll_addr, BroadcastAddress, ETH_ALEN);

      /*fill the ethernet frame header*/
      memcpy(ethernetFrame.header.h_dest, BroadcastAddress, ETH_ALEN);
      memcpy(ethernetFrame.header.h_source, interfaceInfo[count].interfaceInfo->if_haddr, ETH_ALEN);
      ethernetFrame.header.h_proto = htons(PROTOCOLNUM);
      memcpy(&(ethernetFrame.odr), rreq, sizeof(ethernetFrame.odr));

      printf("ODR at node %s: sending frame hdr src: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x dest: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
             selfCanonicalHostName,
             ethernetFrame.header.h_source[0] & 0xff, ethernetFrame.header.h_source[1] & 0xff, ethernetFrame.header.h_source[2] & 0xff, ethernetFrame.header.h_source[3] & 0xff, ethernetFrame.header.h_source[4] & 0xff, ethernetFrame.header.h_source[5] & 0xff,
             ethernetFrame.header.h_dest[0] & 0xff, ethernetFrame.header.h_dest[1] & 0xff, ethernetFrame.header.h_dest[2] & 0xff, ethernetFrame.header.h_dest[3] & 0xff, ethernetFrame.header.h_dest[4] & 0xff, ethernetFrame.header.h_dest[5] & 0xff
             );
      printf("ODR at node %s: ODR msg type: RREQ src: %s dest: %s\n", selfCanonicalHostName, sourceName, destinationName);

      /*call sendto()*/
      retval = sendto(interfaceInfo[count].sockfd, (char *)&ethernetFrame, sizeof(ethernetFrame), 0, (struct sockaddr *)&socket_addr, sizeof(socket_addr));		
      if (retval < 0) {
         printf("SendRREQMessageToNeighbors: Error in sendto\n");
      }
      actualCount++;
   }
   printf("ODR at node %s: RREQ Message successfully broadcasted to %d interface(s).\n", selfCanonicalHostName, actualCount);     
}
