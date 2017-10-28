CC=g++
CFLAGS=-std=c++11 -Wall -Wextra -pedantic -pthread -lssl -lcrypto -g
PROJ=popser

all: $(PROJ)

$(PROJ): $(PROJ).cpp 
	$(CC) $(CFLAGS) -o $(PROJ) $(PROJ).cpp
clean:
	rm -f *.o $(PROJ)
