# Subor: Makefile
# Riesenie: ISA - POP3 server 
# Autor: Peter Šuhaj(xsuhaj02), FIT

CC=g++

CFLAGS=-std=c++11 -Wall -Wextra -pedantic -pthread -lssl -lcrypto

PROJ=popser

all: $(PROJ)

arguments.o: arguments.cpp arguments.hpp
	$(CC) $(CFLAGS) -c arguments.cpp -o arguments.o

$(PROJ).o: $(PROJ).cpp $(PROJ).hpp
	$(CC) $(CFLAGS) -c $(PROJ).cpp -o $(PROJ).o

$(PROJ): $(PROJ).o arguments.o
	$(CC) $(CFLAGS) $(PROJ).o arguments.o -o $(PROJ)

clean:
	rm -f *.o

clean-all:
	rm -f *.o $(PROJ)
