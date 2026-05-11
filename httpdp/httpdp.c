/* Written by Folkert van Heusden */
/* MIT license */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

char *response = "HTTP/1.0 200 PDP11/70 says OK\r\n\r\n";
char *default_file = "index.html";

int
main(argc, argv)
int argc;
char *argv[];
{
    struct sockaddr_in address;
    int opt = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    signal(SIGPIPE, SIG_IGN);

    memset(&address, 0x00, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(80);
    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
	    perror("bind");
	    return 1;
    }
    listen(fd, 4);

    for(;;) {
        struct sockaddr_in caddress;
        int addrlen = sizeof(caddress);
        int ffd = -1;
        int cfd = accept(fd, (struct sockaddr*)&caddress, &addrlen);
        char buffer[128];
        int o = 0;
        /* request */
        buffer[0] = 0;
        while(strchr(buffer, '\r') == NULL) {
            int rc = read(cfd, &buffer[o], sizeof(buffer) - o - 1);
            if (rc <= 0) {
		    printf("get too long\n");
                buffer[0] = 0;
                goto cl;
            }
            o += rc;
            buffer[o] = 0;
        }
        if (buffer[0] && strlen(buffer) >= 5) {
           if (strncmp(buffer, "GET ", 4) == 0) {
              char *slash = NULL;
              char *url = &buffer[4];
              char *sp = strchr(url, ' ');
              if (!sp)
		      goto cl;  /* no HTTP... behind url */
	      *sp = 0x00;
              printf("GET for %s\n", url);
	      if (url[0] == '/')
		      url++;
	      if (strlen(url)) {
		      slash = strchr(url, '/');
		      if (slash)
			      goto cl;  /* prevent ../ attacks */
	      }
	      else {
		      url = default_file;
	      }
              write(cfd, response, strlen(response));
	      ffd = open(url, O_RDONLY);
	      for(;ffd != -1;) {
		      int rc = read(ffd, buffer, sizeof buffer);
		      if (rc <= 0)
			      break;
		      write(cfd, buffer, rc);
	      }
           }
        }

cl:
        close(cfd);
	if (ffd != -1)
		close(ffd);
    }

    return 0;
}
