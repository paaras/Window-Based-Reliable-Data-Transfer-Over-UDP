#include<iostream>
#include<vector>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/stat.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<unistd.h>
#include<ctime>
#include<iomanip>

using namespace std;

#define BUF_SIZE  1024
#define DATA_SIZE 1012

enum { QRY,ACK,DATA,FIN,FIN_ACK,BAD,MSG };

double ploss = 0.25;
double pcrpt = 0.05;
int    WIN_MAX  = 32;

typedef struct{
	int type;
	unsigned int seq;
	unsigned int size;
	char data[DATA_SIZE];
} Packet;

void error(const char *msg){
	perror(msg);
	exit(1);
}

static int loss(){
        double val = rand()%10000;
        val/=10000;
        if(val < ploss)return 1;
        return 0;
}

static int corrupt(){
        double val = rand()%10000;
        val/=10000;
        if(val < pcrpt)return 1;
        return 0;
}

void timestamp() {
	time_t now = time(0);
	tm *ltm = localtime(&now);
	cout << setw(2) << setfill('0') << ltm->tm_hour << ":" << setw(2) << setfill('0') 
	     << ltm->tm_min << ":" << setw(2) << setfill('0') << ltm->tm_sec << " ";
}

int main(int argc, char* argv[]){
	struct sockaddr_in server, client;
	unsigned int slen,clen;
	struct timeval tmv;
	struct stat    st;
	FILE *file;
	char buf[BUF_SIZE];
	unsigned int fseq;
	unsigned int fsize;
	int sock;
	fd_set read_fd;
	vector<Packet> window;
	vector<int> ack;
	int PKT_SIZE = sizeof(Packet);
	if(argc != 2){
		cout<< "Usage: <sender> <portnumber>" << endl;
	}
	// Initial server socket
	slen = sizeof(server);
	clen = sizeof(client);
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if( sock < 0 ) error("socket");
	memset(&server,0,slen);
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = INADDR_ANY;
        server.sin_port = htons(atoi(argv[1]));
        if( bind(sock, (struct sockaddr*) &server, slen)<0)
                error("binding");
        int val = fcntl(sock,F_GETFL,0);
        fcntl(sock,F_SETFL, val | O_NONBLOCK | O_NDELAY);
	int say_yes = 1;
	setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, (void*)&say_yes, sizeof(say_yes));
	setsockopt( sock, SOL_SOCKET, SO_OOBINLINE, (void*)&say_yes, sizeof(say_yes));
	bool start = false;
	bool end = false;
	bool bad = false;
	bool finackrcv = false;
	int timeouts = 0;
	srand(time(NULL));
	//main loop	
	while(!end){
		FD_ZERO(&read_fd);
		FD_SET(sock,&read_fd);
		tmv.tv_sec = 0;
		tmv.tv_usec = 10;
		int msg_num = select(sock+1,&read_fd,NULL,NULL,&tmv);
		if(msg_num == 0){//time out
			if(bad) end = true;
			if(finackrcv) end = true;
			if(start && ack.size() > 0){
				timestamp();
				cout << "--[Time out!]--" << endl;
				for(int i = 0; i < window.size(); ++i){
					if(ack[i]) continue;
					int nwritten = sendto(sock,&window[i],PKT_SIZE,0,(struct sockaddr*)&client,clen);
					if(nwritten < 0 ) error("sendto");
					timestamp();
					cout << "--[Resending packet#" << window[i].seq << "]--" <<endl;
				}
			}
			if(start && feof(file) && ack.size() == 0){
				timestamp();
				cout << "--[Time out!]--" << endl;
				timeouts++;
				Packet p;
				p.type = FIN;
				p.size = fsize;
				int nw = sendto(sock,&p,PKT_SIZE,0,(struct sockaddr*)&client,clen);
				if(nw < 0 || timeouts > 10 ){
					 end = 1;
					 timestamp();
					 cout << "--[Transmission Complete! and closing server]--" << endl;
					 continue;
				}
				timestamp();
				cout << "--[Resending FIN]--" << endl;
			}
			continue;
		}
		if(FD_ISSET(sock,&read_fd)){
			bzero(buf,BUF_SIZE);
			int nread = recvfrom(sock, buf, PKT_SIZE, 0, (struct sockaddr*)&client, &clen);
			if(nread <= 0){
				cout << "here!" << endl;
			}
			if(loss()){
				timestamp();
				cout << "--[Packet Loss!   ]--" << endl;
				continue;
			}
			if(corrupt()){
				timestamp();				
				cout << "--[Packet Corrupt!]--" << endl;
				continue;
			}
			if(nread > 0){
				Packet *pkt = (Packet*)buf;
				if(bad){
					pkt->type = BAD;
					int nwritten = sendto(sock,pkt,PKT_SIZE, 0, (struct sockaddr*)&client,clen);
					if(nwritten < 0 ) error("sendto");
					timestamp();					
					cout << "--[No such file exists]--" << endl;
					bad = true;
				}
				else if(pkt->type == QRY && !start){
					start = true;
					timestamp();					
					cout << "--[New Request File]--" << endl;
					file = fopen(pkt->data,"rb");
					if(file == NULL){ // if file not exists or access denied
						pkt->type = BAD;
						int nwritten = sendto(sock,pkt,PKT_SIZE, 0, (struct sockaddr*)&client,clen);
						if(nwritten < 0 ) error("sendto");
						timestamp();						
						cout << "--[No such file exists]--" << endl;
						bad = true;
						continue;
					}
					stat(pkt->data,&st);
					fsize = st.st_size;
					fseq = 0;
					for(int i = 0 ; i < WIN_MAX; ++i){
						Packet p;
						fseek(file,fseq,SEEK_SET);
						int nfread = fread(p.data, 1, DATA_SIZE,file);
						if(nfread < 0) error("sendto");
						else if(nfread > 0){
							p.type = DATA;
							p.seq  = fseq;
							p.size = nfread;
							fseq += p.size;
							int nw = sendto(sock,&p,PKT_SIZE,0,(struct sockaddr*)&client,clen);
							if(nw < 0) error("sendto");
							timestamp();
							cout << "--[Sending Packet#" << p.seq << " ]--" << endl; 
							window.push_back(p);
							ack.push_back(0);
						}
						else if(feof(file)){//read to end
							p.type = MSG;
							p.size = fsize;
							int nw = sendto(sock,&p,PKT_SIZE,0,(struct sockaddr*)&client,clen);
							if(nw < 0) error("sendto");
							timestamp();							
							cout << "--[Reach End of File]--" << endl;
							break;
						}
					}
				}
				else if(pkt->type == ACK){
					bool change = false;
					for(int i = 0; i < window.size(); ++i){
						if(window[i].seq + window[i].size == pkt->seq){
							ack[i] = 1;
							change = true;
							timestamp();							
							cout << "--[Received ACK#" << pkt->seq << "]--" << endl;
							break;
						}
					}
					if(!change){ //duplicate ACK
						timestamp();						
						cout << "--[Received duplicate ACK#" << pkt->seq << "]--" << endl;
					}
					else if(ack[0] == 1){ // move one step
						window.erase(window.begin());
						ack.erase(ack.begin());
						int count = 0;
						for(int i = 0; i < window.size(); ++i){
							if(ack[i] == 1)count++;
							else break;
						}
						for(int i = 0; i < count;++i){
							window.erase(window.begin());
							ack.erase(ack.begin());
						}
						if( window.size() < WIN_MAX && !feof(file)){
							for(int i = window.size(); i < WIN_MAX ; ++i){
								Packet p;
								fseek(file,fseq,SEEK_SET);
								int nfread = fread(p.data,1,DATA_SIZE,file);
								if(nfread < 0 ) error("sendto");
								else if(nfread > 0){
									p.type = DATA;
									p.seq  = fseq;
									p.size = nfread;
									fseq += p.size;
									int nw = sendto(sock,&p,PKT_SIZE,0,(struct sockaddr*)&client,clen);
									if(nw < 0) error("sendto");
									timestamp();								
									cout << "--[Sending Packet#" << p.seq << " ]--" << endl; 						
									window.push_back(p);
									ack.push_back(0);
								}
								else if(feof(file)){
									p.type = MSG;
									p.size = fsize;
									int nw = sendto(sock,&p,PKT_SIZE,0,(struct sockaddr*)&client,clen);
									if(nw <0 ) error("sendto");
									timestamp();								
									cout << "--[Reach End of File]--" << endl;
									break;
								}
							}
						}
					}
					if(feof(file) && ack.size() == 0){
						Packet p;
						p.type = FIN;
						p.size = fsize;
						int nw = sendto(sock,&p,PKT_SIZE,0,(struct sockaddr*)&client,clen);
						if(nw < 0 ) error("sendto");
						timestamp();
						cout << "--[Transmission Complete]--" << endl;
					}
				}
				else if(pkt->type == FIN_ACK){
					timestamp();					
					cout << "--[Transmission Complete and closing server]--" << endl;
					fclose(file);
					finackrcv = true;
					end;
				}
				else{
					timestamp();
					cout << "--[Unknown Packet!]--" << endl;
				}
			}
		}
	}
	return 0;
}
