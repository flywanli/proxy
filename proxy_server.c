#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <netdb.h>
#include <time.h>

#define Insert 0
#define Miss 1
#define Del 2
#define Show 3

#define BUFFER_SIZE 2048000
#define cachesize 10
int cachenum = 0;
fd_set fds_all, fds_read;


typedef struct request_line {// store the parameters from the client
	char * address;
	char * resource;
	time_t expires;
	char * accessed;
}request_line;


typedef struct pagelist { // Cache entry structure
	request_line req;
	struct pagelist *nextpage;
	struct pagelist *prevpage;
} pagelist;

pagelist *headpage = NULL;
void fix(char * file) {
	char* m = file;
	char* n = file;
	while (*n != 0) {
		*m = *n++;
		if (*m != ' ') m++;
	}
	*m = 0;
}
request_line dewebcodereq(char buffer[]) {
	char *token;
	token = strtok(strdup(buffer), "\r\n");
	request_line req;
	while (token != NULL)
	{
		char *line = malloc(sizeof(char) * strlen(token));
		line = token;
		if (strncmp(line, "GET", 3) == 0) {
			char * getline = line + 4;
			getline[strlen(getline) - 8] = '\0';
			req.resource = getline;
		}
		else if (strncmp(line, "Host: ", 5) == 0) req.address = line + 6;
		token = strtok(NULL, "\r\n");
	}
	fix(req.resource);
	fix(req.address);
	req.expires = 0; // Default params; modified later
	req.accessed = "Never";
	return req;

}

void Sendtoclient(request_line  req, int client_fd) {
	char * file_name = malloc(strlen(req.address) + strlen(req.resource) + 1);
	int buf;
	// convert to a suitable format
	char resource[strlen(req.resource)];
	int i;
	char *str1 = "/";
	char *str2 = "_";
	strcpy(file_name, req.address);
	strcpy(resource, req.resource);
	for (i = 0; i<strlen(resource); i++) {
		if (strncmp(resource + i, str1, 1) == 0) strncpy(resource + i, str2, 1);
	}
	strcat(file_name, resource);
	fix(file_name);
	FILE *file = fopen(file_name, "rb");
	if (file != NULL) {
		while (fread(&buf, 1, 1, file)>0) {
			if ((send(client_fd, &buf, 1, 0)) == -1) {
				perror("Serve failed!");
			}
		}
	}
	FD_CLR(client_fd, &fds_all);
	close(client_fd);
}



int Querycache(int stat, request_line request) {
	pagelist *presentpage;
	pagelist *t1, *t2;

	switch (stat) {
	case Insert: // insert page to the cache
		presentpage = (pagelist*)malloc(sizeof(pagelist));
		presentpage->req.address = request.address;
		presentpage->req.resource = request.resource;
		presentpage->req.expires = request.expires;
		time_t present;
		time(&present);
		struct tm *tim = gmtime(&present);
		char * buf = malloc(100 * sizeof(char));
		strftime(buf, 80, "%a, %d %b %Y %H:%M:%S %Z", tim);
		printf("Cache Lastmodified Time is: %s\n", buf);
		presentpage->req.accessed = buf; // Make last-accessed time = current time

		if (headpage == NULL) {
			presentpage->nextpage = NULL;
			presentpage->prevpage = NULL;
			headpage = presentpage;
		}
		else { // Push node to top
			headpage->prevpage = presentpage;
			presentpage->nextpage = headpage;
			presentpage->prevpage = NULL;
			headpage = presentpage;
		}
		cachenum++;
		if (cachenum == cachesize + 1) { // Max limit for the cache is hit; delete the LRU entry
			presentpage = headpage;
			int m;
			for (m = 0; m<cachesize - 1; m++)
				presentpage = presentpage->nextpage;
			presentpage->nextpage = NULL;
			cachenum = cachesize;
		}
		return 0;
	case Del: // Delete a page in cache
		presentpage = headpage;
		while (presentpage != NULL) {
			if (!strcmp(presentpage->req.address, request.address) && !strcmp(presentpage->req.resource, request.resource)) {
				t1 = presentpage->prevpage;
				t2 = presentpage->nextpage;
				if (t1 != NULL)t1->nextpage = t2; else headpage = t2;
				if (t2 != NULL)t2->prevpage = t1;
				cachenum--;
				break;
			}
			presentpage = presentpage->nextpage;
		}

	case Miss: // if the page is in the cache
		presentpage = headpage;
		while (presentpage != NULL) {
			if (!strcmp(presentpage->req.address, request.address) && !strcmp(presentpage->req.resource, request.resource)) // Cache Hit
			{
				time_t present;
				time(&present);
				char * buff = malloc(100 * sizeof(char));
				struct tm *tim = gmtime(&present);
				//      printf("Present time is %lld\n",mktime(tim));
				strftime(buff, 80, "%a, %d %b %Y %H:%M:%S %Z", tim);
				printf("Present time is: %s\n", buff);

				printf("Cache has this item!\n");

				if (mktime(tim) > presentpage->req.expires) { // Expired
					char * buffer = malloc(100 * sizeof(char));
					time(&presentpage->req.expires);
					struct tm *tim = gmtime(&presentpage->req.expires);
					strftime(buffer, 80, "%a, %d %b %Y %H:%M:%S %Z", tim);
					//printf("Lastmodified time is: %s\n", buffer);
					printf("Page has expired!\n");
					t1 = presentpage->prevpage;
					t2 = presentpage->nextpage;
					if (t1 != NULL)t1->nextpage = t2; else headpage = t2;
					if (t2 != NULL)t2->prevpage = t1;
					cachenum--;
					return 1;
				}
				else { // Not expired
					printf("The page has not expired!\n");
					return 0;
				}
			}
			presentpage = presentpage->nextpage;
		}
		return 2; // Cache Miss

	default: return 1;
	}
}

//listen from the internet server
void listenfrom(int web_fd, request_line req, int client_fd, int stat) {
	char * file_name = malloc(strlen(req.address) + strlen(req.resource) + 1);
	//url2filename(filename, req);
	// conver URL to string
	char resource[strlen(req.resource)];
	int i;
	char *str1 = "/";
	char *str2 = "_";
	strcpy(file_name, req.address);
	strcpy(resource, req.resource);
	for (i = 0; i<strlen(resource); i++) {
		if (strncmp(resource + i, str1, 1) == 0) strncpy(resource + i, str2, 1);
	}
	strcat(file_name, resource);
	fix(file_name);

	int nbytes;
	int iswebcode = 0;
	int isexpire = 0;
	char *webcode = NULL;
	char *expires = NULL;
	char *tk1 = NULL;
	char *tk2 = NULL;

	char content[BUFFER_SIZE];
	memset(content, 0, BUFFER_SIZE);
	char tempstr[10];
	sprintf(tempstr, "tempfile%d", client_fd); // Temporary file
	FILE *tempfile = fopen(tempstr, "wb");
	while ((nbytes = recv(web_fd, content, BUFFER_SIZE, 0)) > 0) {
		if (iswebcode == 0) {
			tk1 = strtok(strdup(content), "\r\n");
			*(tk1 + 12) = '\0';
			webcode = tk1 + 9;
			iswebcode = 1;
		}
		if (isexpire == 0) {
			tk2 = strtok(strdup(content), "\r\n");
			while (tk2 != NULL) {
				if (strncmp(tk2, "Expires: ", 9) == 0) {
					expires = tk2 + 9;
					isexpire = 1;
					break;
				}
				tk2 = strtok(NULL, "\r\n");
			}
		}
		fwrite(content, 1, nbytes, tempfile);
		memset(content, 0, BUFFER_SIZE);
	}

	fclose(tempfile);
	struct tm tim;
	bzero((void *)&tim, sizeof(struct tm));
	printf("Page will expires: %s\n", expires);
	if (expires != NULL && strcmp(expires, "-1") != 0) {
		strptime(expires, "%a, %d %b %Y %H:%M:%S %Z", &tim);
		req.expires = mktime(&tim);
	}
	else req.expires = 0;

	close(web_fd);

	printf(" Page has received from the web server: %s\n", webcode);
	if (nbytes < 0) {
		perror("Receive Error!\n");
		exit(0);
	}
	else {
		if (strcmp(webcode, "304") != 0) { // Response webcode not 304
			if (access(file_name, F_OK) != -1) if (remove(file_name) != 0) printf("Cache: File delete error!\n"); // Save file only if response != 304
			if (rename(tempstr, file_name) != 0) printf("Cache: File cannot be renamed!\n");
		}

		if (stat == 0) Querycache(Del, req);
		if (stat == 1) Querycache(Del, req);
		Querycache(Insert, req);
		Sendtoclient(req, client_fd);
		printf(" Page has been sennd to client!\n\n");
		printf("-------------------END----------------------\n");
	}
}





// creat a socket to connecet to the web server and get webpage
void Processgetpage(request_line req, int client_fd, int stat) {
	int rv_fd, web_fd;
	struct addrinfo hints, *servinfo, *p;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if ((rv_fd = getaddrinfo(req.address, "80", &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv_fd));
		exit(1);
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((web_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("creat socket error");
			continue;
		}
		if (connect(web_fd, p->ai_addr, p->ai_addrlen) == -1) {
			close(web_fd);
			perror("client connect error");
			continue;
		}
		break;
	}
	freeaddrinfo(servinfo);
	//prepare the address and request to send to the webserver
	char *resource = req.resource;
	if (resource == NULL)
		resource = "";
	else if (*resource == '/')
		resource++;
	char * Msg;
	pagelist *here = headpage;
	while (here != NULL) { // Finding last-accessed time for the requested page in cache
		if (!strcmp(here->req.address, req.address) && !strcmp(here->req.resource, req.resource)) break;
		here = here->nextpage;
	}
	printf("status: %d\n", stat);
	if (stat == 0 && here != NULL) {
		printf("Page was in cache, directly send to client fron cache\n");
		Sendtoclient(req, client_fd);
		printf("Page has been sennd to client!\n\n");
		printf("-------------------END----------------------\n");

	}
	else if (stat == 2) {
		printf("GET page from the webserver\n\n");
		char *template = "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n";
		Msg = (char *)malloc(strlen(template) - 5 + strlen(req.address) + strlen(resource) + strlen("ECEN602"));
		sprintf(Msg, template, resource, req.address, "ECEN602");
		if ((send(web_fd, Msg, strlen(Msg), 0)) == -1) {
			perror("GET Failed!");
			exit(1);
		}
		// receive from the wen server
		else { listenfrom(web_fd, req, client_fd, stat); }

	}
	else {//conditional Get
		  //      printf("Conditional GET\n\n");
		  //      char *template = "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\nIf-Modified-Since: %s\r\n\r\n";
		char *template = "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n";
		//      Msg = (char *)malloc(strlen(template)+strlen(req.address)+strlen(resource)+strlen("ECEN602")+strlen(here->req.accessed));
		Msg = (char *)malloc(strlen(template) - 5 + strlen(req.address) + strlen(resource) + strlen("ECEN602"));
		//      sprintf(Msg, template, resource, req.address, "ECEN602", here->req.accessed);
		sprintf(Msg, template, resource, req.address, "ECEN602");
		printf("Conditional Get works, get the page from web server\n");


		if ((send(web_fd, Msg, strlen(Msg), 0)) == -1) {
			perror("GET Failed!");
			exit(1);
		}
		// recieve from the webserver
		else { listenfrom(web_fd, req, client_fd, stat); }
	}
}

int main(int argc, char const *argv[])
{
	struct sockaddr_in serv_addr, client_addr;
	struct hostent *server;
	//fd_set fds_all, fds_read;
	int sock_fd, client_fd;
	int Isrec;
	int client_len;
	int fdmax, i;
	//int clientfd[50];
	//char URL[10000];
	unsigned int port;
	//const char* server_1;

	if (argc != 3) {
		printf("Usage './proxy <IP TO BIND> <PORT TO BIND>\n");
		exit(1);
	}
	else {
		port = atoi(argv[2]);
		server = gethostbyname(argv[1]);
		if (server == NULL)
		{
			fprintf(stderr, "ERROR,no such host\n");
			exit(0);
		}
	}
	bzero((char*)&serv_addr, sizeof(struct sockaddr_in));
	serv_addr.sin_family = AF_INET;//set family
	serv_addr.sin_port = htons(port); //set the port
									  //serv_addr.sin_addr.s_addr = htonl(inet_network(server)); //set the ip address.
									  //serv_addr.sin_addr.s_addr = INADDR_ANY;
	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
	printf("<[proxy server is running ]>\n\n");

	if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) //Socket Function
	{
		printf("Socket cannot be created \n");
		exit(1);
	}
	if (bind(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) //bind funciton
	{
		printf("Cannot bind to the socket \n");
		exit(1);
	}

	if ((listen(sock_fd, 5)) < 0) {
		printf("ERROR listenning\n");
		exit(1);
	}

	FD_ZERO(&fds_all);
	FD_ZERO(&fds_read);

	FD_SET(sock_fd, &fds_all);
	int client_count = 0;
	fdmax = sock_fd;
	while (1) {
		client_len = sizeof(client_addr);
		fds_read = fds_all;

		int fd_count = select(fdmax + 1, &fds_read, NULL, NULL, NULL);
		if (fd_count == -1) {
			printf("Error in select call");
			break;
		}
		for (i = 0; i < fdmax + 1; i++) {
			char buffer[BUFFER_SIZE];
			if (FD_ISSET(i, &fds_read)) {
				if (sock_fd == i) {
					client_fd = accept(sock_fd, (struct sockaddr*)&client_addr, &client_len);
					if (client_fd == -1) {
						printf("Error in connection\n");
						exit(1);
					}
					else {
						FD_SET(client_fd, &fds_all);
						//clientfd=client_fd;
						//clientfd++;
						printf("Client has connectted to proxy server successfully\n");
						if (client_fd>fdmax)
						{
							fdmax = client_fd;
						}
					}
				}
				else
				{
					/* deal with the data from the client*/
					//recvAndProcessServer(i);
					if ((Isrec = recv(i, buffer, BUFFER_SIZE, 0))>0) {
						if (strncmp(buffer, "GET", 3) == 0) {
							request_line req = dewebcodereq(buffer);
							printf("Resource address is: %s\n", req.address);
							printf("Resuroce is: %s\n", req.resource);
							int stat = Querycache(Miss, req);// see if the requset page is in cach
							if (stat == 2) printf("cache don't have this item!\n");// stat=0 cache have
							Processgetpage(req, i, stat);
						}
						//printf("")

					}
				}
			}
		}
	}
	close(sock_fd);
	return 0;
}
