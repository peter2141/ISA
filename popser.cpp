#include <iostream>
#include <cstdlib>
#include <string>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <fstream>
#include <streambuf>
#include <sstream>
#include <fcntl.h>
#include <openssl/md5.h>

#define BUFSIZE  1024
#define QUEUE	2

using namespace std;

//trieda pre spracovanie argumentov
class Arguments{
	//privatne premenne
	long port1;
	string maildir1;
	string authfile1;
	bool crypt1=false;
	bool reset1=false;
	public:
		//metody pre zistenie hodnot privatnych premennych
		int port(){return (int)port1;}
		string maildir(){return maildir1;}
		string authfile(){return authfile1;}
		bool crypt(){return crypt1;}
		bool reset(){return reset1;}
		//metoda pre spracovanie argumentov
		void parseArgs(int argc, char **argv){
			//hladanie parametru -h(rezim 1)
			for(int i = 1; i < argc; i++){
				if(string(argv[i]) == "-h"){
					cout << "HELP" << endl;
					exit(0);
				}
			}
			//hladanie parametru -r(rezim 2)
			if(argc==2 && (string(argv[1])=="-r")){
				cerr << "reset" << endl;
				exit(0);
			}
			//klasicky rezim, kontrola povinnych parametrov a detekcia volitelnych a nepodporovanych
			else{
				//flagy pre getopt
				bool a = false;
				bool d = false;
				bool p = false;
				opterr = 0;
				int c;
				char* ptr = NULL; 
				//getopt pre spracovanie argumentov
				while((c=getopt(argc,argv,"a:d:p:cr")) != -1){
					switch (c){
						case 'a':
							a = true;
							authfile1 = string(optarg);
							break;
						case 'd':
							d = true;
							maildir1 = string(optarg);
							break;
						case 'p':
								p = true;
								//prevod retazca portu na cislo
								port1 = strtol(optarg,&ptr,10);
								break;
						case 'c':
							crypt1 = true;
							break;
						case 'r':
							reset1 = true;
							break;
						case '?':
							cerr << "Nespravne parametre. \nPre informacie o spusteni spustite program s prepinacom -h."<<endl;
							exit(1);
						default: 
							abort();
					}
				}
				//kontrola ci boli zadane povinne parametre
				if(!(a && d && p)){
					cerr << "Nespravne parametre. \nMusite zadat cislo portu, cestu k Maildir a autentifikacny subor.\nPre informacie o spusteni spustite program s prepinacom -h."<<endl;
					exit(1);
				}
				//kontrola spravneho portu
				if(*ptr != '\0'){
					cerr << "Chyba. Port obsahuje znak, moze obsahovat iba cisla!!!"<<endl;
					exit(1); 
				}
			}
		}
		
};

//funkcia pre vlakna=klienty
void* doSth(void *arg){
	









	//odpojime thread, netreba nanho cakat v hlavnom threade
	pthread_detach(pthread_self());
	//lokalna premenna pre socket
	int acceptSocket;
	//sucket castujeme anspat na int
	acceptSocket = *((int *) arg);
	//nastavime socket ako nelbokujuci
	int flags = fcntl(acceptSocket, F_GETFL, 0);
	if ((fcntl(acceptSocket, F_SETFL, flags | O_NONBLOCK))<0){
		perror("ERROR: fcntl");
		exit(EXIT_FAILURE);								
	}
	//premenna pre buffer
	char buff[BUFSIZE];
		//smycka
		for (;;)		
		{					
			int res = recv(acceptSocket, buff, BUFSIZE,0);				
			if (res > 0)
			{
				buff[res] = '\0';
				printf("%s",buff);					

			}
            else if (res == 0) 
            { 
                printf("INFO\n");
				close(acceptSocket);						
				break;
            }
            else if (errno == EAGAIN) // == EWOULDBLOCK
            {
                //printf(".");
                continue;
            }
            else
            {
                perror("ERROR: recv");
                exit(EXIT_FAILURE);
            }
        }
    return (NULL);
}

void print_md5_sum(unsigned char* md) {
    int i;
    for(i=0; i <MD5_DIGEST_LENGTH; i++) {
            printf("%02x",md[i]);
    }
}


int main(int argc, char **argv){
    //vytvorenie objektu pre spracovanie argumentov
    Arguments args;
    //kontrola parametrov
    args.parseArgs(argc,argv);
    //TODO sietova komunikacia,kontrola zadanych ciest,suborov
    
    cout << args.port() << endl;


    int listenSocket,acceptSocket;
    struct sockaddr_in server;
    struct sockaddr_in client;
    socklen_t clientLen=sizeof(client);

    memset(&server, 0, sizeof(server));
    memset(&client, 0, sizeof(client));

    //naplnenie struktury sockaddr_in
    server.sin_family = AF_INET;//ipv4
    server.sin_addr.s_addr = htonl(INADDR_ANY);//hocikto sa moze pripojit
    server.sin_port = htons(args.port());//port



    //cakanie na klientov
    if ((listenSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		cerr << "Chyba pri vytvarani socketu." << endl;
		exit(1);
    }

    //bind - priradenie adresy pre socket
	if (bind(listenSocket, (struct sockaddr *)&server, sizeof(server)) < 0){
		cerr << "Chyba pri bindovani socketu." << endl;
		exit(1);
	}    

	//cakanie na pripojenia
	if (listen(listenSocket, QUEUE) < 0){
		cerr << "Chyba pri nastaveni socketu na pasivny(funkcia listen())." << endl;
		exit(1);
	}


	//experimenty pre pid, time, md5
	cout << time(NULL) << endl;
	cout << getpid() << endl;	
	char name[100];
	getdomainname(name,100);//test on merlin, co ak neni domain name????
	cout << name << endl;

	//experiment md5
	unsigned char md5[MD5_DIGEST_LENGTH];
	string asd = "qwerty";
	MD5((unsigned char *)asd.c_str(),asd.size(),md5);
	
	char mdString[33];
 
    	for(int i = 0; i < 16; i++)
		sprintf(&mdString[i*2], "%02x", (unsigned int)md5[i]);
 
    	printf("md5 digest: %s\n", mdString);

	//string fasz(md5);
	//md5[MD5_DIGEST_LENGTH]='\0';
	//print_md5_sum(md5);	
//	cout << "hash: " << md5 << endl;


	//cyklus pre accept?? TODO 
	while(1){
		if ((acceptSocket = accept(listenSocket, (struct sockaddr*)&client, &clientLen)) < 0){
			cerr << "Chyba pri pripajani." << endl;
			exit(1);
		}

		//vytvorenie vlakna
		pthread_t myThread;
		if((pthread_create(&myThread, NULL, &doSth, &acceptSocket)) != 0){
			cerr << "Chyba pri vytvarani vlakna" << endl;
			exit(1);
		}


	}




	close(listenSocket);

    
    return 0;
}
