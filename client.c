#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <signal.h>


#define err_exit(msg)   \
  {                     \
    perror(msg);        \
    exit(EXIT_FAILURE); \
  }

#define SERVER_PORT 0x3333
#define BUFFER_LEN 1024

typedef struct
{
  pthread_t tid;
  unsigned long client_id;
  struct sockaddr_in client_addr;
  char command[BUFFER_LEN];
} request_t;

int sockfd; //socket file descriptor
unsigned long id;               // The handle used for communication
struct sockaddr_in server_addr; // The remote host
char prompt[50] = {"client shell$"};

void commandLoop(int sockfd, struct sockaddr_in server_addr, socklen_t server_addr_len);
void handle_sigint(int sig);

int main(int argc, char **argv)
{
  char buffer[BUFFER_LEN];
  // struct hostent* host;
  struct hostent *gethostbyname(), *host;
  char hostname[20]; //store host information
  socklen_t server_addr_len; // store IP address and length of the server
  request_t req; //store information about request

  // Create a socket descriptor
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    err_exit("socket");

for (;;)
{
  printf("%s", prompt); //prints the value of the prompt 
  fgets(req.command, BUFFER_LEN, stdin); 
  req.command[strcspn(req.command, "\n")] = '\0'; // remove the newline character from the end of the string

  if (!strcmp(req.command, "help")) // checks if the string stored in request command is equal to string help
  {
    printf("help: Show this help message.\n"); // prints help message
    printf("connect [hostname]: Connect to the remote server.\n"); 
    printf("quit: Exit from the current shell.\n");
  }
  else if (strstr(req.command, "connect")) // checks if string stored in req. command contains substring connect 
  {
    if (sscanf(req.command, "connect %s", hostname) != 1) // uses scanf to parse the req.command  string and extract the hostname substring
    {
      printf("Usage: connect [hostname]\n"); // prints an error message to the console
      continue;
    }

    if ((host = gethostbyname(hostname)) == NULL) // to retrieve information about the host 
    {
      perror("gethostbyname"); // prints an error message to the console indicating  that there was an error with gethostbyname
      continue;
    }

    bzero(&server_addr, sizeof(server_addr)); // to set all bytes of server_addr to zero
    // bcopy(host->h_addr_list, (char *)&server_addr.sin_addr, host->h_length);
    server_addr.sin_addr = *(struct in_addr *)host->h_addr_list[0]; // sets IP address of the server to the first IP address in host address
    server_addr.sin_family = AF_INET; // sets the address family of the server
    server_addr.sin_port = htons(SERVER_PORT); // sets the port number of the server
    server_addr_len = sizeof(server_addr); // sets the length of the server address

    req.command[strlen("connect")] = '\0'; // sets a null terminator character in req.command string after the ''connect'' substring
    if (sendto(sockfd, &req, sizeof(request_t), 0, (struct sockaddr *)&server_addr, server_addr_len) == -1) // sends the contents to the server
      err_exit("send_to"); // calls err_exit function with error message "send_to" which prints the message and exit the program
    bzero(buffer, BUFFER_LEN); // set all bytes in buffer to zero
    recvfrom(sockfd, &buffer, BUFFER_LEN, 0, NULL, NULL); // receives data from server and stores in the buffer 
    if (sscanf(buffer, "SUCCESS, ID: %lu", &id) == 1) // uses scanf to parse the buffer and extract an ID
    {
      sprintf(prompt, "client shell@%s$", hostname); // to set the prompt for the command loop 
      commandLoop(sockfd, server_addr, server_addr_len); // calls commandloop function with socket file descriptor and server address information
    }
    else
      printf("%s\n", buffer);
  }
  else if (!strcmp(req.command, "quit") || !strcmp(req.command, "disconnect")) // checks if user entered a quit command and breaks out of loop if true
    break;
  else
    printf("Invalid command.\n"); // prints an error message to the console for an invalid command
}

return 0;
}

/*
 * Interact with the server by sending commands and printing response to stdout.
 */
void commandLoop(int sockfd, struct sockaddr_in server_addr, socklen_t server_addr_len) // to accept user command and sends them to server
{
  fd_set read_fds; // to hold file descriptors for reading 
  int ready, len; // holds the length of the received message
  char buffer[BUFFER_LEN]; // to hold message received from server
  request_t req; // to hold users request

  // Handle Ctrl-c
  signal(SIGINT, handle_sigint);
  write(STDOUT_FILENO, prompt, strlen(prompt)); // writes the prompt string to standard output stream

for (;;)
  {
    FD_ZERO(&read_fds);
    FD_SET(0, &read_fds);
    FD_SET(sockfd, &read_fds); // initialises read_fds and add file descriptors 
    if ((ready = select(sockfd + 1, &read_fds, NULL, NULL, NULL)) == -1) // waits for any of the file descriptors  to be ready 
      err_exit("select");

    // Read stdin for command
    if (FD_ISSET(0, &read_fds)) // checks if standard input file descriptor is ready for reading
    {
      bzero(&req, sizeof(request_t)); // set the variable req to zero
      fgets(req.command, BUFFER_LEN, stdin); // 
      if (strlen(req.command) > 0)
      {
        // remove newline character from input
        if (req.command[strlen(req.command)-1] == '\n')
          req.command[strlen(req.command)-1] = '\0';
        
        req.client_id = id;
        if (strstr(req.command, "connect")) // checks if command field of the request  contains the substring "connect"
        {
          printf("You are already connected.");
          continue;
        }
        if (sendto(sockfd, &req, sizeof(request_t), 0, (const struct sockaddr *)&server_addr, server_addr_len) == -1)
          err_exit("sendto"); // check if "sendto" function was successful , if not exit the program with the error message  
        if (!strcmp(req.command, "quit")) // check if command feild is equal to the quit 
        {
          sprintf(prompt, "%s>", "client shell"); // to create a prompt string of the form 
          return;
        }

      }
      else
        write(STDOUT_FILENO, prompt, strlen(prompt)); // write prompt string to stdout
    }

    // Read output from remote host
    if (FD_ISSET(sockfd, &read_fds)) // checks if data is available to read from the socket 
    {
      if ((len = recvfrom(sockfd, buffer, BUFFER_LEN, MSG_DONTWAIT, NULL, NULL)) < 0)
      {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
          continue;
        else
          err_exit("recvfrom");
      }
      write(STDOUT_FILENO, buffer, len);
      write(STDOUT_FILENO, prompt, strlen(prompt));
    }
  }
}

void handle_sigint(int sig) {
  request_t req = {.client_id = id};
  strncpy(req.command, "quit", 5);
  sendto(sockfd, &req, sizeof(request_t), 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
  exit(0);
}
