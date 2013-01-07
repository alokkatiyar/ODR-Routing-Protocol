#include "utility.h"

extern RoutingTableElement *routingTableHead;
extern PortPathTableElement *portPathTableHead;

void RoutingTableAddElement(unsigned int destIP,
                            int interface,
                            char *nextHop,
                            int hopCount)
{
   RoutingTableElement *entry, *temp;
   entry = (RoutingTableElement *)calloc(1, sizeof (RoutingTableElement));
   if (entry) {
      entry->destIP = destIP;
      entry->interface = interface;
      memcpy(&entry->nextHop, nextHop, sizeof (entry->nextHop));
      entry->hopCount = hopCount;
      entry->timeStamp = time(NULL);
      if (routingTableHead == NULL) {
         routingTableHead = entry;
      } else {
         temp = routingTableHead;
         while(temp->next != NULL) {
            temp = temp->next;
         }
         temp->next = entry;
      }
   }
}

void
RoutingTablePrint()
{
   int i = 0;
   char destinationIP[INET_ADDRSTRLEN];
   char macAddr[64];
   RoutingTableElement *temp = routingTableHead;
   printf("======Routing Table Begin=========\n");
   while (temp) {
      Inet_ntop(AF_INET, &temp->destIP, destinationIP, INET_ADDRSTRLEN);
      sprintf(macAddr, "%.2x:%.2x%:.2x:%.2x:%.2x:%.2x", temp->nextHop[0] & 0xff, temp->nextHop[1] & 0xff, temp->nextHop[2] & 0xff, temp->nextHop[3] & 0xff, temp->nextHop[4] & 0xff, temp->nextHop[5] & 0xff);
      printf("Entry:%d Dest IP:%s interface:%d nextHop:%s hopCount:%d\n", i, destinationIP, temp->interface, macAddr, temp->hopCount);
      temp = temp->next;
      i++;
   }
   printf("======Routing Table End===========\n");
}

RoutingTableElement *
RoutingTableGetEntryForDestination(unsigned int destIP)
{
   RoutingTableElement *temp;
   RoutingTableDeleteStaleEntries();

   temp = routingTableHead;
   while (temp) {
      if (temp->destIP == destIP) {
         return temp;
      }
      temp = temp->next;
   }
   return temp;
}

void
RoutingTableDeleteStaleEntries()
{
   RoutingTableElement *temp = routingTableHead;
   RoutingTableElement *tempNext;
   RoutingTableElement *prev = NULL;
   int updateHead = 1;

   while (temp) {
      tempNext = temp->next;
      if ((time(NULL) - temp->timeStamp) >= StaleTime) {
         if (updateHead) {
            routingTableHead = temp->next;
         }
         if (prev) {
            prev->next = tempNext;
         }
         free(temp);
         //printf("------Deleting stale entry-----\n");
      } else { 
         if (updateHead == 1) {
            updateHead = 0;
         }
         prev = temp;
      }
      temp = tempNext;
   }
}

int
RoutingTableAddOrUpdateEntry(int forceRouteRediscovery,
                             unsigned int destIP,
                             int interface,
                             char *nextHop,
                             int hopCount)
{
   RoutingTableElement * destEntry;

   RoutingTableDeleteStaleEntries();

   destEntry = RoutingTableGetEntryForDestination(destIP);
   if (!destEntry) {
      RoutingTableAddElement(destIP, interface, nextHop, hopCount);
      return hopCount;
   } else {
      if (forceRouteRediscovery || destEntry->hopCount >= hopCount) {
         destEntry->destIP = destIP;
         destEntry->interface = interface;
         memcpy(&destEntry->nextHop, nextHop, sizeof (destEntry->nextHop));
         destEntry->hopCount = hopCount;
         destEntry->timeStamp = time(NULL);
         return hopCount;
      } else {
         return destEntry->hopCount;
      }
   }
}

PortPathTableElement *
PortPathTableGetEntry(int portNum)
{
   PortPathTableElement *temp = portPathTableHead;
   while (temp) {
      if (portNum == temp->portNum) {
         return temp;
      }
      temp = temp->next;
   }
   return NULL;
}

void
PortPathTableAddEntry(char *sunPath, int portNum, int isPermanent)
{
   //make entries for permanent members i.e. the well known port number
   PortPathTableElement *entry, *temp;

   entry = (PortPathTableElement *)calloc(1, sizeof (PortPathTableElement));
   if(entry) {
      entry->portNum = portNum;
      entry->sunPath = strdup(sunPath);
      entry->isPermanent = isPermanent;
      entry->timeStamp = time(NULL);
      if (portPathTableHead == NULL) {
         portPathTableHead = entry;
      } else {
         temp = portPathTableHead;
         while(temp->next != NULL) {
            temp = temp->next;
         }
         temp->next = entry;
      }
   }
}


void
PortPathTableDeleteExpiredEntries()
{
   PortPathTableElement *temp = portPathTableHead;
   PortPathTableElement *tempNext;
   int updateHead = 1;
   while (temp) {
      tempNext = temp->next;
      if (!temp->isPermanent && (temp->timeStamp - time(NULL)) >= PortPathStaleTime) {
        if (updateHead) {
           portPathTableHead = temp->next;
        }
        if(temp->sunPath) {
           free(temp->sunPath);
        }
        free(temp);
      } else {
         updateHead = 0;
      }
      temp = tempNext;
   }

}

int
PortPathTableAddSunPath(char *sunPath)
{
    PortPathTableElement *temp;
    PortPathTableDeleteExpiredEntries();

    temp = portPathTableHead;
    while (temp != NULL) {
       //modify the timestamp of the sunPath in case it is present in the port path entry table 
       if(strcmp(temp->sunPath, sunPath) == 0) {
           temp->timeStamp = time(NULL);
           return temp->portNum;   
       }
       temp = temp->next;
    }
    
    MonotonicPortNum++;
    PortPathTableAddEntry(sunPath, MonotonicPortNum, 0);
    return MonotonicPortNum;
}

PendingProcessedMsgElement *
PendingProcessedMsgTableGetEntry(PendingProcessedMsgElement **head,
                                 unsigned int sourceIP,
                                 int broadcastId)
{
    PendingProcessedMsgElement *temp = *head;
    while( temp != NULL) {
       if(temp->sourceIP == sourceIP && temp->broadcastId == broadcastId) {
          return temp; 
       }
       temp = temp->next; 
    }
    return NULL;
}

void
PendingProcessedMsgTableAddEntry(PendingProcessedMsgElement **head,
                                 unsigned int sourceIP,
                                 int broadcastId,
                                 void *msgData)
{
   PendingProcessedMsgElement *newNode = NULL;
   PendingProcessedMsgElement *tmpNode = NULL;

   newNode = (PendingProcessedMsgElement*) calloc(1, sizeof(PendingProcessedMsgElement));
   if (newNode) {
      newNode->msgData 		= msgData;
      newNode->sourceIP		= sourceIP;
      newNode->broadcastId	= broadcastId;

      if (*head == NULL) {
         *head = newNode;
      } else {
         tmpNode = *head;
         while (tmpNode->next != NULL) {
            tmpNode = tmpNode->next;
         }
         tmpNode->next = newNode;
      }
   }
}


void
PendingProcessedMsgTableDeleteEntry(PendingProcessedMsgElement **head,
                                    unsigned int sourceIP,
                                    int broadcastId)
{
   PendingProcessedMsgElement *temp = *head;
   PendingProcessedMsgElement *tempNext;
   int updateHead = 1;
   while (temp) {
      tempNext = temp->next;
      if (temp->sourceIP == sourceIP && temp->broadcastId == broadcastId) {
        if (updateHead) {
           *head = temp->next;
        }
        free(temp);
      } else {
         updateHead = 0;
      }
      temp = tempNext;
   }

	return;
}

