#include "utility.h"

void msg_send(int sockfd, char *destip, int destPort , char *msg, int forceRediscover)
{
    AppOdrPacket packet;
    struct sockaddr_un odrSockAddr;
    unsigned int destIP;

    memset(&odrSockAddr, 0, sizeof(odrSockAddr));
    strcpy(odrSockAddr.sun_path, ODRPATH);
    odrSockAddr.sun_family = AF_LOCAL ;

    memset(&packet, 0, sizeof (packet));
    Inet_pton(AF_INET, destip, &destIP);
    packet.destIP = destIP;
    packet.port = destPort;
    strcpy(packet.msgData, msg);
    packet.forceRediscovery = forceRediscover;

    Sendto(sockfd, (char*)&packet, sizeof(packet), 0, (struct sockaddr *)&odrSockAddr, sizeof (struct sockaddr_un));
}


void msg_recv(int sockfd, char *recvdMsg, char *srcIPAddrCanonical, int *srcPort)
{

    struct sockaddr_un odrSockAddr;
    AppOdrPacket packet;
    int len = sizeof(struct sockaddr_un );

    memset(&odrSockAddr, 0, sizeof(odrSockAddr));
    memset(&packet, 0, sizeof (packet));

    recvfrom(sockfd, (char *)&packet, sizeof(packet), 0, (struct sockaddr *)&odrSockAddr, &len);   

    strcpy(recvdMsg, packet.msgData);
    Inet_ntop(AF_INET, &(packet.destIP), srcIPAddrCanonical, INET_ADDRSTRLEN);
    *srcPort = packet.port;
}
