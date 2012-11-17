#define MAX_THREADS 100
#define MAX_QUEUE 10

typedef struct _client_t
{
	char in_buffer[256];
	char out_buffer[256];
    int sockfd;
    FILE *file;
} client_t;

typedef struct _queue_t
{
	int fds[MAX_QUEUE];
	int start;
	int end;
	pthread_mutex_t mutex;
} queue_t;


void init_pthread_army();
char *trimwhitespace(char *str);
int dumpFileToSocket(client_t *client);
void *run();
void init_queue();
void push_queue(int fd);
int pop_queue();