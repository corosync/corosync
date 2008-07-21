OPTIONS = -Wall -I../../include

all: test_amf

serv_main.o: serv_main.c serv_main.h
	gcc $(OPTIONS) -c serv_main.c

serv_amf.o: serv_amf.c serv_amf.h
	gcc $(OPTIONS) -c serv_amf.c

test_amf: serv_main.o serv_amf.o
	gcc $(OPTIONS) -o test_amf serv_main.o serv_amf.o ../../lib/libSaAmf.a ../../lib/libSaCkpt.a -lpthread

clean:
	rm -f test_amf
	rm -f *.o
