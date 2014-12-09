#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <netdb.h>
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

// first column number of counts. 
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

// store information of each client
struct user_info
{
	char *username;
	int socket_fd;
};

// creates a sbcp packet corresponding to join,send etc. Here message1 and message2 indicate 2nd and 3rd columns of sbcp_attributes array.
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
		(packet->payload+i)->length = strlen(message) + 4 + 1;//here message contains no trailing enter character, 4 bytes for header unlike client
		packet->length += (packet->payload+i)->length;
		(packet->payload+i)->payload = (char *)malloc(strlen(message));
		if ((packet->payload+i)->payload == NULL)
		{
			fprintf(stderr,"failed to allocate memory\n");
			exit(0);
		}
		memcpy((packet->payload+i)->payload,message,strlen(message)+1);
		*((packet->payload+i)->payload + strlen(message)) = '\0';
		fflush(stdout);
	}
	return packet;
}

// Converts struct to character stream
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
	// All integers like type and length are converted to network byte order where as character streams are left as it is
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

// converting from character stream to struct
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

// check whether duplicate username is present among the list of the clients
int duplicate (struct user_info *users, char *username, int max_clients)
{
	int counter = 0;
	for (counter = 0;counter < max_clients; counter++)
	{
		if ((users+counter)->username != NULL)
		{
			if(strcmp((users+counter)->username,username) == 0)
				return 1;
		}
	}
	return 0;
}

// concatenation of list of usernames to be sent to client when he newly joins the chat
char * listofusernames(struct user_info *users, int max_clients)
{
	char *usernames_list = (char *)malloc(512);
	if (usernames_list == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}
	strcat(usernames_list,"clients list : ");
	int counter = 0;
	for (counter = 0;counter < max_clients; counter++)
	{
		if ((users+counter)->username != NULL)
		{
			strcat(usernames_list,(users+counter)->username);
			strcat(usernames_list,",");
		}
	}
	strcat(usernames_list," Number of clients = ");
	return usernames_list;
}

// when a message has to be forwarded to other clients this function is called. message1 and message2 contain either username or text message. message2 
// can be null depending upon the type of packet
void forwarding (char *message1, char *message2, int packet_type, int sockfd, int current_socket, fd_set master, int fdmax)
{
	int j = 0;
	for(j = sockfd; j <= fdmax; j++) 
	{
		// send to everyone!
		if (FD_ISSET(j, &master)) 
		{
			// except the sockfd and current client
			if (j != sockfd && j != current_socket)  
			{
				printf("sending the message to other clients\n");
				fflush(stdout);
				struct sbcp_packet *packet_fwd = create_packet(message1,message2,packet_type);
				char *message_fwd = (char *)malloc(packet_fwd->length*sizeof(char));
				if (message_fwd == NULL)
				{
					fprintf(stderr,"failed to allocate memory\n");
					exit(0);
				}	
				memcpy(message_fwd,converttostream(packet_fwd,packet_type),packet_fwd->length);

				if (send(j, message_fwd, packet_fwd->length, 0) == -1) 
				{
					perror("send");
				}
				free(message_fwd);
			}
		}
	}
}

int main( int argc, char *argv[] )
{
	int sockfd, newsockfd, portno, clilen;
	struct sockaddr_in serv_addr, cli_addr;
	char *message_received = (char *)malloc(sizeof(char)*540);
	if (message_received == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}	
	// checking number of arguments
	if (argc < 4) 
	{
		fprintf(stderr,"usage %s server_ip server_port max_clients\n", argv[0]);
		exit(0);
	}

	// First call to socket() function 
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) 
	{
		perror("ERROR opening socket");
		exit(1);
	}

	// initializing information about the users like their socket id and initial usernames are NULL
	struct user_info *users = (struct user_info*)malloc((sizeof(struct user_info))*(atoi(argv[3])+1));
	if (users == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}
	int counter = 0;
	for (counter = 0; counter <= atoi(argv[3]); counter ++)
	{	
		(users+counter)->socket_fd = sockfd + counter + 1;
		(users+counter)->username = NULL;
	}

	// Initialize socket structure 
	bzero((char *) &serv_addr, sizeof(serv_addr));
	portno = atoi(argv[2]);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
	serv_addr.sin_port = htons(portno);

	/* Now bind the host address using bind() call.*/  
	if (bind(sockfd, (struct sockaddr *) &serv_addr,
		          sizeof(serv_addr)) < 0)
	{
		 perror("ERROR on binding");
		 exit(1);
	}

	/* Now start listening for the clients, here process will
	* go in sleep mode and will wait for the incoming connection
	*/
	listen(sockfd,atoi(argv[3]));
	clilen = sizeof(cli_addr);

	fd_set master;
	fd_set read_fds;
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	int fdmax = 0, nbytes; //keeping track of maximum file descriptor
	FD_SET(sockfd,&master);
	fdmax = sockfd;
	int i,j;

	// keeping track of number of clients
	int number_of_clients = 0;

	for(;;)
	{
		//updating readfds
		read_fds = master;
		
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) 
		{
			perror("select");
			exit(1);
		}
		// assuming sockets are assigned with successive numbers
		for(i = sockfd; i <= fdmax; i++) 
		{
			if (FD_ISSET(i, &read_fds)) 
			{       // we got one!!
				// if its server socket
				if (i == sockfd) 
				{
					// handle new connections
					// Accept actual connection from the client 
					// sockfd will be in read state when there are any incoming connections
					newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr,&clilen); 
					if (newsockfd < 0) 
					{
						perror("ERROR on accept");
						exit(1);
					}
					else 
					{
						printf("The client with socket descriptor %d has been connected\n",newsockfd);
						number_of_clients++;
						FD_SET(newsockfd, &master); // add to master set
						if (newsockfd > fdmax) 
						{	// keep track of the max
						        fdmax = newsockfd;
						}
					}
				} 
				else 
				{
					bzero(message_received,540);
					// handle data from a client
					if ((nbytes = recv(i, message_received, 540, 0)) <= 0) 
					{
						// got error or connection closed by client
						if (nbytes == 0) 
						{
							// connection closed
							printf("selectserver: socket %d hung up\n", i);
							if ((users+i-sockfd-1)->username != NULL)
							{
								forwarding((users+i-sockfd-1)->username,NULL,OFFLINE,sockfd,i,master,fdmax);
								(users+i-sockfd-1)->username = NULL;
								number_of_clients--;
							}
						} 
						else 
						{
						        perror("recv");
						}
						close(i); // bye!
						FD_CLR(i, &master); // remove from master set
					} 
					else 
					{
                        printf("we got the message from the client\n");
						fflush(stdout);
						
						struct sbcp_packet *packet = convertfromstream(message_received);
						printf("Message from client %d : %s\n",i,packet->payload->payload);
						fflush(stdout);

						// checking whether the message sent by a client is JOIN
						
						if (packet->type == 2)
						{	
					         // checking max clients, if it exceeds creating a NAK packet and then converting to character stream
							if(number_of_clients > atoi(argv[3]))
							{
								struct sbcp_packet *packet_nak = create_packet("Number of clients exceeded",NULL,NAK);
								char *message_nak = (char *)malloc(packet_nak->length*sizeof(char));
								if (message_nak == NULL)
								{
									fprintf(stderr,"failed to allocate memory\n");
									exit(0);
								}	
								printf("Sending the message : %s\n",packet_nak->payload->payload);
								fflush(stdout);
								memcpy(message_nak,converttostream(packet_nak,NAK),packet_nak->length);
								if (send(i, message_nak, packet_nak->length, 0) == -1) 
								{
									perror("send");
								}
								free(message_nak);
								number_of_clients--;
								printf("Rejecting the client as number of connections exceeded\n");
								fflush(stdout);
							}
							// checking if there is any other user with duplicate names, if the name is duplicate then creating a nak packet and then converting to character stream
							else if(duplicate(users,packet->payload->payload,atoi(argv[3])))	
							{
								struct sbcp_packet *packet_nak = create_packet("username already exists",NULL,NAK);
								char *message_nak = (char *)malloc(packet_nak->length*sizeof(char));
								if (message_nak == NULL)
								{
									fprintf(stderr,"failed to allocate memory\n");
									exit(0);
								}	
								printf("Sending the message : %s\n",packet_nak->payload->payload);
								fflush(stdout);
								memcpy(message_nak,converttostream(packet_nak,NAK),packet_nak->length);
								if (send(i, message_nak, packet_nak->length, 0) == -1) 
								{
									perror("send");
								}
								free(message_nak);
								number_of_clients--;
								printf("Rejecting client as username already exists\n");
								fflush(stdout);
							}
							// if successful an ACK is sent to the user
							else
							{
								char * total_clients = (char *)malloc(sizeof(int));
								if (total_clients == NULL)
								{
									fprintf(stderr,"failed to allocate memory\n");
									exit(0);
								}
								sprintf(total_clients,"%d",(number_of_clients));
								// creating ACK packet
								struct sbcp_packet *packet_ack = create_packet(strcat(listofusernames(users,atoi(argv[3])),total_clients),NULL,ACK);
								char *message_ack = (char *)malloc(packet_ack->length*sizeof(char));	
								if (message_ack == NULL)
								{
									fprintf(stderr,"failed to allocate memory\n");
									exit(0);
								}
								printf("Sending the message : %s\n",packet_ack->payload->payload,(packet_ack->payload+1)->payload);
								fflush(stdout);
								// converting ACK packet to character stream
								memcpy(message_ack,converttostream(packet_ack,ACK),packet_ack->length);
								if (send(i, message_ack, packet_ack->length, 0) == -1) 
								{
									perror("send");
								}
								free(message_ack);
								// updating the information of new users
								(users+i-sockfd-1)->username = (char *)malloc(strlen(packet->payload->payload)+1);
								if ((users+i-sockfd-1)->username == NULL)
								{
									fprintf(stderr,"failed to allocate memory\n");
									exit(0);
								}
								memcpy((users+i-sockfd-1)->username,packet->payload->payload,strlen(packet->payload->payload)+1);
								printf("Accepting the client %s\n",(users+i-sockfd-1)->username);
								fflush(stdout);
								// messaging remaining clients that current user is online
								forwarding((users+i-sockfd-1)->username,NULL,ONLINE,sockfd,i,master,fdmax);
							}
						}
						// If a client has sent a message to be forwarded to other clients
						else if (packet->type == 4)
						{
							forwarding((users+i-sockfd-1)->username,packet->payload->payload,FWD,sockfd,i,master,fdmax);
						}
					}
				} // END handle data from client
			} // END got new incoming connection
		} // END looping through file descriptors
	}
	return 0;
}

