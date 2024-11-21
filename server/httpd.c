#define _GNU_SOURCE
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>

#define WEBPATH "src" 
#define BUFSIZE 1024

volatile sig_atomic_t interrupt_flag = 0;

void sigint_handler() {
    interrupt_flag = 1;
}

void sigchld_handler() {
    while (1) {
        pid_t pid = waitpid(-1, NULL, WNOHANG);
        if (pid <= 0) {
            break;
        }
        printf("Reaped child process %d\n", pid);
    }
}

void request_processor(char *request, ssize_t num, int nfd) {
    char *arglist[3];
    char *token = request;

    char *not_found = "HTTP/1.0 404 NOT FOUND\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";
    char *bad_request = "HTTP/1.0 400 BAD REQUEST\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";
    char header[256] = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n";

    int index = 0;
    while ((token = strsep(&request, " ")) != NULL) {
        arglist[index] = token;
        index++;
    }

    if (index < 3) {
        printf("Invalid argument size");
        return;
    }

    if ((strncmp(arglist[1], "/cgi-like/", 10) == 0) && (strcmp(arglist[0], "GET") == 0)) {
        char *command_args[BUFSIZE];
        char *temp = malloc(strlen(arglist[1]) + 1);

        if (temp == NULL) {
            perror("malloc failed");
            return;
        }

        strcpy(temp, arglist[1]);
        memset(arglist[1], 0, strlen(arglist[1]));
        strncpy(arglist[1], temp + 10, strlen(temp) - 10);

        token = arglist[1];

        index = 0;
        while ((token = strsep(&arglist[1], "?&")) != NULL) {
            if (strcmp(token, "cgi-like") == 0) {
                continue;
            }
            command_args[index] = token;
            index++;
        }

        DIR *cgi_like_dir = opendir("cgi-like/");
        struct dirent *dp;

        if (cgi_like_dir == NULL) {
            perror("Opendir failed");
            return;
        }

        int command_exists = 0;
        while ((dp = readdir(cgi_like_dir)) != NULL) {
            if (strcmp(command_args[0], dp->d_name) == 0) {
                command_exists = 1; 
                break;
            }
        }

        closedir(cgi_like_dir);

        if (command_exists == 0) {
            write(nfd, bad_request, strlen(bad_request));
        }
        else {
            char exec_statement[BUFSIZE];
            strcpy(exec_statement, command_args[0]);
            for (int i = 1; i < index; i++) {
                strcat(exec_statement, " ");
                strcat(exec_statement, command_args[i]);
            }

            char stdout_buf[BUFSIZE];
            char *content_buffer = (char *)malloc(BUFSIZE);
            content_buffer[0] = '\0';
            ssize_t content_buffer_size = 0;
            ssize_t content_buffer_capacity = BUFSIZE;
            FILE *stdout_ptr;
            stdout_ptr = popen(exec_statement, "r");

            if (stdout_ptr == NULL) {
                perror("popen failed");
                return;
            }


            while (fgets(stdout_buf, sizeof(stdout_buf), stdout_ptr) != NULL) {
                if (content_buffer_size + strlen(stdout_buf) >= content_buffer_capacity) {
                    content_buffer = realloc(content_buffer,  content_buffer_capacity + BUFSIZE);
                    if (content_buffer == NULL) {
                        perror("realloc failed");
                        free(content_buffer);
                        fclose(stdout_ptr);
                        return;
                    }
                    content_buffer_capacity = content_buffer_capacity + BUFSIZE;
                }
                content_buffer_size += strlen(stdout_buf);
                strncat(content_buffer, stdout_buf, BUFSIZE - 1);
            }
            content_buffer[content_buffer_size] = '\0';

            char content_size[100];
            snprintf(content_size, sizeof(content_size), "Content-Length: %ld\r\n\r\n", content_buffer_size) ;
            strcat(header, content_size);

            write(nfd, header, strlen(header));
            write(nfd, content_buffer, strlen(content_buffer));

            free(content_buffer);
            fclose(stdout_ptr);
        }
    }

    else if (strcmp(arglist[0], "GET") == 0) {
        if (strcmp(arglist[1], "/") == 0) {
            arglist[1] = "/index.html";
        }

        char webpath[256] = WEBPATH;
        strcat(webpath, arglist[1]);
        FILE *page = fopen(webpath, "r");
        if (page == NULL) {
            write(nfd, not_found, strlen(not_found));
        }
        else {
            struct stat fileStat[BUFSIZE]; 
            stat(webpath, fileStat);
            char content_size[100];
            snprintf(content_size, sizeof(content_size), "Content-Length: %ld\r\n\r\n", fileStat->st_size) ;
            strcat(header, content_size);

            rewind(page);
            fseek(page, 0, SEEK_END);
            long page_size = ftell(page);
            rewind(page);

            char *message_buf = (char *)malloc(page_size + 1);
            if (message_buf == NULL) {
                perror("malloc failed");
                fclose(page);
                return;
            }

            size_t bytes_read = fread(message_buf, 1, page_size, page);
            if (bytes_read != page_size) {
                perror("Failed to read file");
                free(message_buf);
                fclose(page);
                return;
            }

            message_buf[page_size] = '\0';
            fclose(page);

            char *full_message = strdup(header);

            if (full_message == NULL) {
                perror("malloc failed");
                free(message_buf);
                return;
            }

            full_message = (char *)realloc(full_message, strlen(message_buf) + page_size);
            if (full_message == NULL) {
                perror("realloc failed");
                free(message_buf);
                free(full_message);
                return;
            }

            strcat(full_message, message_buf);

            write(nfd, full_message, strlen(full_message));

            free(message_buf);
            free(full_message);
        }

    }
    else if (strcmp(arglist[0], "HEAD") == 0) {
        char webpath[256] = WEBPATH;
        strcat(webpath, arglist[1]);
        FILE *page = fopen(webpath, "r");
        if (page == NULL) {
            write(nfd, not_found, strlen(not_found));
        }
        else {
            char header[256] = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n";
            struct stat fileStat[BUFSIZE]; 
            stat(webpath, fileStat);
            char content_size[100];
            snprintf(content_size, sizeof(content_size), "Content-Length: %ld\r\n\r\n", fileStat->st_size) ;
            strcat(header, content_size);
            write(nfd, header, strlen(header));
        }

    }
}

void handle_request(int nfd)
{
   char recv_buf[BUFSIZE];
   ssize_t bytes_read;

    memset(recv_buf, 0, BUFSIZE);

   while ((bytes_read = read(nfd, recv_buf, BUFSIZE - 1)) > 0)
   {
        request_processor(recv_buf, bytes_read, nfd);
   }

   close(nfd);
}

void run_service(int fd)
{
    char *interanal_error = "HTTP/1.0 500 INTERNAL ERROR\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";
   while (interrupt_flag == 0)
   {
      int nfd = accept_connection(fd);
      if (nfd != -1)
      {
            pid_t pid = fork();
            if (pid < 0) {
                perror("Fork failed");
                write(nfd, interanal_error, strlen(interanal_error));
                close(nfd);
                continue;
            }

            if (pid == 0) {
                printf("Connection established\n");
                handle_request(nfd);
                printf("Connection closed\n");
                close(nfd);
                exit(0);
            }
            else {
                close(nfd);
            }
      }
   }
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Invalid argument count");
        return 1;
    }

    int port = atoi(argv[1]);

   int fd = create_service(port);

   if (fd == -1)
   {
      perror(0);
      exit(1);
   }

    /*
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("Error setting up sigint handler");
        return 1;
    }
    */

    if (signal(SIGCHLD, sigchld_handler) == SIG_ERR) {
        perror("Error setting up sigchld handler");
        return 1;
    }

   printf("listening on port: %d\n", port);
   run_service(fd);
   close(fd);

   return 0;
}
