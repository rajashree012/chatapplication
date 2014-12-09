ECEN602 HW1 Programming Assignment
----------------------------------

Team Number: 12
Member 1 # Zhou, Shenmin (UIN: 823008644)
Member 2 # Polsani, Rajashree Rao (UIN: 223001584)
---------------------------------------

Description/Comments:
--------------------
1. client.c and server.c contains client and server codes respectively  
2. we have attempted feature 1 as part of bonus
3. In order to run on SUN machines the make file has to be changed :
	gcc -Wall   -o  client  client.c -lnsl -lsocket -lresolv
	gcc -Wall   -o  server  server.c -lnsl -lsocket -lresolv
4. In order to end the connection of client enter "END". In case the client is rejected by the server due to either duplicate username or due to exceeding of number of clients. END the client manually and then restart it.
5. In case of creation of sbcp packet for sending ACK message we have used second option where usernames of clients currently online and total number of clients are concatenated.
6. Brief description of the architecture :
	Intially server is started and listens for any available clients. The client is started which connects with the server by providing the username (JOIN). Once the TCP connection is established, depending on the duplicacy of username and present number of clients in the chat either a NAK or ACK is sent by the server. If the server has sent ACK then the client can send user messages. If any user has entered the chat then an ONLINE packet is sent to rest of the clients. Similarly in case a client leaves chat OFFLINE packet is sent.
7. Detailed comments are written in the code
8. As we have used character stream for communication it may not work with the server that was specified in the slides of recitation hours.

Unix command for starting server:
------------------------------------------
./server SERVER_IP SERVER_PORT MAX_CLIENTS

Unix command for starting client:
------------------------------------------
./client USERNAME SERVER_IP SERVER_PORT
