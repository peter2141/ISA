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
#include <csignal>
#include <regex>
#include <locale> 

#define BUFSIZE  1024
#define QUEUE	2

using namespace std;

//premenna pre zaznamenie signalu SIGINT
static int volatile geci = 0;

//enum pre switch ktory indikuje stav
enum states {
	auth,
	trans,
	update
};

//enum pre switch pri spracovani prikazov
enum commands{
	user,
	pass,
	apop,
	stat,
	list,
	retr,
	dele,
	noop,
	rset,
	uidl,
	quit,
	notfound
};

// struktura pre premenne ktore sa maju predavat vlaknam
struct threadVar{ 
	int socket;
	bool crypt;
	string username;
	string password;

};


//konverzia retazcov stavu serveru na hodnoty v enum k pouzitiou v case
states hashState(string state){
	if(!state.compare("authentication")) return auth;
	if(!state.compare("transaction")) return trans;
	if(!state.compare("update")) return update;
}

//konverzia retazcov prikazov na hodnoty v enum k pouzitiou v case
commands hashCommand(string command){
	if (!command.compare("user")) return user;
	if (!command.compare("pass")) return pass;
	if (!command.compare("apop")) return apop;
	if (!command.compare("stat")) return stat;
	if (!command.compare("list")) return list;
	if (!command.compare("retr")) return retr;
	if (!command.compare("dele")) return dele;
	if (!command.compare("noop")) return noop;
	if (!command.compare("rset")) return rset;
	if (!command.compare("uidl")) return uidl;
	if (!command.compare("quit")) return quit;
	return notfound;
}



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
				cerr << "reset" << endl;//TODO reset
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
	
	
	threadVar vars;
	vars = *((threadVar*)arg);

	//lokalna premenna pre socket
	int acceptSocket;
	
	//sucket castujeme naspat na int
	acceptSocket = vars.socket;//*((int *) arg);
	
	cout << vars.username<< endl << vars.password << endl << vars.crypt << endl; 

	//vytvorenie uvitacej spravy
	char name[100];
	gethostname(name,100);
	string welcomeMsg = "+OK POP3 server ready <" + to_string(getpid()) + "." + to_string(time(NULL)) + "@"  + name + ">\r\n";


	//poslanie uvitacej spravy
	send(acceptSocket,welcomeMsg.c_str(),strlen(welcomeMsg.c_str()),0);

	//vypocitanie hashu
	/*string stringToHash = welcomeMsg + vars.password;
	unsigned char md5[MD5_DIGEST_LENGTH];
	MD5((unsigned char *)stringToHash.c_str(),stringToHash.size(),md5);
	

	//staci 32???
	char mdString[32];
 
   	for(int i = 0; i < 16; i++)
	sprintf(&mdString[i*2], "%02x", (unsigned int)md5[i]);
 	
 	string hash = mdString;
 	cout << hash << endl;
        */



	//premenna pre buffer
	char buff[BUFSIZE];
	
	string state = "authentication"; //string pre stav automatu

	//smycka pre zikavanie dat
	while (1)		
	{		
		bzero(buff,BUFSIZE);//vynulovanie buffera			
		int res = recv(acceptSocket, buff, BUFSIZE,0);//poziadavok ma max dlzku 255 bajtov spolu s CRLF		
		if (res > 0)
		{
			
			//vytvorenie string z Cstring kvoli jednoduchsej prace s retazcom
			string message = buff;
			

			//kontrola dlzky spravy
			if(message.size()>255){
				send(acceptSocket,"-ERR Too long command\r\n",strlen("-ERR Too long command\r\n"),0);
				continue;
			}

			string command="";//retazec pre prikaz 
			string argument="";//retazec pre argument
			
			// TODO osetrit picoviny (ak iba \n, atd) 
			if(message.empty()){
				continue;
			}

			if(message.size()<2){
				send(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"),0);
				continue;
			}


			//kontrola CRLF na konci
			if((message.find("\r\n")) != message.size()-2){
				send(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"),0);
				continue;
			}

			//odstraneni CRLF na konci(neni potreba)
			message.erase(message.length()-2,message.length());
			
			//rozdelenie prikazu a argumentu
			bool space = false;
			for (unsigned int i=0; i<(message.length()); i++)
  			{
  				//cout<< message.at(i);
  				if(i!=0 && (message.at(i) == ' ')){
  					if(space==true){
  						argument += message.at(i);
  					}
  					else{
  						space = true;
  					}
  					
  					continue;

  				}
  				if(!space){
  					command += message.at(i);
  				}
  				else{
  					argument += message.at(i);
  				}
    			
  			}


  			//konvertovanie prikazu do lowercase
  			locale loc;
  			string commandLow = "";
  			for(size_t i=0;i<command.length();i++){
  				commandLow += tolower(command[i],loc);
  			}

  			cout << command.size() << endl;
  			cout << argument.size() << endl;

  			cout << command  << endl;

  			cout<< argument << endl;//co s CRLF????

  			cout << commandLow << endl;

  			


			//kontrola ci ma prikaz spravny format
			/*if(!regex_match(buff,regex("^[a-zA-Z]+ {1}.*\n"))){
				//osetrit send
				send(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"),0);
				continue;
			}
				*/

  			//TODO zistit velkost emailov + poradie, ako? 


  			switch(hashState(state)){
  				case auth:
  					switch (hashCommand(commandLow)){
  						case user:
  							cout << "amma madafaka user" << endl;
  							break;
  						case pass:
  							break;
  						case apop:
  							break;
  						case noop://nerob nic
  							break;
  						default:
  							send(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"),0);
							break;
  					}
  					break;
				case trans:
					switch (hashCommand(commandLow)){
  						case list:
  							break;
  						case stat:
  							break;
  						case retr:
  							break;
  						case dele:
  							break;
  						case rset:
  							break;
  						case uidl:
  							break;
  						case noop://nerob nic
  							break;
  						case quit:
  							break;
  						default:
  							send(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"),0);
							break;

  					}
  					break;
				case update://vymazat deleted
					break;		
				default:
					break;
  			}



		}
        else if (res == 0) //ak sa klient odpoji -> odznacit subory na delete,odomknut zamok
        { 
            printf("Client disconnected\n");
			close(acceptSocket);						
			break;
        }
        else if (errno == EAGAIN) // == EWOULDBLOCK
        {
            continue;
        }
        else//ak chyba
        {
            perror("ERROR: recv");
            exit(EXIT_FAILURE);
        }
    }
    close(acceptSocket);
    return (NULL);
}

/*void print_md5_sum(unsigned char* md) {
    int i;
    for(i=0; i <MD5_DIGEST_LENGTH; i++) {
            printf("%02x",md[i]);
    }
}*/



// SIGINT handler
void signalHandler(int x)
{
	
	geci = 1;
	//exit(x);
}



int main(int argc, char **argv){
    //vytvorenie objektu pre spracovanie argumentov
    Arguments args;
    //kontrola parametrov
    args.parseArgs(argc,argv);
    //TODO kontrola zadanych ciest,suborov
    //TODO otestovat authfile+ nacitat
    //TODO otestovat maildir+podpriecinky


    threadVar tmp;//struktura pre premenne ktore je potrebne predat vlaknam
    tmp.username = "";
    tmp.password = "";




    FILE *f;
    f = fopen(args.authfile().c_str(),"r");
    if(f == NULL){
    	cerr << "Nespravny autentifikacny subor. " << endl;
    	exit(1); 
    }
    int ch;
    string tmpString="";
    int counter = 0;
	while ((ch = fgetc(f))  != EOF){
		counter++;
		tmpString += ch;
		if(counter == 11){//ak sa nacital 11. znak(tj. nasical sme string "username = ")
			if(tmpString == "username = "){//nacitali sme string username
				while((ch = fgetc(f)) != '\n'){//do konca riadku nacitame prihlasovacie meno
					tmp.username += ch;
				}
				tmpString = "";
			}
			else{
				cerr << "Invalidny autentifikacny subor" << endl;
				exit(1);
			}
		}
		else if(counter == 22){
			if(tmpString == "password = "){
				while((ch = fgetc(f)) != EOF){//do konca riadku nacitame prihlasovacie meno
					tmp.password += ch;
				}
				tmpString = "";
				break;

			}
			else{
				cerr << "Invalidny autentifikacny subor" << endl;
				exit(1);
			}
		}
		else if(counter > 22){//okrem "username = " a "password = " je tam aj nieco ine
			cerr << "Invalidny autentifikacny subor" << endl;
			exit(1);
		}
		else{
			continue;
		}
	}

    fclose(f);


 

    //RESET
	if(args.reset()){
		;//TODO -reset a pokracovanie
	}

	if(args.crypt()){
		tmp.crypt = args.crypt();
	}
    






	//nastavenie pre posluchajuci socket
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

	int flags = fcntl(listenSocket, F_GETFL, 0);
	if ((fcntl(listenSocket, F_SETFL, flags | O_NONBLOCK))<0){
		perror("ERROR: fcntl");
		exit(EXIT_FAILURE);								
	}


	//experiment md5 working on merlin
	//------------------------------------------------------
	/*unsigned char md5[MD5_DIGEST_LENGTH];
	string asd = "qwerty";
	MD5((unsigned char *)asd.c_str(),asd.size(),md5);
	
	char mdString[33];
 
    	for(int i = 0; i < 16; i++)
		sprintf(&mdString[i*2], "%02x", (unsigned int)md5[i]);
 
    	printf("md5 digest: %s\n", mdString);*/
    //alebo funkcia ntohl

    //------------------------------------------------------

	


	//priprava pre select
	fd_set set;
	FD_ZERO(&set);
	FD_SET(listenSocket, &set);

	//cyklus pre accept?? TODO 
	while(1){

		//select
		if (select(listenSocket + 1, &set, NULL, NULL, NULL) == -1){
			cerr << "Chyba pri select()." << endl;
		}


		if ((acceptSocket = accept(listenSocket, (struct sockaddr*)&client, &clientLen)) < 0){
			cerr << "Chyba pri pripajani." << endl;
		}

		//nastavime socket ako nelbokujuci
		int flags = fcntl(acceptSocket, F_GETFL, 0);
		if ((fcntl(acceptSocket, F_SETFL, flags | O_NONBLOCK))<0){
			cerr << "ERROR: fcntl" << endl;
			close(acceptSocket);							
		}

		//pridanie socketu do struktury
		tmp.socket = acceptSocket;
		
		//vytvorenie vlakna
		pthread_t myThread;

		if((pthread_create(&myThread, NULL, &doSth, &tmp)) != 0){
			cerr << "Chyba pri vytvarani vlakna" << endl;
			close(acceptSocket);
		}


	}

	//signal handler
	signal(SIGINT, signalHandler);


	//zatvorenie socketu
	close(listenSocket);

    exit(0);
  //  return 0;
}
