/*Riesenie: POP3 server
* Predmet: ISA
* Autor: Peter Šuhaj(xsuhaj02)
* Rocnik: 2017/2018
* Subor: clients.hpp
*/

#ifndef CLIENTS_H
#define CLIENTS_H

#include <string>
#include <vector>
#include "popser.hpp"

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


commands hashCommand(std::string command);
states hashState(std::string state);
std::string absolutePath(const char *filename);
bool isnumber(std::string number);
int fileSize(const char *file);
int getVirtualSize(std::string filename);
void getFilesInCur(std::vector<std::string>& files,std::string maildir);
void createUIDL(char* filename, std::string& UIDL);
void md5hash(std::string stringToHash, std::string& hash);
bool checkMaildir(struct threadVar args);
bool mySend(int socket,const char *msg,size_t msgsize);
void* doSth(void *arg);

#endif