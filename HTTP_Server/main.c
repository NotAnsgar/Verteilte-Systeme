/* Beispiel TCP Echo-Server fuer mehrere Clients
 * Gekürzt aus Stevens: Unix Network Programming
 * getestet unter Ubuntu 16.04 64 Bit
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h> 

#define SRV_PORT 8080
#define MAX_SOCK 10
#define MAXLINE 512

// Vorwaertsdeklarationen
void handle_request(int, char*);
void create_http_header(char*, char*, int, char*, char*);
int create_dir_page(char*, char*, char*);
void return_binary_file(FILE*, int);
void err_abort(char *str);

// Explizite Deklaration zur Vermeidung von Warnungen
void exit(int code);
void *memset(void *s, int c, size_t n);

char *docroot = NULL;

int main(int argc, char *argv[]) {
    //Fetch input parameters and check them
    int port = 0;
    if (argc == 3) {
        docroot = argv[1];
        port = atoi(argv[2]);
    } else {
        fprintf(stderr, "Usage: %s [docroot] [port]\n", argv[0]);
        return 1;
    }

    // Deskriptoren, Adresslaenge, Prozess-ID
    int sockfd, newsockfd, alen, pid;
    int reuse = 1;
    // Socket Adressen
    struct sockaddr_in cli_addr, srv_addr;

    // TCP-Socket erzeugen
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        err_abort("Kann Stream-Socket nicht oeffnen!");
    }
    // Nur zum Test: Socketoption zum sofortigen Freigeben der Sockets
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse)) < 0) {
        err_abort("Kann Socketoption nicht setzen!");
    }
    // Binden der lokalen Adresse damit Clients uns erreichen
    memset((void *) &srv_addr, '\0', sizeof (srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *) &srv_addr,
            sizeof (srv_addr)) < 0) {
        err_abort("Kann  lokale  Adresse  nicht  binden,  laeuft  fremder Server?");
    }
    // Warteschlange fuer TCP-Socket einrichten
    listen(sockfd, 5);
    printf("TCP Web-Server: bereit ...\n");

    // Endless loop
    for (;;) {
        alen = sizeof (cli_addr);
        // Verbindung aufbauen
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &alen);
        if (newsockfd < 0) {
            err_abort("Fehler beim Verbindungsaufbau!");
        }

        //Multi Level Server with child processes
        
        if ((pid = fork()) < 0) {
            err_abort("Fehler beim Erzeugen eines Kindprozesses!");
        } else if (pid == 0) {
            close(sockfd);
            handle_request(newsockfd, docroot);
            exit(0);
        }
        close(newsockfd);
         
        
        //Iterative Server
        /*
        handle_request(newsockfd, docroot);
        close (newsockfd);
        */
    }
}

/**
 * Handles the given requests -> checks for requested file or dir and sends reponse
 * 
 * @param sockfd    -> Socket to read from
 * @param docroot   -> Working Dir
 */
void handle_request(int sockfd, 
                    char* docroot) {
    int n;
    char in[MAXLINE];
    memset((void *) in, '\0', MAXLINE);

    for (;;) {
        // Daten vom Socket lesen
        n = read(sockfd, in, MAXLINE);
        if (n == 0) {
            return;
        } else if (n < 0) {
            err_abort("Fehler beim Lesen des Sockets!");
        }
        printf("%s\n", in);

        // Take information from GET request
        char url[50] = "\0";
        char http_v[5] = "\0";
        sscanf(in, "GET %255s HTTP/%s", url, http_v);
        //Check url and http Version
        if(0 >= (int)strlen(url) || 0 >= (int)strlen(http_v))
        {
            err_abort("Get Request is not correctly formatted! Use GET [path] HTTP/[http_version]");
        }
        
        char filepath[strlen(docroot) + strlen(url) + 2];
        snprintf(filepath, sizeof (filepath), "%s/%s", docroot, url);

        //Create var for headerstr on heap
        char* http_header = malloc(1000 * sizeof (*http_header));

        int status = 200;
        // Check whats being requested and act accordingly
        //TODO: Merge binary file types
        if (NULL != strstr(filepath, ".jpg") ||
            NULL != strstr(filepath, ".jpeg") ||
            NULL != strstr(filepath, ".ico") ||
            NULL != strstr(filepath, ".html") ||
            NULL != strstr(filepath, ".txt"))
        {
            //Try to open file
            FILE *file_r;
            if (NULL == (file_r = fopen(filepath, "rb"))) {
                status = 404;
            }
            
            //Get content type
            char content_type[25] = "\0";
            if(NULL != strstr(filepath, ".jpg") ||
                NULL != strstr(filepath, ".jpeg"))
            {
                sprintf(content_type, "image/jpeg");
            }
            else if(NULL != strstr(filepath, ".ico"))
            {
                sprintf(content_type, "image/x-icon");
            }
            else if(NULL != strstr(filepath, ".html"))
            {
                sprintf(content_type, "text/html");
            }
            else if(NULL != strstr(filepath, ".txt"))
            {
                sprintf(content_type, "text/plain");
            }
            
            //Get last modified
            struct stat sb;
            lstat(filepath, &sb);
            
            //Create header and write to socket
            create_http_header(http_header, http_v, status, content_type, ctime(&sb.st_mtime));
            if (write(sockfd, http_header, strlen(http_header)) != strlen(http_header)) {
                err_abort("Fehler beim Schreiben des Sockets!");
            }
            //If file exists, write binary to socket
            if (200 == status) {
                return_binary_file(file_r, sockfd);
            }
        } else {
            //Client requested directory
            char* response_body = malloc(1000 * sizeof (*response_body));
            status = create_dir_page(filepath, response_body, url);
            create_http_header(http_header, http_v, status, "text/html", "");
            if (write(sockfd, http_header, strlen(http_header)) == strlen(http_header))
            {
                if (200 == status)
                {    
                    if(write(sockfd, response_body, strlen(response_body)) != strlen(response_body))
                    {
                        err_abort("Fehler beim Schreiben des Sockets!");
                    }
                }
            }
            else
            {
                err_abort("Fehler beim Schreiben des Sockets!");
            }
            free(response_body);
        }

        free(http_header);
        return;
    //    exit(0);
    }
}

/**
 * Creates the HTTP Header from the given Information
 * TODO: Maybe don't close the header here so Information can be added in request handler
 * 
 * @param header_str    -> Pointer to string to write to
 * @param http_v        -> HTTP Version from request
 * @param status_code   -> HTTP Status Code
 * @param content_type  -> Requested content type
 */
void create_http_header(char* header_str, 
                        char* http_v, 
                        int status_code, 
                        char* content_type,
                        char* last_modified) 
{
    //Get current time
    time_t current_time;
    current_time = time(NULL);
    char* c_time_string;
    c_time_string = ctime(&current_time);

    //Create Status Message from Status Code
    char status_message[15] = "\0";
    if (200 == status_code) {
        sprintf(status_message, "OK");
    } else if (404 == status_code) {
        sprintf(status_message, "NOT FOUND");
    }

    // Finally create header
    //TODO: Still a bit ugly with two parts -> beautify
    
    //Header One with HTTP Version, Status Code, Status Message and Date
    char header_one[300] = "\0";
    snprintf(header_one, sizeof (header_one), "HTTP/%s %d %s\nDate: %sContent-Language: de\nContent-Type: %s\n", http_v, status_code, status_message, c_time_string, content_type);
    strcat(header_str, header_one);
    
    //Header two with last modified if it is a file
    if(0 < (int)strlen(last_modified))
    {
        char header_two[100] = "\0";
        snprintf(header_two, sizeof (header_two), "Last-Modified: %s", last_modified);
        strcat(header_str, header_two);
    }
    strcat(header_str, "\n");
    
    if(404 == status_code) {
        strcat(header_str, "<html>\n<body>\n<h1>ERROR 404: File Not Found</h1>\n</body>\n</html>\n");
    }
}

/**
 * Reads from filestream and writes it directly to socket, 100 units at a time
 * 
 * @param file_r    -> The filestream to read from
 * @param sockfd    -> The socket to write to
 */
void return_binary_file(FILE* file_r, 
                        int sockfd) 
{
    int num_r, num_w;
    char buffer[100];

    // Read from file to buffer and write to socket
    while (feof(file_r) == 0) {
        if ((num_r = fread(buffer, 1, 100, file_r)) != 100) {
            if (ferror(file_r) != 0) {
                err_abort("Fehler beim Lesen einer Datei.");
            } else if (feof(file_r) != 0);
        }
        if ((num_w = write(sockfd, buffer, num_r)) != num_r) {
            
            err_abort("Fehler beim Schreiben einer Datei.");
        }
    }
    fclose(file_r);
    char trailing_whitespace[] = "\n";
    if (write(sockfd, trailing_whitespace, strlen(trailing_whitespace)) != strlen(trailing_whitespace)) {
        err_abort("Fehler beim Schreiben des Sockets!");
    }
}

/**
 * If the user requests a directory, this function creates the html file with 
 * information on the available resources with links
 * 
 * @param filepath      -> string with the wanted path
 * @param dir_page      -> pointer to string on heap to hold html
 * @param url           -> Used to create links
 * @return status_code  -> HTTP Status Code
 */
int create_dir_page(char* filepath, 
                    char* dir_page,
                    char* url) 
{
    //Create Status Code and try to open dir
    int status_code = 200;
    DIR *req_dir = opendir(filepath);
    if (req_dir == NULL) {
        printf("Cannot open directory '%s'\n", filepath);
        status_code = 404;
        return status_code;
    }
    
    //Iterate over files in dir and iteratively create html string
    struct dirent *dp;
    strcat(dir_page, "<html>\n<body>\n<h1>Vorhandene Dateien</h1>\n<ul>\n");
    
    //TODO: Find a better solution (this works but is ugly)
    int has_slash_end = 0;
    if(url && *url && url[strlen(url) - 1] == '/')
    {
        has_slash_end = 1;
    }
    while (NULL != (dp = readdir(req_dir))) {
        if (0 != strcmp(dp->d_name, ".") && 0 != strcmp(dp->d_name, "..")) {
            char current[200] = "\0";
            
            if(1 == has_slash_end)
            {
                snprintf(current, sizeof (current), "<li><a href=\"%s%s\">%s</a></li>\n", url, dp->d_name, dp->d_name);
            }
            else
            {
                snprintf(current, sizeof (current), "<li><a href=\"%s/%s\">%s</a></li>\n", url, dp->d_name, dp->d_name);
            }
            
            strcat(dir_page, current);
        }
    }
    strcat(dir_page, "</ul>\n</body>\n</html>\n");
    
    //Don't forget to close directory
    closedir(req_dir);
    return status_code;
}

/* 
 * Ausgabe von Fehlermeldungen
 */
void err_abort(char *str) 
{
    fprintf(stderr, " TCP Echo-Server: %s\n", str);
    fflush(stdout);
    fflush(stderr);
    exit(1);
}
