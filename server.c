#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <sys/wait.h>

#define err_exit(msg)   \
  {                     \
    perror(msg);        \
    exit(EXIT_FAILURE); \
  }

#define MAX_CLIENTS 4
#define SERVER_PORT 0x3333
#define BUFFER_LEN 1024

typedef struct client
{
  unsigned long id;
  struct sockaddr_in addr;
  socklen_t addr_len;
  struct client *next;
} client_t;

typedef struct
{
  pthread_t tid;
  unsigned long client_id;
  struct sockaddr_in client_addr;
  char command[BUFFER_LEN];
} request_t;

void addClient(struct sockaddr_in addr, socklen_t addr_len);
void removeClient(unsigned long id);
client_t *getClient(unsigned long id);
void *handleRequest(void *args);

int sockfd;               // The server's socket descriptor
int num_clients = 0;      // The total number of connected clients
client_t *Clients = NULL; // The list of connected clients

int main(int argc, char **argv)
{
  request_t *req;
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  socklen_t client_addr_len;

  // Create a socket descriptor
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    err_exit("socket");

  // Clear the server and client addresses
  bzero(&server_addr, sizeof(server_addr));
  bzero(&client_addr, sizeof(client_addr));
  client_addr_len = sizeof(struct sockaddr_in);

  // Set up the server address
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(SERVER_PORT);

  // Bind the server address to the socket descriptor
  if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    err_exit("bind");

  // Forever wait for requests and service them
  for (;;)
  {
    if ((req = malloc(sizeof(request_t))) == NULL)
      err_exit("malloc");

    if (recvfrom(sockfd, req, sizeof(request_t), 0, (struct sockaddr *)&client_addr, &client_addr_len) == -1)
      err_exit("recvfrom");
    
    // Run server commands
    if (!strcmp(req->command, "connect"))
    {
      free(req);
      addClient(client_addr, client_addr_len);
    }
    else if (!strcmp(req->command, "quit"))
    {
      removeClient(req->client_id);
      free(req);
    }
    else
    {
      // Create new thread for new client
      memcpy(&req->client_addr, &client_addr, sizeof(client_addr));
      pthread_create(&req->tid, NULL, handleRequest, req);
    }
  }

  return 0;
}

/*
 *Adds a new client with the given address to the list of connected clients
 *and responds with success and communication handle or failure message to the client.
 */
void addClient(struct sockaddr_in addr, socklen_t addr_len)
{
  client_t *temp = Clients;
  client_t *new_client;
  char buffer[BUFFER_LEN];

  // Check if there's already max clients.
  if (num_clients == MAX_CLIENTS){
    sprintf(buffer, "FAILED: %s","Maximum clients allowed is 4.\n");
    sendto(sockfd, buffer, BUFFER_LEN,
           0, (struct sockaddr *)&addr, addr_len);
    return;
  }

  // Create a new client with the given address
  if ((new_client = malloc(sizeof(client_t))) == NULL)
    err_exit("malloc");
  new_client->id = time(NULL) * random(); // Generate a unique handle
  new_client->addr = addr;
  new_client->addr_len = addr_len;
  new_client->next = NULL;

  // Append the client to the list of other clients
  if (Clients == NULL)
    Clients = new_client;
  else
  {
    while (temp->next != NULL)
      temp = temp->next;
    temp->next = new_client;
  }

  sprintf(buffer, "SUCCESS, ID: %lu", new_client->id);
  sendto(sockfd, buffer, BUFFER_LEN,
         0, (struct sockaddr *)&addr, addr_len);
  num_clients++;
}

/*
 * Get the client with the given id from the list of connected clients.
 */
client_t *getClient(unsigned long id)
{
  client_t *temp = Clients;

  while (temp != NULL && temp->id != id)
    temp = temp->next;
  return temp;
}

/*
 *Removes a client with the given id from the list of connected clients.
 */
void removeClient(unsigned long id)
{
  client_t *temp = Clients;
  client_t *ptr = NULL;

  // If it's the first in the list
  if (id == temp->id)
  {
    Clients = temp->next;
    num_clients--;
    free(temp);
  }
  else
  {
    // Search for the client
    while (temp && temp->next != NULL && ptr == NULL)
    {
      if (id == temp->next->id)
      {
        ptr = temp->next;
        temp->next = temp->next->next;
      }
      temp = temp->next;
    }
    if(ptr != NULL)
    {
      num_clients--;
      free(ptr);
    }
  }
}

/*
 * Handles the request sent by the client with the address in args.
 */
void *handleRequest(void *args)
{
  pid_t pid;
  int pipefd[2];
  request_t *req = (request_t *)args;
  client_t *client = getClient(req->client_id);
  size_t nbytes;
  char buffer[BUFFER_LEN];
  
  // Confirm the client exists and has been consistent with their address and port.
  if (!(client && memcmp(&client->addr.sin_addr, &req->client_addr.sin_addr,
   sizeof(client->addr.sin_addr)) == 0 && client->addr.sin_port == req->client_addr.sin_port))
  {
    sprintf(buffer,"%s", "NOT ALLOWED!");
    sendto(sockfd, buffer, BUFFER_LEN, 0, (const struct sockaddr *)&client->addr, client->addr_len);
    free(req);
    return NULL;
  }
  
  if (pipe(pipefd) == -1)
    err_exit("pipe");

  // Fork a child process to execute the command
  if ((pid = fork()) == -1)
    err_exit("fork");
  if (pid == 0)
  {
    // Child process
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    char *args[] = {"sh", "-c", req->command, NULL};
    execvp(args[0], args);
  }
  else
  {
    // Parent process
    close(pipefd[1]);
    while ((nbytes = read(pipefd[0], buffer, BUFFER_LEN)) > 0)
      if (sendto(sockfd, buffer, nbytes, 0,
        (const struct sockaddr *)&client->addr, client->addr_len) == -1)
        return  NULL;

    waitpid(pid, NULL, 0);
    free(req);
  }

  return NULL;
}
