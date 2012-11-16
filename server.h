typedef struct _client_t
{
	char in_buffer[256];
	char out_buffer[256];
    int sockfd;
    FILE *file;
} client_t;