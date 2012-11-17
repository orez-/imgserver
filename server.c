/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include "server.h"

sem_t my_sem;

pthread_t threads[MAX_THREADS];
queue_t queue;

void init_pthread_army()
{
    int i;
    for (i = 0; i < MAX_THREADS; i++)
    {
        pthread_create(&(threads[i]), NULL, run, NULL);
    }
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

int dumpFileToSocket(client_t *client)
{
    int n;
    // bzero(client->out_buffer, 256);  // zero the buffer
    while (!feof(client->file))
    {
        n = fread(client->out_buffer, 1, 256, client->file);  // read the data
        if (n < 0)
        {
            printf("ERROR reading from file\n");
            return -1;
        }
        if (n < 256)  // read fewer bytes
            bzero(client->out_buffer + n, 256 - n);  // clear out the last 256-n bytes
        n = write(client->sockfd, client->out_buffer, n);  // send the data
        if (n < 0)
        {
            printf("ERROR writing to socket\n");
            return -1;
        }
    }
    return 0;
}

void *run()
{
    int n;
    client_t *client = malloc(sizeof(client_t));
    // int sockfd = *(int *)sockfd_void;
    while (1)
    {
        sem_wait(&my_sem);    // wait for someone to be ready
        pthread_mutex_lock(&(queue.mutex));
        client->sockfd = pop_queue();
        pthread_mutex_unlock(&(queue.mutex));
        while (1)
        {   // actual stuff
            bzero(client->in_buffer, 256);
            memcpy(client->in_buffer, "imgs/", 5);
            n = read(client->sockfd, client->in_buffer + 5, 250);
            if (!n)    // guy quit
            {
                printf("Connection closed\n");
                break;
            }
            else if (n < 0)
            {
                printf("ERROR reading from socket\n");
                break;
            }

            trimwhitespace(client->in_buffer);
            printf("Locating image '%s'\n", client->in_buffer);

            client->file = fopen(client->in_buffer, "r");
            if (client->file == NULL)
            {
                printf("File does not exist\n");
                n = write(client->sockfd, "That file didn't exist", 22);
            }
            else
            {
                printf("I found the file\n");
                n = dumpFileToSocket(client);
                fclose(client->file);
            }
            if (n < 0)
            {
                printf("ERROR writing to socket\n");
                break;
            }
        }
        close(client->sockfd);
    }
    pthread_exit(0);
}

void init_queue()
{
    pthread_mutex_init(&(queue.mutex), NULL);
    queue.start = 0;
    queue.end = 0;
}

void push_queue(int fd)
{
    if ((queue.end + 1) % MAX_QUEUE != queue.start)
    {
        queue.fds[queue.end++] = fd;
        queue.end %= MAX_QUEUE;
    }
    // else: smarter stuff plz
}

int pop_queue()
{
    if (queue.end != queue.start)
    {
        int toR = queue.fds[queue.start];
        queue.start = (queue.start + 1) % MAX_QUEUE;
        return toR;
    }
    return -1;    // smarter stuff plz
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno;
    socklen_t clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int n;
    client_t *client;

    if (argc < 2) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }

    sem_init(&my_sem, 1, 0);
    init_pthread_army();    // TODO: this ought to be pthreaded
    init_queue();

    portno = atoi(argv[1]);

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
    {   // client catcher
        newsockfd = accept(sockfd, 
                    (struct sockaddr *) &cli_addr, 
                    &clilen);
        if (newsockfd < 0)
        {
            printf("ERROR on accept\n");
            continue;
        }

        pthread_mutex_lock(&queue.mutex);
        push_queue(newsockfd);    // TODO: we should block if there's no room in the queue
        pthread_mutex_unlock(&queue.mutex);
        sem_post(&my_sem);
    }
    close(sockfd);
    return 0;
}