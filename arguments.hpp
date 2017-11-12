/*Riesenie: POP3 server
* Predmet: ISA
* Autor: Peter Šuhaj(xsuhaj02)
* Rocnik: 2017/2018
* Subor: arguments.hpp
*/

#ifndef ARGS_HPP
#define ARGS_HPP

#include <string>

using namespace std;
//trieda pre spracovanie argumentov
class arguments{
	//privatne premenne
	private:
		long port1;
		std::string maildir1;
		std::string authfile1;
		bool crypt1;
		bool reset1;
	
	public:
		//metody pre zistenie hodnot privatnych premennych
		int port();
		std::string maildir();
		std::string authfile();
		bool crypt();
		bool reset();
		//metoda pre spracovanie argumentov
		void parseArgs(int argc, char **argv);
		//konstruktor
		arguments(void);
		
};

#endif