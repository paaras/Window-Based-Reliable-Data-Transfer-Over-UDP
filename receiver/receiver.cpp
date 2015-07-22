#include<iostream>
#include<vector>
#include<algorithm>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/stat.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<unistd.h>
#include<netdb.h>
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

bool cmpfunc(const Packet& a, const Packet& b){
	return a.seq < b.seq;
}

void timestamp() {
	time_t now = time(0);
	tm *ltm = localtime(&now);
	cout << setw(2) << setfill('0') << ltm->tm_hour << ":" << setw(2) << setfill('0') 
	     << ltm->tm_min << ":" << setw(2) << setfill('0') << ltm->tm_sec << " ";
}

int main(int argc, char* argv[]){	
	struct sockaddr_in server,client;
	unsigned int seq,slen,clen;
	struct hostent *hp;
	FILE *file;
	char buf[BUF_SIZE];
	int sock;
	if(argc != 4){
		cout << "Usage: <receiver> <sender_hostname> <sender_portnumber> <filename>" << endl;
		exit(1);
	}
	// initial socket
	sock = socket(AF_INET, SOCK_DGRAM,0);
	if(sock < 0) error("socket");
	server.sin_family = AF_INET;
	hp = gethostbyname(argv[1]);
        if(hp == 0) error("Unknown host");
        bcopy((char*)hp->h_addr, (char*) &server.sin_addr, hp->h_length);
        server.sin_port = htons(atoi(argv[2]));
        slen = sizeof(struct sockaddr_in);
	strcpy(buf,argv[3]);
	//strcat(buf,"_copy");
        int val = fcntl(sock,F_GETFL,0);
        fcntl(sock,F_SETFL, val | O_NONBLOCK);

	srand(time(NULL));
	bool end = false;
	bool request = false;
	bool reply = false;
	int PKT_SIZE = sizeof(Packet);
	unsigned int wseq = 0;
	vector<Packet> window;
	bool first = true;
	unsigned filesize = 0;
	while(!end){
		bzero(buf,PKT_SIZE);	
		if(!request || !reply){
			Packet p;
			p.type = QRY;
			strcpy(p.data,argv[3]);
			p.size = strlen(p.data);
			int nwritten = sendto(sock,&p, PKT_SIZE, 0,(struct sockaddr*)&server,slen);
			if(nwritten < 0) error("sendto");
			timestamp();
			cout << "--[Sending Request filename:" << p.data << "]--" <<endl;
			request = true;
		}
		int nread = recvfrom(sock,buf,PKT_SIZE, 0,(struct sockaddr*)&server,&slen);
		/*
		FD_ZERO(&read_fd);
		FD_SET(sock,&read_fd);
		tmv.tv_sec = 0;
		tmv.tv_usec = 200;
		int msg_num = select(sock+1,&read_fd,NULL,NULL,&tmv);
		int nread;		
		if(FD_ISSET(sock,&read_fd)) 
			nread = recvfrom(sock,buf,PKT_SIZE, 0,(struct sockaddr*)&server,&slen);
		*/
		if(nread < 0) continue;
		else if(nread == 0){
			 continue;
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
		Packet *pkt = (Packet*)buf;
		if(pkt->type == DATA){
			if(wseq == pkt->seq){
				if(first){
					file = fopen(argv[3],"wb");
					if(file == NULL) error("fopen");
					first = false;
				}
				int nwritten = fwrite(pkt->data,1,pkt->size,file);
				if(nwritten < 0 || nwritten != pkt->size) error("fwrite");
				fflush(file);
				wseq += pkt->size;
				Packet p;
				p.type = ACK;
				p.seq = wseq;
				p.size = 0;
				int pwritten = sendto(sock,&p,PKT_SIZE,0,(struct sockaddr*)&server,slen);
				if(pwritten < 0 ) error("sendto");
				timestamp();				
				cout << "--[Sending ACK# " << p.seq << "]--" << endl;
				if(window.size() > 0){
					for(int i = 0; i < window.size(); ++i){
						if(window[i].seq == wseq){
							int nw = fwrite(window[i].data,1,window[i].size,file);
							if(nw < 0 || nw != window[i].size) error("fwrite");
							fflush(file);
							wseq +=	window[i].size;
							Packet wp;
							wp.type = ACK;
							wp.seq = wseq;
							wp.size = 0;
							int pnw = sendto(sock, &wp, PKT_SIZE,0,(struct sockaddr*)&server,slen);
							if(pnw < 0) error("sendto");
							timestamp();
							cout << "--[Resending ACK# " << wp.seq << "]--" << endl;
						}
					}
					int wsize = window.size();
					for(int i = 0; i < wsize ; ++i){
						if(window[0].seq <= wseq) window.erase(window.begin());
						else break;
					}
				}
			}
			else if(wseq > pkt->seq){
				//resend ack
				Packet p;
				p.type = ACK;
				p.seq = pkt->seq + pkt->size;
				p.size = 0;
				int pwritten = sendto(sock,&p,PKT_SIZE,0,(struct sockaddr*)&server,slen);
				if(pwritten < 0) error("sendto");
				timestamp();				
				cout << "--[Resending ACK# " << p.seq << "]--" << endl;
			}
			else{//record and send ack
				bool dup = false;
				for(int i = 0; i < window.size() ; ++i){
					if(window[i].seq == pkt->seq){
						Packet p;
						p.type = ACK;
						p.seq = pkt->seq + pkt->size;
						p.size = 0;
						int pwritten = sendto(sock,&p,PKT_SIZE,0,(struct sockaddr*)&server,slen);
						if(pwritten < 0) error("sendto");
						timestamp();						
						cout << "--[Resending ACK# " << p.seq << "]--" << endl;
						dup = true;
						break;					
					}
				}
				if(!dup){
					Packet p;
					memcpy(&p,pkt,PKT_SIZE);
					window.push_back(p);
					sort(window.begin(),window.end(),cmpfunc);
					p.type = ACK;
					p.seq  = pkt->seq + pkt->size;
					p.size = 0;
					int pw = sendto(sock,&p,PKT_SIZE,0,(struct sockaddr*)&server,slen);
					if(pw < 0) error("sendto");
					timestamp();
					cout << "--[Sending ACK# " << p.seq << "]--" << endl;
				}
			}
			reply = true;
		}
		else if(pkt->type == FIN){
			timestamp();
			cout << "--[Transmission Complete!        ]--" << endl;
			filesize = pkt->size;
			pkt->type = FIN_ACK;
			int finw = sendto(sock,pkt,PKT_SIZE,0,(struct sockaddr*)&server,slen);
			if(finw < 0) error("sendto");
			timestamp();			
			cout << "--[Sending FIN-ACK  ]--" << endl;
			end = true;
		}
		else if(pkt->type == BAD){
			timestamp();			
			cout << "--[Requested file doesn't exists!]--" << endl;
			end = true;
		}
		else if(pkt->type == MSG){
			filesize = pkt->size;
			timestamp();			
			cout << "--[Requested file almost complete]--" << endl;
		}
		else{
			timestamp();			
			cout << "--[Unexpected packet received    ]--" << endl;
		}
	}
	fclose(file);
	return 0;
}
