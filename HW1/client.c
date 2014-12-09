#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#define USERNAME 2
#define MESSAGE 4
#define REASON 1
#define CLIENT_COUNT 3

#define JOIN 2
#define SEND 4
#define FWD 3
#define ACK 7
#define NAK 5
#define ONLINE 8
#define OFFLINE 6

// first column indicates number of attributes in each type of message
// For ACK we have used the second option of combining client count and list of users currently in the chat
int sbcp_attributes[][3] = {{0,0,0},{0,0,0},{1,USERNAME,0},{2,USERNAME,MESSAGE},{1,MESSAGE,0},{1,REASON,0},{1,USERNAME,0},{1,MESSAGE,0},{1,USERNAME,0}};

struct attribute
{
	unsigned int type:16;
	unsigned int length:16;
	char *payload;
};
struct sbcp_packet
{
	unsigned int vrsn:9;
	unsigned int type:7;
	unsigned int length:16;
	struct attribute *payload;
};

// creates a sbcp packet corresponding to join,send etc. Here message1 and message2 indicate 2nd and 3rd columns of sbcp_attributes array
struct sbcp_packet * create_packet (char *message1, char *message2, int packet_type)
{
	struct sbcp_packet *packet = (struct sbcp_packet*)malloc(sizeof(struct sbcp_packet));
	if (packet == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}
	packet->vrsn = 3;
	packet->type = packet_type;
	packet->length = 4;
	packet->payload = (struct attribute*)malloc(sizeof(struct attribute)*sbcp_attributes[packet_type][0]); 
	if (packet->payload == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}
	int i=0;
	char *message = NULL;
	for (i = 0;i < sbcp_attributes[packet_type][0];i++)
	{
		packet->payload->type = sbcp_attributes[packet_type][i+1];
		if (i==0)
			message = message1;
		else
			message = message2;
		(packet->payload+i)->length = strlen(message) + 4;   //here message contains trailing enter character, 4 bytes for header which is made '\0'
		packet->length += (packet->payload+i)->length;
		(packet->payload+i)->payload = (char *)malloc(strlen(message));
		if ((packet->payload+i)->payload == NULL)
		{
			fprintf(stderr,"failed to allocate memory\n");
			exit(0);
		}
		memcpy((packet->payload+i)->payload,message,strlen(message));
		*((packet->payload+i)->payload + strlen(message) - 1) = '\0';
	}
	return packet;
}

// Converts struct of packet to character stream
char * converttostream (struct sbcp_packet *packet, int packet_type)
{
	char *temp = (char*)malloc(packet->length);  	
	if (temp == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}
	char *maintemp = temp;
	// combining vrsn and type of packet into a 16 bit number. 
	uint16_t number = 0;
	uint16_t net_number = 0;
	memcpy(&number,packet,2);
	uint16_t n = 1;
	/* checking whether the machine is little endian or big endian. If little endian then apply circular left shift by seven bits. This is done to
	get the same number on both client and server side no matter what type of endianness. When we form a 16 bit number, big endian and little endian
	machines represent different number. In order to eliminate the descripancies we have done below operations so that the number received on other side 
	will be same no matter which order conversion happens.*/
	if(*(char *)&n == 1)
		number = (number << 7) | (number >> (sizeof(number)*8 - 7));
	net_number = htons(number);
	memcpy(temp,&net_number,2);
	// All integers like type and length are converted to network byte order where as for char buffer no change is done.
	temp+=2;
	uint16_t num = htons(packet->length);
	memcpy(temp,&num,2);
	temp+=2;
	int i = 0;
	for (i = 0;i < sbcp_attributes[packet_type][0];i++)
	{
		num = htons((packet->payload+i)->type);
		memcpy(temp,&num,2);
		temp+=2;
		num = htons((packet->payload+i)->length);
		memcpy(temp,&num,2);
		temp+=2;
		memcpy(temp,(packet->payload+i)->payload,(packet->payload+i)->length-4);
		if (i==0)
		temp+=(packet->payload+i)->length-4;
	}
	return maintemp;	
}

// converting from character stream to struct packet
struct sbcp_packet * convertfromstream(char *charstream)
{
	uint16_t number = 0;
	uint16_t net_number = 0;
	memcpy(&number,charstream,2);
	net_number = ntohs(number);

	struct sbcp_packet *packet = (struct sbcp_packet*)malloc(sizeof(struct sbcp_packet));
	if (packet == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}
	// getting type and vrsn from number after converting it to host order. First 9 most significant bits are version number and 7 least significant 
	//correspond to type
	packet->type = net_number;
	packet->vrsn = net_number >>7;
	charstream+=2;
	memcpy(&number,charstream,2);
	// numbers like type and length are converted to host byte order where as character stream is left as it is
	packet->length = ntohs(number);
	charstream+=2;
	packet->payload = (struct attribute*)malloc(sizeof(struct attribute)*sbcp_attributes[packet->type][0]); 
	if (packet->payload == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}
	int i = 0;

	for (i = 0;i < sbcp_attributes[packet->type][0];i++)
	{
		memcpy(&number,charstream,2);
		(packet->payload+i)->type = ntohs(number);
		charstream+=2;
		memcpy(&number,charstream,2);
		(packet->payload+i)->length = ntohs(number);
		charstream+=2;
		(packet->payload+i)->payload = (char *)malloc((packet->payload+i)->length - 4);
		if ((packet->payload+i)->payload == NULL)
		{
			fprintf(stderr,"failed to allocate memory\n");
			exit(0);
		}
		memcpy((packet->payload+i)->payload,charstream,(packet->payload+i)->length - 4);
		if (i==0)
		charstream+=(packet->payload+i)->length-4;
	}
	return packet;
}

int main(int argc, char *argv[])
{
	int sockfd;
	int portno;
	struct sockaddr_in serv_addr;
	struct hostent *server;
	struct in_addr ipv4addr;
        fd_set writefds;
	fd_set readfds;
	int nbytes;
	
	// information we take from the user
	char *user_input = (char *)malloc(512*sizeof(char));
	if (user_input == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}

	// message_received from the server (540 - That is the maximum size possible in this application)
	char *message_received = (char *)malloc(540*sizeof(char));
	if (message_received == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}
	
	// checking the arguments from the user
	if (argc < 4) 
	{
		fprintf(stderr,"usage %s username server_ip server_port\n", argv[0]);
		exit(0);
	}
	// checking if username exceeds the length of 16 bytes
	if (strlen(argv[1]) >= 16)
	{
		fprintf(stderr,"username should be less than 16 characters\n", argv[0]);
		exit(0);
	}		

	portno = atoi(argv[3]);

	// Create a socket point 
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) 
	{
		perror("ERROR opening socket");
		exit(1);
	}

	inet_pton(AF_INET,argv[2],&ipv4addr);
	server = gethostbyaddr(&ipv4addr,sizeof ipv4addr,AF_INET);
	
	if (server == NULL) 
	{
		fprintf(stderr,"ERROR, no such host\n");
		exit(0);
	}

	// initializing the values of server
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr,(char *)&serv_addr.sin_addr.s_addr,server->h_length);
	serv_addr.sin_port = htons(portno);

	// Now connect to the server 
	if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) 
	{
		perror("ERROR connecting");
		exit(1);
	}

	// sending the username here (introducing a dummy character to compensate for enter in other cases)
	// creating sbcp packet to send username -JOIN
	struct sbcp_packet *packet = create_packet(strcat(argv[1],"o"),NULL,JOIN);
				
	printf("Passing the username : %s\n",packet->payload->payload);
	fflush(stdout);
	
	char *message_sent = (char *)malloc(packet->length*sizeof(char));
	if (message_sent == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}	
	// converting to character stream
	memcpy(message_sent,converttostream(packet,JOIN),packet->length);		
	if (send(sockfd, message_sent, packet->length, 0) == -1) 
	{
		perror("send");
		exit(1);
	}
	free(message_sent);
	// receiving of ack and nack packets
	bzero(message_received,512);
	if ((nbytes = recv(sockfd, message_received, 512, 0)) <= 0) 
	{
		// got error or connection closed by client
		if (nbytes == 0) 
		{
			// connection closed
			printf("server: socket %d hung up\n", sockfd);
		} 
		else 
		{
			perror("recv");
		}
	} 
	else 
	{
		struct sbcp_packet *packet = convertfromstream(message_received);
		// Ack packet sent by server confirming the clients connection
		if(packet->type==7)
		{
			printf("server has successfully accepted the connection\n");
			printf("number of clients and the usernames who are currently available : %s\n",packet->payload->payload);
			fflush(stdout);
		}
		// Nak packet sent by server for rejecting the connection for this client
		if(packet->type==5)
		{	
			printf("server has rejected the connection due to : %s\n",packet->payload->payload);
			fflush(stdout);
			//close(sockfd) ... here client socket is already closed by the server.. in order to end client process enter "END"
		}
	}

	while (1)
	{
		FD_ZERO(&writefds);
		FD_ZERO(&readfds);
		
		// adding client socket to set of read and write fds
		FD_SET(sockfd,&readfds);
		FD_SET(sockfd,&writefds);
		// adding standard input to read fds
		FD_SET(STDIN_FILENO,&readfds);
		// checking which of the fds are ready for reading and writing
		if (select(sockfd+1, &readfds, &writefds, NULL, NULL) == -1) 
		{
			perror("select");
			exit(1);
		}
		// If user has entered input on the terminal
		if (FD_ISSET(STDIN_FILENO, &readfds))
		{	
			bzero(user_input,512);
			fgets(user_input,512,stdin);
			// checking if text entered by client exceeds the length of 512 bytes
			if (strlen(user_input) >= 512)
			{
				fprintf(stderr,"message should be less than 512 characters\n", argv[0]);
				exit(0);
			}
			// user enters END in order to end the connection
			if (user_input[0] == 'E' && user_input[1] == 'N' && user_input[2] == 'D')
			{
				close (sockfd);
				break;
			}
			// user enters some message to send to other clients
			else 
			{
				// creating a SEND packet to send the user message
				struct sbcp_packet *packet = create_packet(user_input,NULL,SEND);
				
				printf("Sending the message : %s",user_input);
				fflush(stdout);
				
				char *message_sent = (char *)malloc(packet->length*sizeof(char));
				if (message_sent == NULL)
				{
					fprintf(stderr,"failed to allocate memory\n");
					exit(0);
				}	
				
				memcpy(message_sent,converttostream(packet,SEND),packet->length);	
			
				if (FD_ISSET(sockfd, &writefds)) 
				{
					if (send(sockfd, message_sent, packet->length, 0) == -1) 
					{
						perror("send");
						exit(1);
					}
				}
				else
					printf("you cannot write now");
				free(message_sent);
			}
		}
		else
		{
			// Client reads messages sent by other clients
			if (FD_ISSET(sockfd, &readfds)) 
			{
				bzero(message_received,512);
				if ((nbytes = recv(sockfd, message_received, 512, 0)) <= 0) 
				{
					// got error or connection closed by client
					if (nbytes == 0) 
					{
						// connection closed
						printf("selectserver: socket %d hung up\n", sockfd);
						close(sockfd);
					} 
					else 
					{
						perror("recv");
					}
				} 
				else 
				{
					struct sbcp_packet *packet = convertfromstream(message_received);
					// message from other clients
					if (packet->type == 3)
					{
						printf("username : %s\n",packet->payload->payload);
						printf("message : %s\n",(packet->payload+1)->payload);
						fflush(stdout);
					}
					// message that a client has joined the chat
					else if (packet->type == 8)
					{
						printf("user %s is online\n",packet->payload->payload);
						fflush(stdout);
					}
					// message that client has left the chat
					else if (packet->type == 6)
					{
						printf("user %s is offline\n",packet->payload->payload);
						fflush(stdout);
					}
				}
			}
		}
	}
	return 0;
}

