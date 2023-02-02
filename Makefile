all: client.c libmfs.c server.c udp.h udp.c mfs.h ufs.h mkfs.c
	gcc server.c udp.c -o server
	gcc -c -Wall -fpic libmfs.c udp.c
	gcc -shared -o libmfs.so libmfs.o
	gcc client.c udp.c -o client -L. -lmfs
	gcc mkfs.c -o mkfs

clean:
	rm libmfs.o libmfs.so server client mkfs udp.o