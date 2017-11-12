/*Riesenie: POP3 server
* Predmet: ISA
* Autor: Peter Šuhaj(xsuhaj02)
* Rocnik: 2017/2018
* Subor: popser.hpp
*/

#ifndef POPSER_HPP
#define POPSER_HPP

//makro na velkost bufferu pre prijimanie
#define BUFSIZE  1024
//makro pre frontu pri select
#define QUEUE	2



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
	std::string username;
	std::string password;
	std::string maildir;
};

#endif