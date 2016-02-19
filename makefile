client.o:
	gcc -Wall   -o  client  http_client.c
server.o: 
	gcc -Wall   -o  server  proxy_server.c
clean:
	rm -rf server
	rm -rf client
