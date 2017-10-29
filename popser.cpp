#include <iostream>
#include <cstdlib>
#include <string>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
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
#include <mutex>
#include <dirent.h>
#include <ctime>
#include <list>
#include <vector>

#define BUFSIZE  1024
#define QUEUE	2

using namespace std;

//premenna pre zaznamenie signalu SIGINT
static int volatile geci = 0;
//mutex pre locknutie maildiru
mutex mailMutex;
int counter=0; //pre hashovanie
int threadcount = 0;

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
	staat,
	lst,
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
	string maildir;
};


//konverzia retazcov stavu serveru na hodnoty v enum k pouzitiou v case
states hashState(string state){
	if(!state.compare("authentication")){
		return auth;
	} 
	else if(!state.compare("transaction")){
		return trans;
	} 
	else{//(!state.compare("update")) return update;
		return update;
	}
}

//konverzia retazcov prikazov na hodnoty v enum k pouzitiou v case
commands hashCommand(string command){
	if (!command.compare("user")) return user;
	if (!command.compare("pass")) return pass;
	if (!command.compare("apop")) return apop;
	if (!command.compare("stat")) return staat;
	if (!command.compare("list")) return lst;
	if (!command.compare("retr")) return retr;
	if (!command.compare("dele")) return dele;
	if (!command.compare("noop")) return noop;
	if (!command.compare("rset")) return rset;
	if (!command.compare("uidl")) return uidl;
	if (!command.compare("quit")) return quit;
	return notfound;
}

string absolutePath(const char *filename){
	string path=realpath(filename,NULL);
	return path;
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
				ifstream resetIn;
				ifstream deleteIn;
				resetIn.open("reset.txt", ios::in);
				//kontrola ci existuje subor (da sa otvorit??)
				if ((resetIn.rdstate() & std::ifstream::failbit ) == 0 ){//ak existuje subor tak este nebol pouzity reset
					string filename;
					string tmppath;
					size_t pos;
					while(!resetIn.eof()){//kym neni eof
						getline(resetIn,filename);

						//ak EOF(obsahoval este \n ale getline to uz nacitalo takze testujeme tu)
						if ((resetIn.rdstate() & std::ifstream::eofbit ) != 0 ){
							break;
						}

						tmppath = filename;

						//prepis absolutnej cesty s /cur na /new
						pos = filename.rfind("/Maildir/cur/");//hladame posledny vyskyt cur--treba osetrovat vobec?
						string tmpfilename2 = filename;
						tmpfilename2.replace(pos,13,"/Maildir/new/");
						//presun suborov z cur do new
						int res = rename(filename.c_str(), tmpfilename2.c_str());
						if(res != 0){ // preco je chyba??
							//TODO spinavy hack(ak sa nepodarilo presunut tak subor je zmazany)
							continue;
							/*cerr << "chyba pri premenovani(prsune) z cur do new" << endl;
								//posunut vsetko naspat? 
							exit(1);*/
						}
						tmppath.erase(pos,string::npos);//vymazeme z cesty suboru vsetko od Maildir-ziskame cestu k maildiru, potreba pri mazani moznych suborov co sotali v cur
					}
					

					//odstranenie pomocnych suborov 
					if(remove("reset.txt")!=0){
						cerr << "Chyba pri mazani pomocneho suboru na ukladanie presunov z new do cur" << endl;
					}
					if(remove("info.txt")!=0){
						cerr << "Chyba pri mazani pomocneho suboru na ukladanie informacii o mailov" << endl;
					}
					if(remove("deleted.txt")!=0){
						cerr << "Chyba pri mazani pomocneho suboru na ukladanie mazanych suborov" << endl;
					}



					//odstranit vsetko ine z cur
					DIR * dir;
					struct dirent *file;
					string tmpdir = tmppath + "/Maildir/cur";//ak reset prazdny? nemoze byt prazdny!!!
					string tmpfilename;
					if((dir = opendir(tmpdir.c_str())) != NULL){
						while((file = readdir(dir)) != NULL){
							if(!strcmp(file->d_name,".") || !strcmp(file->d_name,"..") ){
								continue;
							}
							tmpfilename = tmpdir + "/"+ file->d_name;
							if(remove(tmpfilename.c_str())!=0){
								cerr << "Chyba pri mazani pomocneho suboru na ukladanie presunov z new do cur" << endl;
							}
						}
						closedir(dir);
					}
					else{//problem s cur priecinkom, ukoncime program
						cerr << "chyba pri otvarani priecinku cur" << endl;
						exit(1);
					}
				}

				resetIn.close();

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

bool isnumber(string number){
	char* ptr = NULL; 
	strtol(number.c_str(),&ptr,10);
	if(*ptr != '\0'){
		return false;
	}
	else{
		return true;
	}
}

//velkost zadaneho suboru
int fileSize(const char *file){
	FILE *f=NULL;
	f=fopen(file,"rb");
	fseek(f,0,SEEK_END);
	int size=ftell(f);
	fclose(f);
	return size;
}

//funckia pre zistenie virtualnej velkosti suboru
int getVirtualSize(string filename){
	int size = fileSize(filename.c_str());//zieskame velkost
	int linenumber = 0;
	string line;
	ifstream file;
	file.open(filename.c_str());//otvorime subor
	while(!file.eof()){//kym neni eof
		getline(file,line);

		//ak EOF(obsahoval este \n ale getline to uz nacitalo takze testujeme tu)
		if ((file.rdstate() & std::ifstream::eofbit ) != 0 ){
			break;
		}
		//pozrieme ci je tam \r
		if(line[line.length()-1] != '\r'){
			linenumber++;
		}
	}
	file.close();
	size += linenumber;
	return size;
}

void getFilesInCur(vector<string>& files,string maildir){
	DIR *tmpdir=NULL;
	struct dirent *tmpfile;
	string curdir = maildir + "/cur";
	tmpdir = opendir(curdir.c_str());
	while((tmpfile = readdir(tmpdir)) != NULL){
			if(!strcmp(tmpfile->d_name,".") || !strcmp(tmpfile->d_name,"..") ){
				continue;
			}
			else{
				//pridame nazov suboru do vectoru mien
				files.push_back(string(tmpfile->d_name));
			}

			
	}
	closedir(tmpdir);
}



void md5hash(string stringToHash, string& hash){
	unsigned char md5[MD5_DIGEST_LENGTH];
	MD5((unsigned char *)stringToHash.c_str(),stringToHash.size(),md5);
	//staci 32???
	char mdString[32];
   	for(int i = 0; i < 16; i++)
	sprintf(&mdString[i*2], "%02x", (unsigned int)md5[i]);
 	hash = mdString;
}

void createUIDL(char* filename, string& UIDL){
	string tmp = to_string(time(NULL)) + string(filename) + to_string(counter);
	md5hash(tmp,UIDL);
}



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
	

	//vytvorenie uvitacej spravy
	char name[100];
	gethostname(name,100);
	string welcomeMsg = "+OK POP3 server ready <" + to_string(getpid()) + "." + to_string(time(NULL)) + "@"  + name + ">\r\n";


	//poslanie uvitacej spravy
	send(acceptSocket,welcomeMsg.c_str(),strlen(welcomeMsg.c_str()),0);

	//vypocitanie hashu
	string stringToHash = welcomeMsg + vars.password;

 	string hash;

 	md5hash(stringToHash,hash);


	//premenna pre buffer
	char buff[BUFSIZE];
	
	string state = "authentication"; //string pre stav automatu

	bool userOK = false; //flag pre zistenie ci bol zadany spravny username

	//zoznam pre maily oznacene ako deleted v DELE, zmazanie v UPDATE
  	list<char*> tmpdel_list;
  	//vector pre subory v current - kvoli cislovanie mailov   	pridanie suborov v cur do vectoru,osetrit ci sa da otvorit cur??
  	vector<string> geciszopokurva;
  	

  	string username;//pre kontrolu username v pass

  	//struct timeval timeout;


  	//premenna pre meranie casu
  	long int timestamp=0;

	//------------------------------------------------------------------------------------------------------------------------------
	//HLAVNA SMYCKA
	//smycka pre zikavanie dat
	while (geci == 0)		
	{		
		//priprava pre select
		fd_set set;
		FD_ZERO(&set);
		FD_SET(acceptSocket, &set);

		//timeot pre select - 10 minut
		//timeout.tv_sec = 600;
  		//timeout.tv_usec = 0;




		//ak vyprsal timeout odpojime klienta
		/*int selret;
		selret = select(acceptSocket + 1, &set, NULL, NULL, &timeout);
		if(selret == -1){
			break;
		}
		else if (selret == 0){
			send(acceptSocket,"-ERR Timeout expired\r\n",strlen("-ERR Timeout expired\r\n"),0);
			close(acceptSocket);
			mailMutex.unlock();//?????? TODO
    		return (NULL);
		}*/


		/*if (select(acceptSocket + 1, &set, NULL, NULL, &timeout) == 0){


		 if (geci == 1){
		 	cout << "ending" << endl;
			mailMutex.unlock();//?????? TODO
    		return (NULL);
		 }
		}*/
	

		bzero(buff,BUFSIZE);//vynulovanie buffera			
		int res = recv(acceptSocket, buff, BUFSIZE,0);//poziadavok ma max dlzku 255 bajtov spolu s CRLF
		int sen; //pre kontrolu send		
		if (res > 0)
		{
			timestamp = 0;//vynulujeme casovac ak 
			//vytvorenie string z Cstring kvoli jednoduchsej prace s retazcom
			string message = buff;
			

			//kontrola dlzky spravy ????? aka max dlzka???
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

  			switch(hashState(state)){


  				case auth:{
  					switch (hashCommand(commandLow)){
  						

  						

  						case user://prisiel prikaz user
							if(vars.crypt){//bol zadany parameter -c, USER after USER moze byt

								send(acceptSocket,"+OK Hello my friend\r\n",strlen("+OK Hello my friend\r\n"),0);
								username = argument;
								userOK = true;
								break;

							}
							else{//nebola povolena autentifikacia USER - PASS
								send(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"),0);
								break;
							}
  							break;
  						case pass:{
  							if(userOK){
  								if(vars.password.compare(argument) == 0 && vars.username.compare(username) == 0){//dobre heslo a meno
  									//cout << geciszopokurva[0]<<endl;
  									send(acceptSocket,"+OK Correct password\r\n",strlen("+OK Correct password\r\n"),0);
  									if(!mailMutex.try_lock()){
  										send(acceptSocket,"-ERR Mailbox locked, try next time\r\n",strlen("-ERR Mailbox locked, try next time\r\n"),0);
  										close(acceptSocket);//odpojime klienta, alebo neodpajat????
  										threadcount--;
  										return(NULL);
  									}

  									getFilesInCur(geciszopokurva,vars.maildir);//ziskame subory v current, preto tu lebo iba teraz mozeme pristupovat k maildiru


  									bool firstRun = true;//kontrola prveho spustenia


  									struct stat buffer;   
  									if(stat("reset.txt", &buffer) == 0){
  										firstRun = false;
  									}
  									//vytvorime potrebne pomocne subory
  									if(firstRun){
  										ofstream resfile("reset.txt");
  										ofstream infoFile("info.txt");
  										ofstream deleted("deleted.txt");
  									}
  									ofstream resetFile;
  									ofstream infoFile;

  									infoFile.open("info.txt", std::ofstream::app);
  									resetFile.open("reset.txt",std::ofstream::app);

  									//presun z new do cur
  									DIR *dir=NULL;
									struct dirent *file;
									string tmpdir = vars.maildir + "/new";
									string tmpfilename1,tmpfilename2;	

									if((dir = opendir(tmpdir.c_str())) != NULL){
										while((file = readdir(dir)) != NULL){
											if(!strcmp(file->d_name,".") || !strcmp(file->d_name,"..") ){
												continue;
											}
											tmpfilename1 = tmpdir + "/"+ file->d_name;
											tmpfilename2 = vars.maildir + "/cur/" + file->d_name;
											if(rename(tmpfilename1.c_str(), tmpfilename2.c_str()) != 0){
												cerr << "chyba pri premenovani(prsune) z new do cur" << endl;
												close(acceptSocket);
			  									mailMutex.unlock();
			  									//posunut vsetko naspat? 
												exit(1);
											}

											//pridame nazov suboru do vectoru mien
											geciszopokurva.push_back(string(file->d_name));
											//pridanie nazvu a uidl do pomocneho suboru  
											infoFile <<file->d_name;
											infoFile << "/";

											//ziskame UIDl a pridame do suboru
											string uidl;
											createUIDL(file->d_name,uidl),
											infoFile << uidl;
											infoFile << "/";
											//pridame aj velkost suboru(virtualnu tj. s CRLF)
											infoFile << getVirtualSize(tmpfilename2)<<endl;
											//pridanie absolutnej cesty do suboru potrebneho k resetu
											resetFile << absolutePath(tmpfilename2.c_str()) << '\n';
											counter++;


										}
										closedir(dir);

 										
									}

									else{//problem s new priecinkom, ukoncime program
										cerr << "chyba pri otvarani priecinku cur" << endl;
										close(acceptSocket);
			  							mailMutex.unlock();
			  							threadcount--;
										exit(1);
									}	//presun do dalsieho stavu
									
									infoFile.close();
 									resetFile.close();

  									state = "transaction";
  									break;
  								}

  								else{//zle heslo
  									send(acceptSocket,"-ERR Wrong username or password\r\n",strlen("-ERR Wrong username or password\r\n"),0);
  									userOK = false;
  									break;
  								}
  							}//PASS mozno zadat iba pos spravnom USER
  							else{
  								send(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"),0);
								break;
  							}

  							break;
  						}
  						case apop:{
  							//porovnat s hashom
  							if(userOK){//po user bol zadany apop  - to je chyba
  								send(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"),0);
								break;
  							}
  							//pridanie username k hashu
  							string apopstr = vars.username + " " + argument;
  							if(hash.compare(apopstr) == 0){
  								send(acceptSocket,"+OK Correct password\r\n",strlen("+OK Correct password\r\n"),0);
								if(!mailMutex.try_lock()){
									send(acceptSocket,"-ERR Mailbox locked, try next time\r\n",strlen("-ERR Mailbox locked, try next time\r\n"),0);
									close(acceptSocket);//odpojime klienta, alebo neodpajat????
									threadcount--;
									return(NULL);
								}
								//kontrola ci existuje subor reset
								FILE *f;
								bool firstRun = true;//kontrola prveho spustenia
								if((f=fopen("reset.txt","r")) != NULL){
									firstRun = false;
								}

								
								ofstream resetFile;
								ofstream infoFile;
								//vytvorime potrebne pomocne subory
								if(firstRun){
									ofstream resfile("reset.txt");
									ofstream infoFile("info.txt");
									ofstream deleted("deleted.txt");
								}

								infoFile.open("info.txt", std::ofstream::app);
								resetFile.open("reset.txt",std::ofstream::app);
								
								//presun z new do cur
								DIR *dir=NULL;
								struct dirent *file;
								string tmpdir = vars.maildir + "/new";
								string tmpfilename1,tmpfilename2;

								

								if((dir = opendir(tmpdir.c_str())) != NULL){
									while((file = readdir(dir)) != NULL){
										if(!strcmp(file->d_name,".") || !strcmp(file->d_name,"..") ){
											continue;
										}
										tmpfilename1 = tmpdir + "/"+ file->d_name;
										tmpfilename2 = vars.maildir + "/cur/" + file->d_name;
										if(rename(tmpfilename1.c_str(), tmpfilename2.c_str()) != 0){
											cerr << "chyba pri premenovani(prsune) z new do cur" << endl;
											close(acceptSocket);
		  									mailMutex.unlock();
		  									//posunut vsetko naspat? 
											exit(1);
										}
										//pridame nazov suboru do vectoru mien
										geciszopokurva.push_back(file->d_name);
										//pridanie nazvu a uidl do pomocneho suboru  
										infoFile <<file->d_name;
										infoFile << "/";
										infoFile << "UIDL" << endl;//TODO vytvroenie a ziskanie UIDL
										resetFile << absolutePath(tmpfilename2.c_str()) << '\n';

									}

									closedir(dir);

									infoFile.close();
									resetFile.close();
								}
								else{//problem s new priecinkom, ukoncime program
									cerr << "chyba pri otvarani priecinku cur" << endl;
									close(acceptSocket);
		  							mailMutex.unlock();
									exit(1);
								}	//presun do dalsieho stavu
									
								state = "transaction";
								//TODO move from new to cur
								break;
  							}
  							else{
  								send(acceptSocket,"-ERR Wrong MD5 hash\r\n",strlen("-ERR Wrong MD5 hash\r\n"),0);
  								break;
  							}
  							break;
  							}
  						case noop://nerob nic
  							if(argument.compare("")){//noop neberie argumenty
  								send(acceptSocket,"-ERR Noop does not take arguments\r\n",strlen("-ERR Noop does not take arguments\r\n"),0);
								break;
  							}
  							send(acceptSocket,"+OK\r\n",strlen("+OK\r\n"),0);
  							break;
  						case quit://udpojime klienta
  							if(argument.compare("")){//quit neberie argumenty
  								send(acceptSocket,"-ERR Quit does not take arguments\r\n",strlen("-ERR Quit does not take arguments\r\n"),0);
								break;
  							}
  							send(acceptSocket,"+OK Server signing off\r\n",strlen("+OK Server signing off\r\n"),0);
  							close(acceptSocket);
  							mailMutex.unlock();
  							threadcount--;
  							return(NULL);
  						default:
  							send(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"),0);
							break;
  					}
  					break;

  				}
				case trans:{

					switch (hashCommand(commandLow)){
  						case lst:{
   							if(!argument.compare("")){//nebol zadany parameter, tj poslat informaciu o vsetkych spravach
  								DIR *d = NULL;
  								string tmpdir = vars.maildir + "/cur";
  								int msgcnt = 0;//premenn pra cyklus(indexovanie vo vectoru nazvov suborov)
  								int msgnum = 1;//premenna pre cislo sprav
  								int msgsen = 0;//pocet mailov v maildiru(ktore nie su uznacene na mazanie)
  								//stringy pre poslanie odpovede
  								string answ = "";
  								string tmpAnsw = "";
  								bool listOK = true;//flag
  								int sizesum = 0;//pocet bajtov
   								if((d = opendir(tmpdir.c_str())) != NULL){									
   									while(msgcnt < (int)geciszopokurva.size()){//prejdeme vsetky subory ktore sa nachadzaju v current
										
										//iteracia cez list, kontrola ci uz dany subor bol oznaceny na mazanie
										for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
										    if(!geciszopokurva[msgcnt].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
												listOK = false;
												break;
										    }
										}

		  								if(listOK){//ak cislo v poriadku
	  										int tmpsize;
	  										char filename[256];
	  										string line;
	  										ifstream file;
	  										file.open("info.txt");		

	  										while(!file.eof()){//kym neni eof
												getline(file,line);
												//ak EOF(obsahoval este \n ale getline to uz nacitalo takze testujeme tu)
												if ((file.rdstate() & std::ifstream::eofbit ) != 0 ){
													break;
												}
												sscanf(line.c_str(),"%[^/]/%*[^/]/%d",filename,&tmpsize);//nacitame nazov a velkost suboru
												if(!geciszopokurva[msgcnt].compare(filename)){//ak sme nasli subor 
													break; // v tmpsize mame velkost daneho suboru
												}
											}
	  										file.close();
		  									sizesum += tmpsize;
		  									msgsen++;
		  									tmpAnsw.append(to_string(msgnum)+ " " + to_string(tmpsize) + "\r\n");//pridavame do odpovede riadok pre danu spravu
		  								}

		  								msgnum++;
		  								msgcnt++;
		  								listOK = true;
		  							}
		  							answ = "+OK " + to_string(msgsen) + " messages (" + to_string(sizesum) + " octets)\r\n";//prvy riadok odpovede
		  							tmpAnsw.append(".\r\n");//ukoncenie spravy
		  							answ.append(tmpAnsw);//spojenie 1. riadku a tela
		  							send(acceptSocket,answ.c_str(),strlen(answ.c_str()),0);//odosielanie
  									closedir(d);
	  							}
	  							else{//problem s cur priecinkom, ukoncime program
	  								cerr << "chyba pri otvarani priecinku cur" << endl;
	  								close(acceptSocket);
	  								mailMutex.unlock();
	  								threadcount--;
	  								exit(1);
	  							}	
  							}
  							else{//bol zadany argument
  								//kontrola ci argument je cislo
  								if(!isnumber(argument)){
  									send(acceptSocket,"-ERR Not valid number\r\n",strlen("-ERR Not valid number\r\n"),0);
  									break;
  								}
  								int msgnum = stoi(argument,nullptr,10);//TODO osetrit - ak neni cislo tak error
  								msgnum -= 1;//kvoli indexovaniu vo vectore 
  								DIR *d = NULL;
  								string tmpdir = vars.maildir + "/cur";
  								bool listOK = true;

  								if((d = opendir(tmpdir.c_str())) != NULL){									
									if(msgnum >= (int)geciszopokurva.size() || msgnum < 0){//kontrola intervalu 
	  									send(acceptSocket,"-ERR Messege does not exists\r\n",strlen("-ERR Messege does not exists\r\n"),0);
										closedir(d);
										break;
	  								}
	  								else{

										//iteracia cez list, kontrola ci uz dany subor bol oznaceny na mazanie
										for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
										    if(!geciszopokurva[msgnum].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
										    	send(acceptSocket,"-ERR Messege already deleted\r\n",strlen("-ERR Messege already deleted\r\n"),0);
												listOK = false;
												break;
										    }
										}

		  								if(listOK){//ak cislo v poriadku
	  										int tmpsize;
	  										char filename[256];
	  										string line;
	  										ifstream file;
	  										file.open("info.txt");		

	  										while(!file.eof()){//kym neni eof
												getline(file,line);
												//ak EOF(obsahoval este \n ale getline to uz nacitalo takze testujeme tu)
												if ((file.rdstate() & std::ifstream::eofbit ) != 0 ){
													break;
												}
												sscanf(line.c_str(),"%[^/]/%*[^/]/%d",filename,&tmpsize);//nacitame nazov a velkost suboru
												if(!geciszopokurva[msgnum].compare(filename)){//ak sme nasli subor 
													break; // v tmpsize mame velkost daneho suboru
												}
											}
	  										file.close();
		  									string tmpAnsw = "+OK " + to_string(++msgnum)+ " " + to_string(tmpsize) + "\r\n";
		  									send(acceptSocket,tmpAnsw.c_str(),strlen(tmpAnsw.c_str()),0);
		  								}
	  								}

  									closedir(d);
	  							}
	  							else{//problem s cur priecinkom, ukoncime program
	  								cerr << "chyba pri otvarani priecinku cur" << endl;
	  								close(acceptSocket);
	  								mailMutex.unlock();
	  								threadcount--;
	  								exit(1);
	  							}	
  							}
  							break;
  						}

  						case staat:{
  							// https://stackoverflow.com/questions/612097/how-can-i-get-the-list-of-files-in-a-directory-using-c-or-c
  							DIR *d = NULL;
  							//struct dirent *file;
  							string tmpdir = vars.maildir + "/cur";
  							int totalsize = 0;
  							int count = 0;
  							string tmpfilename;
  							if((d = opendir(tmpdir.c_str())) != NULL){
  								int tmpsize;
  								char filename[256];
  								string line;
  								ifstream file;
  								file.open("info.txt");
  								bool wasdeleted = false;
  								//ziskame velkosti suborov
  								while(!file.eof()){//kym neni eof
									getline(file,line);

									//ak EOF(obsahoval este \n ale getline to uz nacitalo takze testujeme tu)
									if ((file.rdstate() & std::ifstream::eofbit ) != 0 ){
										break;
									}

									//rozparsujeme riadok z info suboru, ziskame nazov a velkost jednotlivych suborov
									sscanf(line.c_str(),"%[^/]/%*[^/]/%d",filename,&tmpsize);


									//kontrola ci subor je oznaceny ako mazany
									for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
									    if(!strcmp(filename,*iterator)){//ak mail bol uz oznaceny na mazanie
									    	wasdeleted = true;
									    	break;
									    }
									}
									//kontorla ci subor uz nebol zmazany nahodou
									ifstream delfile;
									delfile.open("deleted.txt");
									while(!delfile.eof()){
										getline(delfile,line);
										if ((delfile.rdstate() & std::ifstream::eofbit ) != 0 ){
											break;
										}
										if(!strcmp(line.c_str(),filename)){//subor uz bol zmazany
											wasdeleted = true;
											break;
										}
									}
									delfile.close();
									//ak bol oznaceny na mazanie alebo zmazany tak sa nepripocita jeho velkost a nepripocitava sa ani do poctu mailov
									if(wasdeleted){
										wasdeleted = false;
										continue;
									}
									else{
										count++;
										totalsize += tmpsize;
									}
									
								}
								file.close();
								closedir(d);
								string tmpAnsw = "+OK " + to_string(count) + " " + to_string(totalsize) + "\r\n";
  								send(acceptSocket,tmpAnsw.c_str(),strlen(tmpAnsw.c_str()),0);
								break;

  							}

  							else{//problem s cur priecinkom, ukoncime program
  								cerr << "chyba pri otvarani priecinku cur" << endl;
  								close(acceptSocket);
  								mailMutex.unlock();
  								threadcount--;
  								exit(1);
  							}	
  							break;
  						}

  						case retr:{
  							//retr ma povinny argument cislo spravy
  							if(!argument.compare("")){//retr ma povinny argument
  								send(acceptSocket,"-ERR Retr need message number\r\n",strlen("-ERR Retr need message number\r\n"),0);
								break;
  							}
  							//kontrola ci argument je cislo
							if(!isnumber(argument)){
								send(acceptSocket,"-ERR Not valid number\r\n",strlen("-ERR Not valid number\r\n"),0);
								break;
							}
  							DIR *d = NULL;
  							int msgnum = stoi(argument,nullptr,10);//TODO osetrit - ak neni cislo tak error
  							msgnum -= 1;//kvoli indexovaniu vo vectore 
  							string tmpdir = vars.maildir + "/cur";
  							int filesize = 0;
  							string tmpfilename;
  							string msg = "";
  							string tmpline;
  							bool retrOK = true;
  							if((d = opendir(tmpdir.c_str())) != NULL){
  								
   								if(msgnum >= (int)geciszopokurva.size() || msgnum < 0){//osetrenie cisla mailu
  									send(acceptSocket,"-ERR Messege does not exists\r\n",strlen("-ERR Messege does not exists\r\n"),0);
									break;
  								}

  								else{

  									tmpfilename = tmpdir + "/" + geciszopokurva[msgnum];
									//kontrola ci sprava bola mazana

									for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
									    if(!geciszopokurva[msgnum].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
									   	send(acceptSocket,"-ERR Messege already deleted\r\n",strlen("-ERR Messege already deleted\r\n"),0);
									    	retrOK = false;//flag pre oznacenie chyby 
											break;
									    }
									}
									//ziskanie velkosti -NIEE, ziskat z infor file!!! TODO
									filesize = getVirtualSize(tmpfilename.c_str());
									ifstream f(tmpfilename.c_str());
									//nacitanie emailu
									while(!f.eof()){
										getline(f,tmpline);
										if ((f.rdstate() & std::ifstream::eofbit ) != 0 ){
											break;
										}
										//kontrola ci je na zaciatku riadku bodka, ak ano tak pridame dalsie(byte-stuff)
										if(tmpline[0] == '.'){
											tmpline.insert(0, 1, '.');
										}



										//appendovanie do msg
										msg.append(tmpline);
										if(tmpline[tmpline.length()-1] != '\r'){//ak riadok bol zakonceny iba s /n
											msg += "\r\n";
										}
										else{//ak riadok bol nzakonceny s /r/n  tak pridame iba /n
											msg += "\n";
										}
										
									}
									f.close();
	  								
	  								if(retrOK){//ak vsetko prebehlo v poriadku
	  									  	string tmpAnsw = "+OK " + to_string(filesize) + " octets\r\n" + msg + ".\r\n"; //osetrit ten koniec, podla IMF, preksumat imf TODO
			  								send(acceptSocket,tmpAnsw.c_str(),strlen(tmpAnsw.c_str()),0);
											break;
  									}
  								}
  								closedir(d);

  							}
  							else{//problem s cur priecinkom, ukoncime program
  								cerr << "chyba pri otvarani priecinku cur" << endl;
  								close(acceptSocket);
  								mailMutex.unlock();
  								threadcount--;
  								exit(1);
  							}	
  							break;
  						}

  						case dele:{
  							if(!argument.compare("")){//dele ma povinny argument
  								send(acceptSocket,"-ERR Enter message number you want to delete\r\n",strlen("-ERR Enter message number you want to delete\r\n"),0);
								break;
  							}
  							//kontrola ci argument je cislo
							if(!isnumber(argument)){
								send(acceptSocket,"-ERR Not valid number\r\n",strlen("-ERR Not valid number\r\n"),0);
								break;
							}
  							DIR *d = NULL;
  							bool delOK = true;
  							int msgnum = stoi(argument,nullptr,10);//TODO osetrit
  							msgnum -= 1;
  							string tmpdir = vars.maildir + "/cur";
  							if((d = opendir(tmpdir.c_str())) != NULL){
								if(msgnum >= (int)geciszopokurva.size() || msgnum < 0){//kontrola intervalu 
  									send(acceptSocket,"-ERR Messege does not exists\r\n",strlen("-ERR Messege does not exists\r\n"),0);
									closedir(d);
									break;
  								}
  								else{
									//iteracia cez list, kontrola ci uz dany subor bol oznaceny na mazanie
									for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
									    if(!geciszopokurva[msgnum].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
									    	send(acceptSocket,"-ERR Messege already deleted\r\n",strlen("-ERR Messege already deleted\r\n"),0);
											delOK = false;
											break;
									    }
									}

										
									
	  								if(delOK){//posleme ok ak bol subor oznaceny ako mazany(kvoli tomu aby tato hlaska sa neposielala pri chybe)
	  									tmpdel_list.push_back((char*)geciszopokurva[msgnum].c_str());		
	  									string tmpAnsw = "+OK msg deleted\r\n";
	  									send(acceptSocket,tmpAnsw.c_str(),strlen(tmpAnsw.c_str()),0);
	  								}
  								}

  								closedir(d);
  							}
  							else{//problem s cur priecinkom, ukoncime program
  								cerr << "chyba pri otvarani priecinku cur" << endl;
  								close(acceptSocket);
  								mailMutex.unlock();
  								threadcount--;
  								exit(1);
  							}	
  							break;
  						}
  						case rset:
  							tmpdel_list.clear();
  							send(acceptSocket,"+OK\r\n",strlen("+OK\r\n"),0);
  							break;
  						case uidl:{//podobne k listu len vraciame ine veci
  							if(!argument.compare("")){//nebol zadany parameter, tj poslat informaciu o vsetkych spravach
  								DIR *d = NULL;
  								string tmpdir = vars.maildir + "/cur";
  								int msgcnt = 0;//premenn pra cyklus(indexovanie vo vectoru nazvov suborov)
  								int msgnum = 1;//premenna pre cislo sprav
  								string answ = "";
  								string tmpAnsw = "";
  								bool uidlOK = true;//flag
   								if((d = opendir(tmpdir.c_str())) != NULL){									
   									while(msgcnt < (int)geciszopokurva.size()){//prejdeme vsetky subory ktore sa nachadzaju v current
										
										//iteracia cez list, kontrola ci uz dany subor bol oznaceny na mazanie
										for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
										    if(!geciszopokurva[msgcnt].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
												uidlOK = false;
												break;
										    }
										}

		  								if(uidlOK){//ak cislo v poriadku
	  										char filename[256];
	  										char uidl[256];//osetrit ze negenerujem taky dlhe uidl TODO
	  										string line;
	  										ifstream file;
	  										file.open("info.txt");		
	  										while(!file.eof()){//kym neni eof
												getline(file,line);
												//ak EOF(obsahoval este \n ale getline to uz nacitalo takze testujeme tu)
												if ((file.rdstate() & std::ifstream::eofbit ) != 0 ){
													break;
												}
												sscanf(line.c_str(),"%[^/]/%[^/]/%*d",filename,uidl);//nacitame nazov a velkost suboru
												if(!geciszopokurva[msgcnt].compare(filename)){//ak sme nasli subor 
													break; // v uidl mame uidl
												}
											}
	  										file.close();
		  									tmpAnsw.append(to_string(msgnum)+ " " + string(uidl) + "\r\n");//pridavame do odpovede riadok pre danu spravu
		  								}

		  								msgnum++;
		  								msgcnt++;
		  								uidlOK = true;
		  							}
		  							answ = "+OK\r\n";
		  							tmpAnsw.append(".\r\n");//ukoncenie spravy
		  							answ.append(tmpAnsw);//spojenie 1. riadku a tela
		  							send(acceptSocket,answ.c_str(),strlen(answ.c_str()),0);//odosielanie
  									closedir(d);
	  							}
	  							else{//problem s cur priecinkom, ukoncime program
	  								cerr << "chyba pri otvarani priecinku cur" << endl;
	  								close(acceptSocket);
	  								mailMutex.unlock();
	  								threadcount--;
	  								exit(1);
	  							}	
  							}
  							else{//bol zadany argument
  								//kontrola ci argument je cislo
  								if(!isnumber(argument)){
  									send(acceptSocket,"-ERR Not valid number\r\n",strlen("-ERR Not valid number\r\n"),0);
  									break;
  								}
  								int msgnum = stoi(argument,nullptr,10);//TODO osetrit - ak neni cislo tak error
  								msgnum -= 1;//kvoli indexovaniu vo vectore 
  								DIR *d = NULL;
  								string tmpdir = vars.maildir + "/cur";
  								bool deleOK = true;

  								if((d = opendir(tmpdir.c_str())) != NULL){									
									if(msgnum >= (int)geciszopokurva.size() || msgnum < 0){//kontrola intervalu 
	  									send(acceptSocket,"-ERR Messege does not exists\r\n",strlen("-ERR Messege does not exists\r\n"),0);
										closedir(d);
										break;
	  								}
	  								else{

										//iteracia cez list, kontrola ci uz dany subor bol oznaceny na mazanie
										for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
										    if(!geciszopokurva[msgnum].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
										    	send(acceptSocket,"-ERR Messege already deleted\r\n",strlen("-ERR Messege already deleted\r\n"),0);
												deleOK = false;
												break;
										    }
										}

		  								if(deleOK){//ak cislo v poriadku
	  										char filename[256];
	  										char uidl[256];
	  										string line;
	  										ifstream file;
	  										file.open("info.txt");		

	  										while(!file.eof()){//kym neni eof
												getline(file,line);
												//ak EOF(obsahoval este \n ale getline to uz nacitalo takze testujeme tu)
												if ((file.rdstate() & std::ifstream::eofbit ) != 0 ){
													break;
												}
												sscanf(line.c_str(),"%[^/]/%[^/]/%*d",filename,uidl);//nacitame nazov a velkost suboru
												if(!geciszopokurva[msgnum].compare(filename)){//ak sme nasli subor 
													break; // v tmpsize mame velkost daneho suboru
												}
											}
	  										file.close();
		  									string tmpAnsw = "+OK " + to_string(++msgnum) + " " + string(uidl) + "\r\n";
		  									send(acceptSocket,tmpAnsw.c_str(),strlen(tmpAnsw.c_str()),0);
		  								}
	  								}

  									closedir(d);
	  							}
	  							else{//problem s cur priecinkom, ukoncime program
	  								cerr << "chyba pri otvarani priecinku cur" << endl;
	  								close(acceptSocket);
	  								mailMutex.unlock();
	  								exit(1);
	  							}	
  							}
  							break;
  						}
  						case noop://nerob nic
  							send(acceptSocket,"+OK\r\n",strlen("+OK\r\n"),0);
  							break;
  						case quit: 
  							send(acceptSocket,"+OK Server signing off\r\n",strlen("+OK Server signing off\r\n"),0);
  							state = "update";
  							break;
  						default:
  							send(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"),0);
							break;
  					}
  					if(state.compare("update")){
  						break;
  					}
  					
  				}
				case update:{//vymazat deleted
					string filename;
					ofstream deleted;
					deleted.open("deleted.txt", std::ofstream::app);
					for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {//prejdeme vsetky oznacene subory
						//pridame nazov suboru do suboru s mazanymi mailmi
						deleted << *iterator << endl;
						filename = vars.maildir + "/cur/" + *iterator;
						if(remove(filename.c_str())!=0){//vymazeme subor
							cerr << "Chyba pri mazani mailu" << endl;
						}
					}
					deleted.close();
					close(acceptSocket);//odpojime klienta,//premiestnit do quit?/
					mailMutex.unlock();
					threadcount--;
					return(NULL);
					break;	
				}	
				default:
					break;
  			}



		}
        else if (res == 0) //ak sa klient odpoji -> odznacit subory na delete,odomknut zamok
        { 
            printf("Client disconnected\n");
            mailMutex.unlock();						
			break;
        }
        else if (errno == EAGAIN) // == EWOULDBLOCK
        {
        	if(timestamp == 0){
        		timestamp = (long int)time(NULL);//ak bol nulovany pridame cas ked prvy krat sa neblokovalo tj neprislo nic
        	}
        	long int tmptime = (long int)time(NULL) - timestamp; //pozrieme rozdiel casu
            if(tmptime >= 600){//ak preslo 10 minut
            	send(acceptSocket,"-ERR Timeout expired\r\n",strlen("-ERR Timeout expired\r\n"),0);
				close(acceptSocket);
				mailMutex.unlock();//?????? TODO
				threadcount--;
    			return (NULL);
            }
            continue;
        }
        else//ak chyba
        {
        	mailMutex.unlock();	
            cerr << "ERROR: recv" << endl;
            continue;
            //exit(EXIT_FAILURE);
        }
    }
    //ukonci sa thread
    mailMutex.unlock();//TODOT
    close(acceptSocket);
    threadcount--;
    cout << to_string(threadcount) << endl;
    return (NULL);
}


//kontrola spravneho formatu maildiru
bool checkMaildir(Arguments args){
	//kontrola maildiru a podpriecinkov
    DIR* dir;
    if((dir = opendir(args.maildir().c_str())) == NULL){
    	cerr << "Chyba pri otvarani maildiru." << endl;
    	return false;
    }
    closedir(dir);
    
    string tmpdir;//pre kontrolu cur,new,tmp
    tmpdir = args.maildir() + "/cur";
    if((dir = opendir(tmpdir.c_str())) == NULL){
    	cerr << "Maildir neobsahuje potrebne adresare" << endl;
    	return false;
    }
    closedir(dir);

    tmpdir = args.maildir() + "/tmp";
    if((dir = opendir(tmpdir.c_str())) == NULL){
    	cerr << "Maildir neobsahuje potrebne adresare" << endl;
    	return false;
    }
    closedir(dir);

    tmpdir = args.maildir() + "/new";
    if((dir = opendir(tmpdir.c_str())) == NULL){
    	cerr << "Maildir neobsahuje potrebne adresare" << endl;
    	return false;
    }
    closedir(dir);
    return true;
}


// SIGINT handler TODO
void signalHandler(int x)
{
	
	geci = 1;
	(void)x;
}



int main(int argc, char **argv){
    //vytvorenie objektu pre spracovanie argumentov
    Arguments args;
    //kontrola parametrov
    args.parseArgs(argc,argv);
    //TODO otestovat maildir+podpriecinky
	
    threadVar tmp;//struktura pre premenne ktore je potrebne predat vlaknam
    tmp.username = "";
    tmp.password = "";



    //nacitanie autentifikacneho suboru --TODO do funkcie
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


	
	if(!checkMaildir(args)){
		exit(1);//chyba pri overovani Maildiru
	}

    //pridanie maildiru do struktury pre funkciu vlakna
    tmp.maildir = args.maildir();

    //RESET ----- upravit premenovanie, mazanie je dobre

	if(args.reset()){//osetrit este ak reset je spusteny po resete
		ifstream resetIn;
		resetIn.open("reset.txt", ios::in);
		//kontrola ci existuje subor (da sa otvorit??)
		if ((resetIn.rdstate() & std::ifstream::failbit ) == 0 ){
			//subor existuje		
			string filename;
			while(!resetIn.eof()){//kym neni eof
				getline(resetIn,filename);
				//ak EOF(obsahoval este \n ale getline to uz nacitalo takze testujeme tu)
				if ( (resetIn.rdstate() & std::ifstream::eofbit ) != 0 ){
					break;
				}
				size_t pos = filename.rfind("/Maildir/cur/");//hladame posledny vyskyt cur--treba osetrovat vobec?
				string tmpfilename2 = filename;
				tmpfilename2.replace(pos,13,"/Maildir/new/");
				int res = rename(filename.c_str(), tmpfilename2.c_str());
				//if(rename(tmpfilename1.c_str(), tmpfilename2.c_str()) != 0){
				if(res != 0){ // preco je chyba??
					//TODO spinavy hack(ak sa nepodarilo presunut tak subor je zmazany)
					continue;

					//cout << res << endl;
					/*			
					cerr << "chyba pri premenovani(prsune) z cur do new" << endl;
						//posunut vsetko naspat? 
					exit(1);*/
				}
			}
			resetIn.close();
			if(remove("reset.txt")!=0){
				cerr << "Chyba pri mazani pomocneho suboru na ukladanie presunov z new do cur" << endl;
			}
			if(remove("info.txt")!=0){
				cerr << "Chyba pri mazani pomocneho suboru na ukladanie informacii o mailov" << endl;
			}
			if(remove("deleted.txt")!=0){
				cerr << "Chyba pri mazani pomocneho suboru na ukladanie mazanych suborov" << endl;
			}

			//odstranit vsetko ostatne z cur
			DIR* dir;
			struct dirent *file;
			string tmpdir = tmp.maildir + "/cur";
			string tmpfilename;
			if((dir = opendir(tmpdir.c_str())) != NULL){
				while((file = readdir(dir)) != NULL){
					if(!strcmp(file->d_name,".") || !strcmp(file->d_name,"..") ){
						continue;
					}
					tmpfilename = tmpdir + "/"+ file->d_name;
					if(remove(tmpfilename.c_str())!=0){
						cerr << "Chyba pri mazani pomocneho suboru na ukladanie presunov z new do cur" << endl;
					}
				}
				closedir(dir);
			}
			else{//problem s cur priecinkom, ukoncime program
				cerr << "chyba pri otvarani priecinku cur" << endl;
				exit(1);
			}	
		}
	}

	//prepinac -c
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

	//nastavime socket na neblokujuci
	int flags = fcntl(listenSocket, F_GETFL, 0);
	if ((fcntl(listenSocket, F_SETFL, flags | O_NONBLOCK))<0){
		perror("ERROR: fcntl");
		exit(EXIT_FAILURE);								
	}
	


	//priprava pre select
	fd_set set;
	FD_ZERO(&set);
	FD_SET(listenSocket, &set);

	




	//block signal to threads
	/*sigset_t s;


    sigemptyset(&s);
    sigaddset(&s, SIGQUIT);
    sigaddset(&s, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &s, NULL);*/


    signal(SIGINT, signalHandler);


	while(geci == 0){

		//select
		if (select(listenSocket + 1, &set, NULL, NULL, NULL) == -1){
			continue;
		}


		if ((acceptSocket = accept(listenSocket, (struct sockaddr*)&client, &clientLen)) < 0){
			cerr << "Chyba pri pripajani." << endl;
			continue;
		}

		//nastavime socket ako nelbokujuci
		int flags = fcntl(acceptSocket, F_GETFL, 0);
		if ((fcntl(acceptSocket, F_SETFL, flags | O_NONBLOCK))<0){
			cerr << "ERROR: fcntl" << endl;
			close(acceptSocket);	
			continue;						
		}

		//pridanie socketu do struktury
		tmp.socket = acceptSocket;
		


		//vytvorenie vlakna
		pthread_t myThread;

		if((pthread_create(&myThread, NULL, &doSth, &tmp)) != 0){
			cerr << "Chyba pri vytvarani vlakna" << endl;
			close(acceptSocket);
		}

		threadcount++;
	}

	//zatvorenie socketu
	close(listenSocket);

	while(threadcount);//cakame kym sa kazde vlano ukonci
    exit(0);
}
