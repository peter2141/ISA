/*Riesenie: POP3 server
* Predmet: ISA
* Autor: Peter Šuhaj(xsuhaj02)
* Rocnik: 2017/2018
* Subor: arguments.cpp
*/

#include <string>
#include <string.h>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "arguments.hpp"

using namespace std;

extern pthread_mutex_t mailMutex;

//konstruktor - inicializacia potrebnych privatnych premennych
arguments::arguments(string path){
   	arguments::crypt1=false;
	arguments::reset1=false;
	arguments::binarypath = path;
}

//metody pre zistenie hodnot privatnych premennych
int arguments::port(){
	return (int)port1;
}
string arguments::maildir(){
	return maildir1;
}
string arguments::authfile(){
	return authfile1;
}
bool arguments::crypt(){
	return crypt1;
}
bool arguments::reset(){
	return reset1;
}

//metoda pre spracovanie argumentov
void arguments::parseArgs(int argc, char **argv){
	
	//hladanie parametru -h(rezim 1)
	for(int i = 1; i < argc; i++){
		if(string(argv[i]) == "-h"){
			cout << "\nToto je POP3 server\n" << endl;
			cout << "Použitie: \n"  << endl;
			cout << "./popser -h pre výpis nápovedy.\n" << endl;
			cout << "./popser -r pre vykonanie resetu.\n" << endl;
			cout << "./popser -d path_to_maildir -a path_to_authfile -p portnum [-c] [-r] pre spustenie serveru" << endl;
			cout << "Pri zadani parametru -r sa vykoná reset, pri zadaní -c je povolená iba nešifrovaná autentifikácia.\n" << endl;
			pthread_mutex_destroy(&mailMutex);
			exit(0);
		}
	}

	//hladanie parametru -r(rezim 2)
	if(argc==2 && (string(argv[1])=="-r")){
		ifstream resetIn;
		ifstream deleteIn;
		resetIn.open(binarypath + "reset.txt", ios::in);
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
				struct stat buffer;   
				//ak subor uz neexistuje nerobime nic
				if(stat(filename.c_str(), &buffer) != 0){
					continue;
				}
				tmppath = filename;

				//prepis absolutnej cesty s /cur na /new
				pos = filename.rfind("/cur/");//hladame posledny vyskyt cur--treba osetrovat vobec?
				string tmpfilename2 = filename;
				tmpfilename2.replace(pos,5,"/new/");
				//presun suborov z cur do new
				int res = rename(filename.c_str(), tmpfilename2.c_str());
				if(res != 0){ 
					cerr << "chyba pri premenovani(prsune) z cur do new" << endl;
					pthread_mutex_destroy(&mailMutex);
					exit(1);
				}
				tmppath.erase(pos,string::npos);//vymazeme z cesty suboru vsetko od Maildir-ziskame cestu k maildiru, potreba pri mazani moznych suborov co sotali v cur
			}
			resetIn.close();

			//odstranenie pomocnych suborov 
			string tempname = binarypath + "reset.txt";
			if(remove(tempname.c_str())!=0){
				cerr << "Chyba pri mazani pomocneho suboru na ukladanie presunov z new do cur" << endl;
			}
			tempname = binarypath + "info.txt";
			if(remove(tempname.c_str())!=0){
				cerr << "Chyba pri mazani pomocneho suboru na ukladanie informacii o mailov" << endl;
			}

			
		}

		pthread_mutex_destroy(&mailMutex);
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
					pthread_mutex_destroy(&mailMutex);
					exit(1);
				default: 
					abort();
			}
		}
		//kontrola ci boli zadane povinne parametre
		if(!(a && d && p)){
			cerr << "Nespravne parametre. \nMusite zadat cislo portu, cestu k Maildir a autentifikacny subor.\nPre informacie o spusteni spustite program s prepinacom -h."<<endl;
			pthread_mutex_destroy(&mailMutex);
			exit(1);
		}
		//kontrola spravneho portu
		if(*ptr != '\0'){
			cerr << "Chyba. Port obsahuje znak, moze obsahovat iba cisla!!!"<<endl;
			pthread_mutex_destroy(&mailMutex);
			exit(1); 
		}
	}
}

