
                                                      CSE 533 Assignment 3
			       Team Members

1) Alok Katiyar (108943744 )
2) Rohan R Babtiwale(108687468) 

This assignment deals with developing a routing protocol used for routing data packets between differenet nodes in a routing topology. There are three imporant parts of this assignment :
1) Client : This is the program that will be sending a request for data packet to the server
2) Server : This program will respond to the client request by sending the time to the client
3) ODR : This is the actual program that is supposed to run on all the nodes of the topology and 
is responsible for data packet delivery to and from the client and the server. Some of it's salient features include discovering efficient routes between different nodes. Keeping a track of the discovered routes in it's routing table so that any other request for delivery to the same node coming from any other client can be catered to without following the discovery process for the node time and again. However such routes are kept only for a certain period of time called the 'staleness' parameter beyond which the routes are deemed as unreliable and deleted.


We now describe each process and it's implementation in detail:

1) Client : The client process running on any node is bound to a ephemeral port(file) returned by the mkstemp function. Any reading of data is to be done through this socket. The client then prompts the user to enter the name of the node (VM1 to VM10) from which it wants to get the time. It then resolves the IP address of the server VM to find if any such VM is actually present or not. If the VM is not present when we go back to the Enter VM prompt again, else a trace message is requesting for the time is sent from the client to the domain socket of the ODR. Actually the message gets written to the 'well know port' i.e the file that the odr processes domain socket is bound to and the ODR reads the request from there and takes action on the request. This action is described in the ODR section of the README. The request is backed by a timeout just and in case no response is heard from the server. The client retransmits the request with a forced rediscovery field set the in the sent packet. This field forces the odr to discover a route for the server, even if a non stale entry is present in the routing table. If no response is heard after the second timeout as well the client again goes back to the enter VM prompt. We have applied a signal handler which unlinks the temporary file in case of the client process getting killed, just to make sure that these files do not proliferate in the system.


2) Server: The server process is bound to a well known path( analogous to well known ports) from which it listens any requests for time from the clients. The process is blocked in a call to recvfrom wherein it is waiting for any time requests to come from any of the clients running in the topology. As soon as it receives request for time which is actually routed by the ODR process running on it's node it finds out which client has sent the request and prepares and writes a response   back to the ODR domain socket. This response is delivered by the ODR running on the server, using the methodology explained below.


3) ODR Process :  As soon as the ODR process is evoked it enters the portno and the path entry of  the server process running on the node.  The port no and the path are well known entries for the server and they are deemed as a permanent members of the port path table which is composed of the followinging structure variables:
 
  struct PortPathTableElement
{
      int portNum;  //port number for a given process(this is a well known entry for the server process running on the VM
      char *path; //the path name that a process is bound to
      int isPermanent ; //whether or not this entry will be a permanent member
      time_t timestamp; // this timestamp is used to keep a track if the entry has been in the table for long enough time and can be deleted
 }

The odr process then creates a domain socket for accepting requests and writing response back to the client and server applications running on the same node.
The ODR then fetches a list of all it's interfaces and then binds each of these intefaces to a pf_packet socket to communicate with the ODR's running on different nodes( This communication can be of different types like receiving RREQ, RREP, Application messages(request for time or time responses). We do the bind any pf_packet socket to the eth0 and loopback interface.


When ever the ODR receives any request on it's domain socket we check if the request has been received fom the client of the server. If the request has been received from the server then it's entry is present in the port path table and is a permanent entry and there is no need to make the entry again.If the request is from the client then we entry the temposary file to which the client is bound and a ephemeral port number( this port number is generated by simply taking a seed value and incrementing it by one once i new request arrives from a client to the ODR).
These entries in the port path table help the odr process in determining if any request it has received is from the client or a response from the server to a client running on the same host or a different host. Entries for each  client request is made in the portPathTable and these entries help the ODR in delivering the time packets receive from the server to the client by matching the port number in the received App message against the entries in the port path tabel.

Our applicationa messages are of the the following format :

There is a common  header for each of RREP, RREQ and the App Payload messages:

typedef struct OdrPacketHeader
{
       OdrPacketType type;
       unsigned int sourceIp;
       unsigned int destnIp;
       int hopCount;
 } OdrPacketHeader;	

The RREP, RREQ and App Payload messages are constructed as follows :

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

If the ODR recieves  a domain socket request then it it creates odrPacket 
which is a structure containing the  Odr Packet header and a union of the RREQ, RREP and the App packet. We set the variable of the union to whatever type of packet we wish to send and deliver it across the network.
After the ODR packet for the application request has been created it is handled by the ProcessAppPacket function since message at the domain socket are bound to be App messages and not the RREP or the RREQ

The  ProcessAppPacket  function works as follows :

1) If the port number in the Application packet is the well known port of the server.Then this implies that there is a request from the client to a particular server on the network else it is the response from our server catering to the request of a particular client.After this the odr looks for entries in the routing table. If there is route that exists for the destination in the routing table the the segment is sent out through the pf_packet socket corresponding to the interface to the next hop in the route. Else we save the copy of the Application payload in a list that we have made for application payloads  and the RREP's and make a new RREQ and braodcast it. The manner in which RREQ's and RREP's have been handled have been described in the section below the handle any kinds of requests arriving on the pf_socket (RREQ,RREP and App Messages)


HandlePFPacketRequest works as follows:

Whenever any of the pf sockets are becomes readable we recv the ethernet packet from the socket and decide the client and destination of the packet from the ethernet header and also the the type of the packet that has been received (ie whether it is RREQ, RREP or App message).

Then these requests are distributed between the following functions which are described as below :


HandleRREQPacket :Whenever the Packet received is RREQ we first check if it was generated by our node only.In such a case we ignore the we the RREQ. If this is not the case then we search in the list of processeed RREQ's( ie RREQ's which we have already served),then we deem it as duplicate RREQ. We then see
if the hop count of the new RREQ is less than the previous RREQ and update the hop count in the list of stored RREQ's. If the RREQ is not present we add it to the list of RREQ's that we have created. We then find out if the RREQ is destined for our node only, if such is the case then we send the RREP back using the reverse path that had been built by the packet in arriving to this node and send the RREP through the outgoing inteface in the routing table.We also check for the forced rediscovery case to ensure that the route is rediscovered if the following field is set even if the current VM's routing table has a route for the RREQ, in such a case we continue to flood the RREQ.



HandleRREPPacket : Whenever we receive a new RREP. We get the hopcount from the RREP and add the route conveyed by the RREP to the routing table. We then check if the RREP was destined for us i.e. if it was created in response to a a RREQ that was created and broadcasted by us. In such a case we fetch the RREP or the APP Payload from the list for the RREP's and APP Payloads for which this RREQ was generated.
We then call the handle RREP function once again to process the Fetched RREP if the fetched entry is a RREP.
If the fetched entry is a App Message then we call the ProcessAppPacket function which has been described above to handle the fetched RREP.

Just in case we do not get any entry from the list of RREP and APP messages then we simply need to pass this RREP to whatever destination it is bound to. We then look for a route in the routing table and pass the RREP to the next hop indiacted by the route, if there is no entry in the routing table for the destination we prepare a RREQ and broadcast it to get a fresh route.


In the broadcast process we always omit eth0 and the loopback interfaces 
    


 
