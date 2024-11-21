CC = gcc
HTTPD_SERVER = httpd 
HTTPD_SERVER_OBJS = httpd.o net.o

httpd :

$(HTTPD_SERVER) : $(HTTPD_SERVER_OBJS)
	$(CC) $(HTTPD_SERVER_OBJS) -o $(HTTPD_SERVER)

httpd.o : httpd.c net.h
	$(CC) -c httpd.c -o httpd.o

net.o : net.c net.h
	$(CC) -c net.c -o net.o


clean :
	rm *.o $(HTTPD_SERVER)
