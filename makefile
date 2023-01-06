# make rule primaria con dummy target ‘all’--> non crea alcun file all ma fa un complete build
# che dipende dai target client e server scritti sotto
all: device server
# make rule per il client
device: device.c utility_d.c
	gcc -o dev device.c -g -Wall
# make rule per il server
server: server.c utility_s.c
	gcc -o serv server.c -g -Wall
# pulizia dei file della compilazione (eseguito con ‘make clean’ da terminale)
clean:
	rm *.o dev serv