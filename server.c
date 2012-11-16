/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include "server.h"

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

char *trimwhitespace(char *str)
{
    char *end;
    // Trim leading space
    while(isspace(*str)) str++;
    if(*str == 0)  // All spaces?
    return str;
    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace(*end)) end--;
    // Write new null terminator
    *(end+1) = 0;
    return str;
}

void dumpFileToSocket(client_t *client)
{
    int n;
    // bzero(client->out_buffer, 256);  // zero the buffer
    while (!feof(client->file))
    {
        n = fread(client->out_buffer, 1, 256, client->file);  // read the data
        if (n < 0)
            error("ERROR reading from file");
        if (n < 256)  // read fewer bytes
            bzero(client->out_buffer + n, 256 - n);  // clear out the last 256-n bytes
        n = write(client->sockfd, client->out_buffer, n);  // send the data
        if (n < 0)
            error("ERROR writing to socket");
    }
}

void run(int portno)
{
    struct sockaddr_in serv_addr, cli_addr;
    int n;
    int sockfd;
    client_t *client;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
            sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    listen(sockfd, 5);
    printf("You listened!\n");
    clilen = sizeof(cli_addr);

    while (1)
    {
        client->sockfd = accept(sockfd, 
                    (struct sockaddr *) &cli_addr, 
                    &clilen);
        if (client->sockfd < 0) 
            error("ERROR on accept");
        client = malloc(sizeof(client_t));
        printf("Oh you accepted\n");
        bzero(buffer, 256);
    }
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno;
    socklen_t clilen;
    char buffer[256];

    if (argc < 2) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }

    portno = atoi(argv[1]);

    buffer[0] = 'i';
    buffer[1] = 'm';
    buffer[2] = 'g';
    buffer[3] = 's';
    buffer[4] = '/';

    n = read(newsockfd, buffer + 5, 250);
    if (n < 0) error("ERROR reading from socket");


    trimwhitespace(buffer);
    printf("Locating image '%s'\n", buffer);

    FILE *file = fopen(buffer, "r");
    if (file == NULL)
    {
        printf("File does not exist\n");
        n = write(newsockfd, "That file didn't exist", 22);
    }
    else
    {
        printf("Here is the message: %s\n",buffer);
        n = write(newsockfd,"I got your image",16);
        fclose(file);
    }
    if (n < 0) error("ERROR writing to socket");
    close(newsockfd);
    close(sockfd);
    return 0;
}