#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

int main() {
    const char *SOCK_PATH = "/tmp/vswsh.sock";

    // Ignore SIGPIPE signals
    signal(SIGPIPE, SIG_IGN);

    // Set up Unix domain address
    struct sockaddr_un sock_addr = {
        .sun_family = AF_UNIX,
    };
    strncpy(sock_addr.sun_path, SOCK_PATH, sizeof(sock_addr.sun_path));

    // Initialize Unix domain socket
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("vswsh");
        return EXIT_FAILURE;
    }

    // Connect to server
    int ret = connect(sockfd, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    if (ret == -1) {
        perror("vswsh");
        return EXIT_FAILURE;
    }
    
    // Buffer for storing the user command - heap allocated by getline() function 
    char *cmd = NULL;
    char *out = NULL;
    size_t len;
    ssize_t chars_read;
    ssize_t bytes_read;
    ssize_t bytes_written;

    while (true) {

        // Read input from the user
        printf("vswitch> ");
        chars_read = getline(&cmd, &len, stdin);
        if (chars_read == -1) {
            perror("vswsh");
            return EXIT_FAILURE;
        }

        // Strip trailing newline and decrease string size accordingly
        cmd[strcspn(cmd, "\n")] = '\0';
        chars_read--;

        // Check for empty command
        if (chars_read <= 0) {
            free(cmd);
            cmd = NULL;
            continue;
        }

        // Check for exit command
        if (strcmp(cmd, "exit") == 0) {
            break;
        }

        // Write command length to socket
        bytes_written = write(sockfd, &chars_read, sizeof(chars_read));
        if (bytes_written == -1) {
            perror("vswsh");
            return EXIT_FAILURE;
        }

        // Write command to socket
        bytes_written = write(sockfd, cmd, chars_read);
        if (bytes_written == -1) {
            perror("vswsh");
            return EXIT_FAILURE;
        }

        // Read outoput length from the socket (doesn't include space for null terminator)
        bytes_read = read(sockfd, &len, sizeof(len));
        if (bytes_read == -1) {
            perror("vswsh");
            return EXIT_FAILURE;
        }

        // Heap-allocate buffer for output
        out = malloc((len + 1) * sizeof(char));
        if (out == NULL) {
            perror("vswsh");
            return EXIT_FAILURE;
        }

        // Read output from the socket
        bytes_read = read(sockfd, out, len * sizeof(char));
        if (bytes_read == -1) {
            perror("vswsh");
            return EXIT_FAILURE;
        }

        // Ensure null termination and print the output
        out[len] = '\0';
        // Space out only multiline strings
        if (strchr(out, '\n') != NULL) {
            printf("\n%s\n", out);
        } else {
            puts(out);
        }
        
        // Free command buffer
        free(cmd);
        cmd = NULL;

        // Free output buffer
        free(out);
        out = NULL;
    }

    // Close the socket
    close(sockfd);
    return EXIT_SUCCESS;
}