#include <string.h>
#include <time.h>
#include "unp.h"

#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/if_packet.h>

#include "hw_addrs.h"

extern int StaleTime;
extern int PortPathStaleTime;
extern int MonotonicPortNum;

#define SERV_UNIXSTR_PATH_DG "/temp/alok_path"
#define ODRPATH "/tmp/odr_alok_path" 
#define SERVERPATH "/tmp/server_alok_path"
#define SERVERPORT 25871
#define PROTOCOLNUM 8308 

#define APPODRMSGSIZE 256

// Packet structure from App to ODR and ODR to App
typedef struct AppOdrPacket {
   // IP Address
   unsigned int destIP;
   // Port
   int port;
   // If forced rediscovery
   int forceRediscovery;
   // Actual message data to send or received
   char msgData[APPODRMSGSIZE];
} AppOdrPacket;

typedef struct InterfaceInfo {
   struct hwa_info* interfaceInfo;
   int sockfd;
   char	IPAddress[16];   
} InterfaceInfo;

typedef enum OdrPacketType {
   OdrPacketTypeRREQ = 0,
   OdrPacketTypeRREP,
   OdrPacketTypeAPP,
} OdrPacketType;

typedef struct OdrPacketHeader {
   OdrPacketType type;
   unsigned int sourceIP;
   unsigned int destIP;
   int hopCount;
} OdrPacketHeader;

typedef struct OdrPacketRREQ {
   int forceRediscovery;
   int broadcastId;
   int rrepSent;
} OdrPacketRREQ;

typedef struct OdrPacketRREP {
   int forceRediscovery;
   int broadcastId;
} OdrPacketRREP;

typedef struct OdrPacketAPP {
   int sourcePort;
   int destPort;
   char msgData[APPODRMSGSIZE];
} OdrPacketAPP;


typedef struct OdrPacket {
   OdrPacketHeader hdr;
   union {
      OdrPacketRREQ rreq;
      OdrPacketRREP rrep;
      OdrPacketAPP  app;
   } pkt;
} OdrPacket;

typedef struct EthernetPacket {
   struct ethhdr header;
   OdrPacket odr;
} EthernetPacket;

typedef struct PortPathTableElement {
   int portNum;
   char *sunPath;
   int isPermanent;
   time_t timeStamp;
   struct PortPathTableElement *next;
} PortPathTableElement;

typedef struct PendingProcessedMsgElement {
   void *msgData;
   unsigned int sourceIP;
   int broadcastId;
   struct PendingProcessedMsgElement *next;
} PendingProcessedMsgElement;

typedef struct RoutingTableElement {
   // Destination IP address
   unsigned int destIP;
   // Which interface to use to send the packet to the destination
   int interface;
   // Mac address of the neighbor from which this destination is reachable
   char nextHop[IF_HADDR];
   // Destination is reachable in how many hops from this node
   int hopCount;
   // time when this entry was made
   time_t timeStamp;
   // next entry in the table
   struct RoutingTableElement *next;
} RoutingTableElement;

void RoutingTableAddElement(unsigned int destIP, int interface, char *nextHop, int hopCount);
void RoutingTablePrint();
RoutingTableElement * RoutingTableGetEntryForDestination(unsigned int destIP);
void RoutingTableDeleteStaleEntries();
int RoutingTableAddOrUpdateEntry(int forceRouteRediscovery, unsigned int destIP, int interface, char *nextHop, int hopCount);
void RoutingTablePrint();

PortPathTableElement *PortPathTableGetEntry(int portNum);
void PortPathTableAddEntry(char *sunPath,int portNum, int isPermanent);
void PortPathTableDeleteExpiredEntries();
int PortPathTableAddSunPath(char *sunPath);


PendingProcessedMsgElement *PendingProcessedMsgTableGetEntry(PendingProcessedMsgElement **head, unsigned int sourceIP, int broadcastId);
void PendingProcessedMsgTableAddEntry(PendingProcessedMsgElement **head, unsigned int sourceIP, int broadcastId, void *msgData);
void PendingProcessedMsgTableDeleteEntry(PendingProcessedMsgElement **head, unsigned int sourceIP, int broadcastId);


void GetHostByAddr(unsigned int IPAddr, char *hostName);
void ConstructInterfaceInfo();

void SendODRAppOrRREPMessageToNeighbor(RoutingTableElement *routingTableElement, OdrPacket * odrPacket);
void SendRREQMessageToNeighbors(OdrPacket *rreq, int arrivalInterface);


//declaration for msg_send and msg_recv function
void msg_send(int sockfd, char *destip, int destPort , char *msg, int forceRediscover);
void msg_recv(int sockfd, char *recvdMsg, char *srcIPAddrCanonical, int *srcPort);

