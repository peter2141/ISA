/*Riesenie: POP3 server
* Predmet: ISA
* Autor: Peter Šuhaj(xsuhaj02)
* Rocnik: 2017/2018
* Subor: popser.cpp
*/

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
#include <fcntl.h>
#include <openssl/md5.h>
#include <csignal>
#include <locale> 
#include <dirent.h>
#include <ctime>
#include <list>
#include <vector>
#include <limits.h>

#include "popser.hpp"
#include "arguments.hpp"

using namespace std;


//premenna pre zaznamenie signalu SIGINT
static int volatile sigflag = 0;
//premenna pre vytvaranie uidl
int counter=0; 
//pocitadlo beziacich vlakien
int threadcount = 0;
//mutex pre uzamknutie maildiru
pthread_mutex_t mailMutex = PTHREAD_MUTEX_INITIALIZER;

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


//ziskanie absolutnej cesty zadaneho suboru
string absolutePath(const char *filename){
	string path;
	char* tmppath=realpath(filename,NULL);
	path = tmppath;
	free(tmppath);
	return path;
}


//kontrola ci string je platne cislo
bool isnumber(string number){
	char* ptr = NULL; 
	strtoll(number.c_str(),&ptr,10);
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
			//ak eof este mohol tam byt riadok bez \n
			//pozrieme ci je tam \r -- iba vtedy ak riadok neni prazdny
			if(line.length() > 0){
				if(line[line.length()-1] != '\r'){
					linenumber++;
				}
				linenumber++;//aj \n
			}
			break;
		}
		//pozrieme ci je tam \r -- iba vtedy ak riadok neni prazdny
		if(line.length() > 0){
			if(line[line.length()-1] != '\r'){
				linenumber++;
			}
		}
		else{//ak na riadku bolo iba \n
			linenumber++;
		}

	}
	file.close();
	size += linenumber;
	return size;
}

//ziskanie nazvov suborov v zadanom pricinku
void getFilesInCur(vector<string>& files,string maildir){
	DIR *tmpdir=NULL;
	struct dirent *tmpfile;
	string curdir = maildir + "/cur";
	tmpdir = opendir(curdir.c_str());
	ifstream log;
	string line;
	char tmpfilename[256];
	bool fileincur=false;
	while((tmpfile = readdir(tmpdir)) != NULL){
		fileincur = false;
		//skontrolujeme ci subor sa presunul z new do cur alebo bol rucne pridany do cur-ak nao tak ho ignorujeme
		log.open("info.txt");
		while (getline(log,line)){
			//ak EOF(obsahoval este \n ale getline to uz nacitalo takze testujeme tu)
			if ((log.rdstate() & std::ifstream::eofbit ) != 0 ){
				break;
			}
			sscanf(line.c_str(),"%[^/]/%*[^/]/%*d",tmpfilename);//nacitame nazov a velkost suboru
			
			//kontrola ci subor sa nachadza v log--ak ano tak dobre
			if (!strcmp(tmpfilename,tmpfile->d_name)){
	            fileincur = true;
	            break;
	        }
	    }	    
	    log.close();

		
	    //ak . alebo .. alebo subor nebol v logfile tak ho preskocime
		if(!strcmp(tmpfile->d_name,".") || !strcmp(tmpfile->d_name,"..") || !fileincur){
			continue;
		}
		//ifstream log,deleted;
		else{
			//pridame nazov suboru do vectoru mien
			files.push_back(string(tmpfile->d_name));
		}		
	}
	closedir(tmpdir);
}


//generovanie md5 hash zo stringu
void md5hash(string stringToHash, string& hash){
	unsigned char md5[MD5_DIGEST_LENGTH];
	MD5((unsigned char *)stringToHash.c_str(),stringToHash.size(),md5);
	//staci 32???
	char mdString[32];
   	for(int i = 0; i < 16; i++)
	sprintf(&mdString[i*2], "%02x", (unsigned int)md5[i]);
 	hash = mdString;
}

//vytvaranie unikatneho identifikatoru
void createUIDL(char* filename, string& UIDL){
	string tmp = to_string(time(NULL)) + string(filename) + to_string(counter);
	md5hash(tmp,UIDL);
}


//zistenie cesty k binarke-k ukladaniu pomocnych suborov
void pathToBinary(string& path){
	char buff[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buff, sizeof(buff)-1);
    string asdpath;
    if (len != -1) {
      buff[len] = '\0';
      asdpath = buff;
    }
    //TODO oserit ak chyba
    size_t position = asdpath.rfind("/");
    asdpath.erase(position+1,string::npos);
    path = asdpath;
}

//kontrola spravneho formatu maildiru
bool checkMaildir(struct threadVar args){

	//kontrola ci vobec maildir je v poriadku
    DIR* dir;
    if((dir = opendir(args.maildir.c_str())) == NULL){
    	cerr << "Chyba pri otvarani maildiru." << endl;
    	return false;
    }
    closedir(dir);
    
    //je tam priecinok cur?
    string tmpdir;
    tmpdir = args.maildir + "/cur";
    if((dir = opendir(tmpdir.c_str())) == NULL){
    	cerr << "Maildir neobsahuje potrebne adresare" << endl;
    	return false;
    }
    closedir(dir);

    //je tam priecinok new?
    tmpdir = args.maildir + "/new";
    if((dir = opendir(tmpdir.c_str())) == NULL){
    	cerr << "Maildir neobsahuje potrebne adresare" << endl;
    	return false;
    }
    closedir(dir);
    return true;
}


//nacitanie autentifikacneho suboru, kontrola chyb
void readAuthFile(string& username, string& password,arguments args){
    FILE *f = NULL;
    f = fopen(args.authfile().c_str(),"r");
    if(f == NULL){
    	cerr << "Nespravny autentifikacny subor." << endl;
    	pthread_mutex_destroy(&mailMutex);
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
					username += ch;
				}
				tmpString = "";
			}
			else{
				cerr << "Invalidny autentifikacny subor" << endl;
				pthread_mutex_destroy(&mailMutex);
				fclose(f);
				exit(1);
			}
		}
		else if(counter == 22){
			if(tmpString == "password = "){
				while((ch = fgetc(f)) != EOF){//do konca riadku nacitame prihlasovacie meno
					//druha newlina
					if(ch=='\n'){
						break;
					}
					password += ch;
				}
				tmpString = "";
				break;

			}
			else{
				cerr << "Invalidny autentifikacny subor" << endl;
				pthread_mutex_destroy(&mailMutex);
				fclose(f);
				exit(1);
			}
		}
		else if(counter > 22){//okrem "username = " a "password = " je tam aj nieco ine
			cerr << "Invalidny autentifikacny subor" << endl;
			pthread_mutex_destroy(&mailMutex);
			fclose(f);
			exit(1);
		}
		else{
			continue;
		}
	}

    fclose(f);
}


//vlastna funkcia pre send, osetrenie chyby
bool mySend(int socket,const char *msg,size_t msgsize){
	int ret;
	int size=0;
	string message = msg ;
	while(size != (int)msgsize && sigflag==0){

		ret = send(socket,message.c_str(),message.size(),0);
		if(ret > 0){
				message.erase(0,ret);
				size += ret;

		}

		else if(errno == EAGAIN){
			continue;
		}

		else{
			cerr << "Chyba pri posielani(funkcia sen = mySend). Odpajam klienta." << endl;
			pthread_mutex_unlock(&mailMutex);
			threadcount--;
			close(socket);
			return false;
		}

	}
	return true;
}


//funkcia pre vlakna==klienty
void* doSth(void *arg){
	
	//odpojime thread, netreba nanho cakat v hlavnom threade
	pthread_detach(pthread_self());
	

	//vytvorime strukturu lokalne obsahujucu potrebne informacie
	threadVar vars;
	//kedze funckia pre thread ma argument typu void musime argument pretypovat na datovy typ struktury
	vars = *((threadVar*)arg);


	//pre kontrolu sen = mySend	
	bool sen; 

	//lokalna premenna pre socket
	int acceptSocket;
	
	//sucket castujeme naspat na int
	acceptSocket = vars.socket;
	

	//vytvorenie uvitacej spravy
	char name[HOST_NAME_MAX];
	gethostname(name,HOST_NAME_MAX);
	
	//string welcomeMsg = "+OK POP3 server ready <" + to_string(getpid()) + "." + to_string(time(NULL)) + "@"  + name + ">\r\n";


	//string stringToHash = "<" + to_string(getpid()) + "." + to_string(time(NULL)) + "@"  + name + ">" + vars.password;
	//string pre timestamp
	string welcomeTimestamp = "<" + to_string(pthread_self()) + "." + to_string(time(NULL)) + "@"  + name + ">";
	//string na hashovanie - pripjime helso na koniec
	string stringToHash = welcomeTimestamp + vars.password;


	//pre kazdy thread unikatny welcome msg?? TODO
	//uvitacia sprava
	string welcomeMsg = "+OK POP3 server ready " +welcomeTimestamp + "\r\n";


	//poslanie uvitacej spravy
	sen = mySend(acceptSocket,welcomeMsg.c_str(),strlen(welcomeMsg.c_str()));
	if(!sen){
		//ukoncim thread ak chyba
		return(NULL);
	}


	//string pre ulozenie hashu
 	string hash;
 	//zahashuje sa uvitacia sprava
 	md5hash(stringToHash,hash);


	//premenna pre buffer
	char buff[BUFSIZE];
	
	//string pre stav automatu, po pripjeni je v stavu authentication
	string state = "authentication"; 


	 //flag pre zistenie ci bol zadany spravny username
	bool userOK = false;

	//zoznam pre maily oznacene ako deleted v DELE, zmazanie v UPDATE
  	list<char*> tmpdel_list;

  	//vector pre subory v current - kvoli cislovanie mailov  pridanie suborov v cur do vectoru,osetrit ci sa da otvorit cur??
  	vector<string> mailnums;
  	
  	//pre kontrolu username v apop a pass
  	string username;


  	//premenna pre meranie casu
  	long int timestamp=0;

	//------------------------------------------------------------------------------------------------------------------------------
	//HLAVNA SMYCKA
	//smycka pre zikavanie dat, kontrola flagu pri siginte
	while (sigflag == 0)		
	{		

		bzero(buff,BUFSIZE);//vynulovanie buffera			
		int res = recv(acceptSocket, buff, BUFSIZE,0);//poziadavok ma max dlzku 255 bajtov spolu s CRLF	
		if (res > 0)
		{
			timestamp = 0;//vynulujeme casovac ak 
			//vytvorenie string z Cstring kvoli jednoduchsej prace s retazcom
			string message = buff;
			

			//kontrola dlzky spravy ????? aka max dlzka???
			if(message.size()>255){
				sen = mySend(acceptSocket,"-ERR Too long command\r\n",strlen("-ERR Too long command\r\n"));
				if(!sen){
					//ukoncim thread ak chyba
					return(NULL);
				}
				continue;
			}

			string command="";//retazec pre prikaz 
			string argument="";//retazec pre argument
			
			// TODO osetrit picoviny (ak iba \n, atd) 
			if(message.empty()){
				continue;
			}

			if(message.size()<2){
				sen = mySend(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"));
				if(!sen){
					//ukoncim thread ak chyba
					return(NULL);
				}
				continue;
			}


			//kontrola CRLF na konci
			if((message.find("\r\n")) != message.size()-2){
				sen = mySend(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"));
				if(!sen){
					//ukoncim thread ak chyba
					return(NULL);
				}
				continue;
			}

			//odstraneni CRLF na konci
			message.erase(message.length()-2,message.length());
			

			//rozdelenie prikazu a argumentu
			bool space = false;
			for (unsigned int i=0; i<(message.length()); i++)
  			{
  				if(i!=0 && (message.at(i) == ' ')){
  					if(space==true){
  						argument += message.at(i);
  					}
  					else{
  						space = true;
  					}
  					
  					continue;

  				//ak bol na
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


  			//switch pre stavy automatu komunikacie
  			switch(hashState(state)){


  				case auth:{

  					//switch pre spracovanie prikazov, kazdy case reprezentuje jeden prikaz
  					switch (hashCommand(commandLow)){
  		
  						case user://prisiel prikaz user
							if(vars.crypt){//bol zadany parameter -c, USER after USER moze byt

								//kontrola dlzky argumentu - dane v RFC
								if(argument.length()>40){
									sen = mySend(acceptSocket,"-ERR Too long argument.\r\n",strlen("-ERR Too long argument.\r\n"));
									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
									break;
								}


								//kontrola ci argument obsahuje medzeru
								bool space = false;
								for (unsigned i=0; i<argument.length(); i++){
    								if(isspace(argument[i])){
										space = true;
    								}
  								}

  								//ak ano chyba
  								if(space){
  									sen = mySend(acceptSocket,"-ERR Username can not contain whitespace.\r\n",strlen("-ERR Username can not contain whitespace.\r\n"));
									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
									break;
  								}

  								//ak prazdny argument tiez chyba
  								if(argument==""){
  									sen = mySend(acceptSocket,"-ERR Username was not entered.\r\n",strlen("-ERR Username was not entered.\r\n"));
										if(!sen){
											//ukoncim thread ak chyba
											return(NULL);
										}
										break;
  								}

  								//ak vsetko v pohode tak OK
								sen = mySend(acceptSocket,"+OK Hello my friend\r\n",strlen("+OK Hello my friend\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								username = argument;
								userOK = true;	
  								

								break;

							}
							else{//nebola povolena autentifikacia USER - PASS
								sen = mySend(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}
  							break;

  						case pass:{
							//kontrola dlzky argumentu - dane v RFC
							if(argument.length()>40){
								sen = mySend(acceptSocket,"-ERR Too long argument.\r\n",strlen("-ERR Too long argument.\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}
  							if(userOK){
  								if(vars.password.compare(argument) == 0 && vars.username.compare(username) == 0){//dobre heslo a meno
  									//najpr skontrolujeme maildir
  									if(!checkMaildir(vars)){
  										sen = mySend(acceptSocket,"-ERR problem with maildir.\r\n",strlen("-ERR problem with maildir.\r\n"));
  										if(!sen){
											//ukoncim thread ak chyba
											return(NULL);
										}
  										close(acceptSocket);
  										threadcount--;
  										return(NULL);
  									}


  									if(pthread_mutex_trylock(&mailMutex)){//kontrola ci je maildir obsadenyy
  										sen = mySend(acceptSocket,"-ERR Mailbox locked, try next time\r\n",strlen("-ERR Mailbox locked, try next time\r\n"));
  										if(!sen){
											//ukoncim thread ak chyba
											return(NULL);
										}
  										close(acceptSocket);//odpojime klienta, alebo neodpajat????
  										threadcount--;
  										return(NULL);
  									}

  									//posle sa ok iba vtedy ak maildir neni locknuty
   									//ak maildir ok aj heslo ok tak posleme kladnu odpoved
  									sen = mySend(acceptSocket,"+OK Correct password\r\n",strlen("+OK Correct password\r\n"));
  									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}

  									getFilesInCur(mailnums,vars.maildir);//ziskame subory z current ktore su uz tam


  									bool firstRun = true;//kontrola prveho spustenia


  									struct stat buffer;   
  									if(stat("reset.txt", &buffer) == 0){
  										firstRun = false;
  									}
  									//vytvorime potrebne pomocne subory
  									if(firstRun){
  										ofstream resfile("reset.txt");
  										ofstream infoFile("info.txt");
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
												cerr << "chyba pri premenovani(presune) z new do cur" << endl;
												close(acceptSocket);
			  									pthread_mutex_unlock(&mailMutex);
			  									//posunut vsetko naspat? 
												threadcount--;
												return(NULL);
											}

											//pridame nazov noveho suboru do vectoru mien
											mailnums.push_back(string(file->d_name));
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
			  							pthread_mutex_unlock(&mailMutex);
			  							threadcount--;
										return(NULL);
									}	//presun do dalsieho stavu
									
									infoFile.close();
 									resetFile.close();

  									state = "transaction";
  									break;
  								}

  								else{//zle heslo
  									sen = mySend(acceptSocket,"-ERR Wrong username or password\r\n",strlen("-ERR Wrong username or password\r\n"));
  									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
  									userOK = false;
  									break;
  								}
  							}//PASS mozno zadat iba pos spravnom USER
  							else{
  								sen = mySend(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"));
  								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
  							}

  							break;
  						}
  						case apop:{
  							//porovnat s hashom
  							if(vars.crypt){//ak sa zada parameter -c moze sa pouzivat ibsa kombinacia USER a PASS
  								sen = mySend(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"));
				  				if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
  							}
							

  							//prazdny argument
							if(argument==""){
								sen = mySend(acceptSocket,"-ERR No username and digest entered.\r\n",strlen("-ERR No username and digest entered.\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}

							//kontrola dlzky argumentu - dane v RFC
							if(argument.length()>81){//40+40+medzera
								sen = mySend(acceptSocket,"-ERR Too long argument.\r\n",strlen("-ERR Too long argument.\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}



							//spocitame biele znaky v argumente
							int space = 0;
							for (unsigned i=0; i<argument.length(); i++){
								if(isspace(argument[i])){
									space++;
								}
							}


							//pozrieme ci 1 znak alebo posledny je space
							int spaceloc1 = 1;
							int spaceloc2 = 0;
						 	spaceloc1 = argument.find(" ");
							spaceloc2 = argument.rfind(" ");

							if(spaceloc1 == 0 || spaceloc2 == (int)(argument.length()-1)){
								sen = mySend(acceptSocket,"-ERR Username or digest not contain whitespace.\r\n",strlen("-ERR Username or digest not contain whitespace.\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							} 

							//ak okrem medzeri medzi username a digest bol est enejaky whitespace
							if(space > 1){
								sen = mySend(acceptSocket,"-ERR Username or digest not contain whitespace.\r\n",strlen("-ERR Username or digest not contain whitespace.\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}
							


 						
 							//pridanie username k hashu
  							string hash2 = vars.username + " " + hash;
  							string apopstr = argument;
  							if(hash2.compare(apopstr) == 0){
								//kontrola maildiru
								if(!checkMaildir(vars)){
  										sen = mySend(acceptSocket,"-ERR problem with maildir.\r\n",strlen("-ERR problem with maildir.\r\n"));
  										if(!sen){
											//ukoncim thread ak chyba
											return(NULL);
										}
  										close(acceptSocket);
  										threadcount--;
  										return(NULL);
  								}


								if(pthread_mutex_trylock(&mailMutex) != 0){
									sen = mySend(acceptSocket,"-ERR Mailbox locked, try next time\r\n",strlen("-ERR Mailbox locked, try next time\r\n"));
									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
									close(acceptSocket);//odpojime klienta, alebo neodpajat????
									threadcount--;
									return(NULL);
								}

								//ak vsetko v poriadku posle sa OK
 								sen = mySend(acceptSocket,"+OK Correct password\r\n",strlen("+OK Correct password\r\n"));
  								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}

								getFilesInCur(mailnums,vars.maildir);//ziskame subory v current, preto tu lebo iba teraz mozeme pristupovat k maildiru


								bool firstRun = true;//kontrola prveho spustenia


								struct stat buffer;   
								if(stat("reset.txt", &buffer) == 0){
									firstRun = false;
								}
								//vytvorime potrebne pomocne subory
								if(firstRun){
									ofstream resfile("reset.txt");
									ofstream infoFile("info.txt");
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
											cerr << "chyba pri premenovani(presune) z new do cur" << endl;
											close(acceptSocket);
		  									pthread_mutex_unlock(&mailMutex);
		  									//posunut vsetko naspat? 
											threadcount--;
											return(NULL);
										}

										//pridame nazov suboru do vectoru mien
										mailnums.push_back(string(file->d_name));
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
		  							pthread_mutex_unlock(&mailMutex);
		  							threadcount--;
									return(NULL);
								}	//presun do dalsieho stavu
								
								infoFile.close();
								resetFile.close();

								state = "transaction";
								break;
  							}
  							else{
  								sen = mySend(acceptSocket,"-ERR Wrong MD5 hash or wrong username\r\n",strlen("-ERR Wrong MD5 hash or wrong username\r\n"));
  								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
  								break;
  							}
  							break;
  							}
  						case quit://udpojime klienta
  							if(argument.compare("")){//quit neberie argumenty
  								sen = mySend(acceptSocket,"-ERR Quit does not take arguments\r\n",strlen("-ERR Quit does not take arguments\r\n"));
  								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
  							}
  							sen = mySend(acceptSocket,"+OK Server signing off\r\n",strlen("+OK Server signing off\r\n"));
  							if(!sen){
								//ukoncim thread ak chyba
								return(NULL);
							}
  							close(acceptSocket);
  							pthread_mutex_unlock(&mailMutex);
  							threadcount--;
  							return(NULL);
  						default:
  							sen = mySend(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"));
  							if(!sen){
								//ukoncim thread ak chyba
								return(NULL);
							}
							break;
  					}
  					break;

  				}


				case trans:{//stav transaction, praca so slozkou maildir
					//switch pre spracovanie prikazov
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
   									while(msgcnt < (int)mailnums.size()){//prejdeme vsetky subory ktore sa nachadzaju v current
										
										//iteracia cez list, kontrola ci uz dany subor bol oznaceny na mazanie
										for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
										    if(!mailnums[msgcnt].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
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
												if(!mailnums[msgcnt].compare(filename)){//ak sme nasli subor 
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
		  							sen = mySend(acceptSocket,answ.c_str(),strlen(answ.c_str()));//odosielanie
		  							if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
  									closedir(d);
	  							}
	  							else{//problem s cur priecinkom, ukoncime program
	  								cerr << "chyba pri otvarani priecinku cur" << endl;
	  								close(acceptSocket);
	  								pthread_mutex_unlock(&mailMutex);
	  								threadcount--;
	  								exit(1);
	  							}	
  							}
  							else{//bol zadany argument
  								//kontrola dlzky argumentu - dane v RFC
								if(argument.length()>40){
									sen = mySend(acceptSocket,"-ERR Too long argument.\r\n",strlen("-ERR Too long argument.\r\n"));
									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
									break;
								}

  								//kontrola ci argument je cislo
  								if(!isnumber(argument)){
  									sen = mySend(acceptSocket,"-ERR Not valid number\r\n",strlen("-ERR Not valid number\r\n"));
  									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
  									break;
  								}

  								int msgnum;
	 							//skusime nacitat cislo
								try {
									msgnum = stoi(argument);
								}
								//ak chyba tak sa hodi vynimka a osetrime to
								catch(...) {
								  sen = mySend(acceptSocket,"-ERR Too long number.\r\n",strlen("-ERR Too long number.\r\n"));
									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
									break;
								}

  								msgnum -= 1;//kvoli indexovaniu vo vectore - sa indexuje od 0 
  								DIR *d = NULL;
  								string tmpdir = vars.maildir + "/cur";
  								bool listOK = true;

  								if((d = opendir(tmpdir.c_str())) != NULL){									
									if(msgnum >= (int)mailnums.size() || msgnum < 0){//kontrola intervalu 
	  									sen = mySend(acceptSocket,"-ERR Messege does not exists\r\n",strlen("-ERR Messege does not exists\r\n"));
	  									if(!sen){
											//ukoncim thread ak chyba
											return(NULL);
										}
										closedir(d);
										break;
	  								}
	  								else{

										//iteracia cez list, kontrola ci uz dany subor bol oznaceny na mazanie
										for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
										    if(!mailnums[msgnum].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
										    	sen = mySend(acceptSocket,"-ERR Messege already deleted\r\n",strlen("-ERR Messege already deleted\r\n"));
										    	if(!sen){
													//ukoncim thread ak chyba
													return(NULL);
												}
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
												if(!mailnums[msgnum].compare(filename)){//ak sme nasli subor 
													break; // v tmpsize mame velkost daneho suboru
												}
											}
	  										file.close();
		  									string tmpAnsw = "+OK " + to_string(++msgnum)+ " " + to_string(tmpsize) + "\r\n";
		  									sen = mySend(acceptSocket,tmpAnsw.c_str(),strlen(tmpAnsw.c_str()));
		  									if(!sen){
												//ukoncim thread ak chyba
												return(NULL);
											}
		  								}
	  								}

  									closedir(d);
	  							}
	  							else{//problem s cur priecinkom, ukoncime program
	  								cerr << "chyba pri otvarani priecinku cur" << endl;
	  								close(acceptSocket);
	  								pthread_mutex_unlock(&mailMutex);
	  								threadcount--;
	  								return(NULL);
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
  								sen = mySend(acceptSocket,tmpAnsw.c_str(),strlen(tmpAnsw.c_str()));
  								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;

  							}

  							else{//problem s cur priecinkom, ukoncime program
  								cerr << "chyba pri otvarani priecinku cur" << endl;
  								close(acceptSocket);
  								pthread_mutex_unlock(&mailMutex);
  								threadcount--;
  								return(NULL);
  							}	
  							break;
  						}

  						case retr:{
  							//retr ma povinny argument cislo spravy
  							if(!argument.compare("")){//retr ma povinny argument
  								sen = mySend(acceptSocket,"-ERR Retr need message number\r\n",strlen("-ERR Retr need message number\r\n"));
  								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
  							}

  							//kontrola dlzky argumentu - dane v RFC
							if(argument.length()>40){
								sen = mySend(acceptSocket,"-ERR Too long argument.\r\n",strlen("-ERR Too long argument.\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}

  							//kontrola ci argument je cislo
							if(!isnumber(argument)){
								sen = mySend(acceptSocket,"-ERR Not valid number\r\n",strlen("-ERR Not valid number\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}
							
  							DIR *d = NULL;
  							

							int msgnum;
							//skusime nacitat cislo
							try {
								msgnum = stoi(argument);
							}
							//ak chyba tak sa hodi vynimka a osetrime to
							catch(...) {
							  sen = mySend(acceptSocket,"-ERR Too long number.\r\n",strlen("-ERR Too long number.\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}


  							msgnum -= 1;//kvoli indexovaniu vo vectore 
  							string tmpdir = vars.maildir + "/cur";
  							int filesize = 0;
  							string tmpfilename;
  							string msg = "";
  							string tmpline;
  							bool retrOK = true;
  							if((d = opendir(tmpdir.c_str())) != NULL){
  								
   								if(msgnum >= (int)mailnums.size() || msgnum < 0){//osetrenie cisla mailu
  									sen = mySend(acceptSocket,"-ERR Messege does not exists\r\n",strlen("-ERR Messege does not exists\r\n"));
  									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
									break;
  								}

  								else{

  									tmpfilename = tmpdir + "/" + mailnums[msgnum];
									//kontrola ci sprava bola mazana

									for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
									    if(!mailnums[msgnum].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
										   	sen = mySend(acceptSocket,"-ERR Messege already deleted\r\n",strlen("-ERR Messege already deleted\r\n"));
										   	if(!sen){
												//ukoncim thread ak chyba
												return(NULL);
											}
										    retrOK = false;//flag pre oznacenie chyby 
											break;
									    }
									}
									
	
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
										sscanf(line.c_str(),"%[^/]/%*[^/]/%d",filename,&filesize);//nacitame nazov a velkost suboru
										if(!mailnums[msgnum].compare(filename)){//ak sme nasli subor 
											break; // v tmpsize mame velkost daneho suboru
										}
									}
									file.close();


									ifstream f;
									f.open(tmpfilename.c_str());
									//nacitanie emailu
									while(!f.eof()){
										getline(f,tmpline);
										if ((f.rdstate() & std::ifstream::eofbit ) != 0 ){
											
												if(tmpline.size() > 0){//ak neni prazdny riadok
													if(tmpline[0] == '.'){
														tmpline.insert(0, 1, '.');
													}
													msg.append(tmpline);
													if(tmpline[tmpline.length()-1] != '\r'){//ak riadok bol zakonceny iba s /n
														msg += "\r\n";
													}
													else{//ak riadok bol nzakonceny s /r/n  tak pridame iba /n
														msg += "\n";
													}
													
												}
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
			  								sen = mySend(acceptSocket,tmpAnsw.c_str(),strlen(tmpAnsw.c_str()));
			  						
			  								if(!sen){
												//ukoncim thread ak chyba
												return(NULL);
											}
											closedir(d);
											break;
  									}
  								}
  								closedir(d);

  							}
  							else{//problem s cur priecinkom, ukoncime program
  								cerr << "chyba pri otvarani priecinku cur" << endl;
  								close(acceptSocket);
  								pthread_mutex_unlock(&mailMutex);
  								threadcount--;
  								return(NULL);
  							}	
  							break;
  						}

  						case dele:{
  							if(!argument.compare("")){//dele ma povinny argument
  								sen = mySend(acceptSocket,"-ERR Enter message number you want to delete\r\n",strlen("-ERR Enter message number you want to delete\r\n"));
  								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
  							}

							//kontrola dlzky argumentu - dane v RFC
							if(argument.length()>40){
								sen = mySend(acceptSocket,"-ERR Too long argument.\r\n",strlen("-ERR Too long argument.\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}

  							//kontrola ci argument je cislo
							if(!isnumber(argument)){
								sen = mySend(acceptSocket,"-ERR Not valid number\r\n",strlen("-ERR Not valid number\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}
  							DIR *d = NULL;
  							bool delOK = true;

  							int msgnum;
							//skusime nacitat cislo
							try {
								msgnum = stoi(argument);
							}
							//ak chyba tak sa hodi vynimka a osetrime to
							catch(...) {
							  sen = mySend(acceptSocket,"-ERR Too long number.\r\n",strlen("-ERR Too long number.\r\n"));
								if(!sen){
									//ukoncim thread ak chyba
									return(NULL);
								}
								break;
							}

  							msgnum -= 1;
  							string tmpdir = vars.maildir + "/cur";
  							if((d = opendir(tmpdir.c_str())) != NULL){
								if(msgnum >= (int)mailnums.size() || msgnum < 0){//kontrola intervalu 
  									sen = mySend(acceptSocket,"-ERR Messege does not exists\r\n",strlen("-ERR Messege does not exists\r\n"));
  									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
									closedir(d);
									break;
  								}
  								else{
									//iteracia cez list, kontrola ci uz dany subor bol oznaceny na mazanie
									for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
									    if(!mailnums[msgnum].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
									    	sen = mySend(acceptSocket,"-ERR Messege already deleted\r\n",strlen("-ERR Messege already deleted\r\n"));
									    	if(!sen){
												//ukoncim thread ak chyba
												return(NULL);
											}
											delOK = false;
											break;
									    }
									}

										
									
	  								if(delOK){//posleme ok ak bol subor oznaceny ako mazany(kvoli tomu aby tato hlaska sa neposielala pri chybe)
	  									tmpdel_list.push_back((char*)mailnums[msgnum].c_str());		
	  									string tmpAnsw = "+OK msg deleted\r\n";
	  									sen = mySend(acceptSocket,tmpAnsw.c_str(),strlen(tmpAnsw.c_str()));
	  									if(!sen){
											//ukoncim thread ak chyba
											return(NULL);
										}
	  								}
  								}

  								closedir(d);
  							}
  							else{//problem s cur priecinkom, ukoncime program
  								cerr << "chyba pri otvarani priecinku cur" << endl;
  								close(acceptSocket);
  								pthread_mutex_unlock(&mailMutex);
  								threadcount--;
  								return(NULL);
  							}	
  							break;
  						}
  						case rset:
  							tmpdel_list.clear();
  							sen = mySend(acceptSocket,"+OK\r\n",strlen("+OK\r\n"));
  							if(!sen){
								//ukoncim thread ak chyba
								return(NULL);
							}
  							break;
  						case uidl:{
  							if(!argument.compare("")){//nebol zadany parameter, tj poslat informaciu o vsetkych spravach
  								DIR *d = NULL;
  								string tmpdir = vars.maildir + "/cur";
  								int msgcnt = 0;//premenn pra cyklus(indexovanie vo vectoru nazvov suborov)
  								int msgnum = 1;//premenna pre cislo sprav
  								string answ = "";
  								string tmpAnsw = "";
  								bool uidlOK = true;//flag
   								if((d = opendir(tmpdir.c_str())) != NULL){									
   									while(msgcnt < (int)mailnums.size()){//prejdeme vsetky subory ktore sa nachadzaju v current
										
										//iteracia cez list, kontrola ci uz dany subor bol oznaceny na mazanie
										for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
										    if(!mailnums[msgcnt].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
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
												if(!mailnums[msgcnt].compare(filename)){//ak sme nasli subor 
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
		  							sen = mySend(acceptSocket,answ.c_str(),strlen(answ.c_str()));//odosielanie
		  							if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
  									closedir(d);
	  							}
	  							else{//problem s cur priecinkom, ukoncime program
	  								cerr << "chyba pri otvarani priecinku cur" << endl;
	  								close(acceptSocket);
	  								pthread_mutex_unlock(&mailMutex);
	  								threadcount--;
    								return (NULL);
	  							}	
  							}
  							else{//bol zadany argument

								//kontrola dlzky argumentu - dane v RFC
								if(argument.length()>40){
									sen = mySend(acceptSocket,"-ERR Too long argument.\r\n",strlen("-ERR Too long argument.\r\n"));
									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
									break;
								}

  								//kontrola ci argument je cislo
  								if(!isnumber(argument)){
  									sen = mySend(acceptSocket,"-ERR Not valid number\r\n",strlen("-ERR Not valid number\r\n"));
  									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
  									break;
  								}
  								 
  								int msgnum; 
								//skusime nacitat cislo
								try {
									msgnum = stoi(argument);
								}
								//ak chyba tak sa hodi vynimka a osetrime to
								catch(...) {
								  sen = mySend(acceptSocket,"-ERR Too long number.\r\n",strlen("-ERR Too long number.\r\n"));
									if(!sen){
										//ukoncim thread ak chyba
										return(NULL);
									}
									break;
								}

  								msgnum -= 1;//kvoli indexovaniu vo vectore 
  								DIR *d = NULL;
  								string tmpdir = vars.maildir + "/cur";
  								bool deleOK = true;

  								if((d = opendir(tmpdir.c_str())) != NULL){									
									if(msgnum >= (int)mailnums.size() || msgnum < 0){//kontrola intervalu 
	  									sen = mySend(acceptSocket,"-ERR Messege does not exists\r\n",strlen("-ERR Messege does not exists\r\n"));
	  									if(!sen){
											//ukoncim thread ak chyba
											return(NULL);
										}
										closedir(d);
										break;
	  								}
	  								else{

										//iteracia cez list, kontrola ci uz dany subor bol oznaceny na mazanie
										for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
										    if(!mailnums[msgnum].compare(string(*iterator))){//ak vybrany mail bol uz oznaceny na mazanie chyba
										    	sen = mySend(acceptSocket,"-ERR Messege already deleted\r\n",strlen("-ERR Messege already deleted\r\n"));
										    	if(!sen){
													//ukoncim thread ak chyba
													return(NULL);
												}
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
												if(!mailnums[msgnum].compare(filename)){//ak sme nasli subor 
													break; // v tmpsize mame velkost daneho suboru
												}
											}
	  										file.close();
		  									string tmpAnsw = "+OK " + to_string(++msgnum) + " " + string(uidl) + "\r\n";
		  									sen = mySend(acceptSocket,tmpAnsw.c_str(),strlen(tmpAnsw.c_str()));
		  									if(!sen){
												//ukoncim thread ak chyba
												return(NULL);
											}
		  								}
	  								}

  									closedir(d);
	  							}
	  							else{//problem s cur priecinkom, ukoncime program
	  								cerr << "chyba pri otvarani priecinku cur" << endl;
	  								close(acceptSocket);
	  								pthread_mutex_unlock(&mailMutex);
    								threadcount--;
    								return (NULL);
	  							}	
  							}
  							break;
  						}
  						case noop://nerob nic
  							sen = mySend(acceptSocket,"+OK\r\n",strlen("+OK\r\n"));
  							if(!sen){
								//ukoncim thread ak chyba
								return(NULL);
							}
  							break;
  						case quit: //ukoncime pracu s amildirom,ide sa do stavu quit
  							state = "update";
  							break;
  						default://ak zly prikaz
  							sen = mySend(acceptSocket,"-ERR Invalid command\r\n",strlen("-ERR Invalid command\r\n"));
  							if(!sen){
								//ukoncim thread ak chyba
								return(NULL);
							}
							break;
  					}
  					if(state.compare("update")){
  						break;
  					}
  					
  				}
				case update:{//stav update, komunikacia sa ukonci, vymazu sa spravy oznacene na mazanie
					string filename;
					string line;
					ifstream log;
					ofstream tmp;
					char tmpfilename[256];
					bool notdeleted = false;
					//prejdeme vsetky oznacene subory
					for (list<char*>::const_iterator iterator = tmpdel_list.begin(); iterator != tmpdel_list.end(); iterator++) {
						notdeleted = false;
						log.open("info.txt");
						tmp.open("tmp.txt");
						filename = vars.maildir + "/cur/" + *iterator;
						if(remove(filename.c_str())!=0){//vymazeme subor, problem u mazani
							notdeleted=true;//subor sa nepodarilo vymazat
						}

						else{//ak mazanie okej
							//vymazeme zaznam o suboru v log.txt
							while (getline(log,line)){
								sscanf(line.c_str(),"%[^/]/%*[^/]/%*d",tmpfilename);//nacitame nazov a velkost suboru
		       					//ak riaodk s informaciou o subore na mazanie tak preskocime
		       					if(notdeleted == false){
			       					if (strcmp(tmpfilename,*iterator)){
							            tmp << line << endl;
							        }
		       					}
		       					else{//ak sa subor nepodarilo vymazat tak ho nechame v logfile
		       						tmp << line << endl;
		       					}

						    }
						}

					    
					    tmp.close();
					    log.close();
						
						//vytvorime novy log
						remove("info.txt");
						rename("tmp.txt","info.txt");
					}

					if(notdeleted == false){//vsetko sa podarilo vymazat
						sen = mySend(acceptSocket,"+OK Server signing off\r\n",strlen("+OK Server signing off\r\n"));
						if(!sen){
							//ukoncim thread ak chyba
							return(NULL);
						}
					}
					else{//nieco sa nepodarilo vymazat
						sen = mySend(acceptSocket,"-ERR deleting some messages.\r\n",strlen("-ERR deleting some messages.\r\n"));
						if(!sen){
							//ukoncim thread ak chyba
							return(NULL);
						}
					}
					//odpojime klienta, ukonci sa thread
					close(acceptSocket);
					pthread_mutex_unlock(&mailMutex);
					threadcount--;
					return(NULL);
					break;	
				}	
				default:
					break;
  			}



		}
        else if (res == 0){ //ak sa klient odpojil 
            //odomkne sa zamok, ukonci sa komunikacia
            pthread_mutex_unlock(&mailMutex);					
			break;
        }
        else if (errno == EAGAIN){ // == EWOULDBLOCK, ak by socket blokoval,tj. neprijima ziadne data
        	if(timestamp == 0){
        		timestamp = (long int)time(NULL);//ak bol nulovany pridame cas ked prvy krat sa neblokovalo tj. neprislo nic
        	}
        	long int tmptime = (long int)time(NULL) - timestamp; //pozrieme rozdiel casu
            if(tmptime >= 600){//ak preslo 10 minut(600 sekund)
            	sen = mySend(acceptSocket,"-ERR Timeout expired\r\n",strlen("-ERR Timeout expired\r\n"));
            	if(!sen){
					//ukoncim thread ak chyba
					return(NULL);
				}
				close(acceptSocket);
				pthread_mutex_unlock(&mailMutex);//?????? TODO
				threadcount--;
    			return (NULL);
            }
            continue;
        }
        else//ak chyba->zaporna hodnota recv
        {
        	//odpojime klienta, skonci sa vlakno
            cerr << "Error at recv, closing client." << endl;
            pthread_mutex_unlock(&mailMutex);
            close(acceptSocket);
    		threadcount--;
    		return (NULL);
        }
    }
    //ukonci sa thread
    pthread_mutex_unlock(&mailMutex);//TODO
    close(acceptSocket);
    threadcount--;
    return (NULL);
}



//funkcia na zachytenie signalu SIGINT
void signalHandler(int x)
{
	
	//nastavi sa globalna premenna
	sigflag = 1;
	(void)x;
}


//hlavna funkcia programu
int main(int argc, char **argv){

    //vytvorenie objektu pre spracovanie argumentov
    arguments args;
    //kontrola parametrov
    args.parseArgs(argc,argv);

	//struktura pre premenne ktore je potrebne predat vlaknam
    threadVar tmp;

    tmp.username = "";
    tmp.password = "";


    //nacitanie username a password, 
    readAuthFile(tmp.username,tmp.password,args);

    //pridanie maildiru do struktury pre funkciu vlakna
    tmp.maildir = args.maildir();



    //RESET 
	if(args.reset()){//bol zadany parameter -r
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
				struct stat buffer;   
				//ak subor uz neexistuje nerobime nic - znamena ze uz bol vymazany
				if(stat(filename.c_str(), &buffer) != 0){
					continue;
				}

				//hladame posledny vyskyt cur
				size_t pos = filename.rfind("/cur/");
				string tmpfilename2 = filename;
				//nahradime /cur/ na /new/ kvoli presuvaniu
				tmpfilename2.replace(pos,5,"/new/");

				//presnieme naspat subor
				int res = rename(filename.c_str(), tmpfilename2.c_str());
				
				//if(rename(tmpfilename1.c_str(), tmpfilename2.c_str()) != 0){
				if(res != 0){ // preco je chyba??
					cerr << "chyba pri premenovani(prsune) z cur do new" << endl;
					//posunut vsetko naspat? pokracovat?  
					exit(1);
				}
			}
			resetIn.close();

			//vymazu sa logovacie subory
			if(remove("reset.txt")!=0){
				cerr << "Chyba pri mazani pomocneho suboru na ukladanie presunov z new do cur" << endl;
			}
			if(remove("info.txt")!=0){
				cerr << "Chyba pri mazani pomocneho suboru na ukladanie informacii o mailov" << endl;
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
				pthread_mutex_destroy(&mailMutex);
				exit(1);
			}	
		}
	}




	//kontrola ci bol zadany prepinac -c
	if(args.crypt()){
		tmp.crypt = true;
	}
	else{
		tmp.crypt = false;
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
		pthread_mutex_destroy(&mailMutex);
		exit(1);
    }

    //nastavenie reuseaddr, aby sa po ukonceni serveru pouzity port mohol znovu pouzit hned
    int optval = 1;
    if((setsockopt(listenSocket,SOL_SOCKET,SO_REUSEADDR,&optval,sizeof(int))) < 0){
    	cerr << "Chyba pri setsockopt reuseaddr" << endl;
    	pthread_mutex_destroy(&mailMutex);
    	close(listenSocket);
		exit(1);
    }

    //bind - priradenie adresy pre socket
	if (bind(listenSocket, (struct sockaddr *)&server, sizeof(server)) < 0){
		cerr << "Chyba pri bindovani socketu." << endl;
		pthread_mutex_destroy(&mailMutex);
		close(listenSocket);
		exit(1);
	}    

	//cakanie na pripojenia
	if (listen(listenSocket, QUEUE) < 0){
		cerr << "Chyba pri nastaveni socketu na pasivny(funkcia listen())." << endl;
		pthread_mutex_destroy(&mailMutex);
		close(listenSocket);
		exit(1);
	}

	//nastavime socket na neblokujuci
	int flags = fcntl(listenSocket, F_GETFL, 0);
	if ((fcntl(listenSocket, F_SETFL, flags | O_NONBLOCK))<0){
		cerr << "Chyba pri nastaveni listen socketu na neblokujuci." << endl;
		pthread_mutex_destroy(&mailMutex);
		close(listenSocket);
		exit(1);							
	}
	


	//priprava pre select
	fd_set set;
	FD_ZERO(&set);
	FD_SET(listenSocket, &set);

	
	//spracovanie signalu SIGINT
    signal(SIGINT, signalHandler);

    //smyzka ktora prijima pripojenie a vytvara komunikacny socket
	while(sigflag == 0){

		//select
		if (select(listenSocket + 1, &set, NULL, NULL, NULL) == -1){
			continue;
		}

		//vytvori sa komunikacny sucket pre daneho klienta
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
		

		//vytvorenie vlakna pre dane pripojenie
		pthread_t myThread;

		if((pthread_create(&myThread, NULL, &doSth, &tmp)) != 0){
			cerr << "Chyba pri vytvarani vlakna" << endl;
			close(acceptSocket);
			continue;
		}

		//zvysime pocitadlo beziacich vlakien
		threadcount++;
	}

	//zatvorenie socketu
	close(listenSocket);

	while(threadcount);//cakame kym sa kazde vlano ukonci

	//uvolnime mutex
	pthread_mutex_destroy(&mailMutex);


    return 0;
}
