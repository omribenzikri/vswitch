#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

void print_help() { 
    const char *HELP_DIAG_HEADING = "Diagnostic commands:";
    const char *HELP_CONF_HEADING = "Configuration commands:";

    size_t diag_cmds_count = 3;
    const char *diag_cmds[] = {
        "show vports - show connected virtual interfaces",
        "show macs - show MAC address table",
        "show stats - show interface statistics"
    };

    size_t conf_cmds_count = 1;
    const char *conf_cmds[] = {
        "trunk [ADDR] - connect a virtual trunk link to a peer vswitch"
    };

    puts(HELP_DIAG_HEADING);
    for (int i = 0; i < diag_cmds_count; i++) {
        printf("  %s\n", diag_cmds[i]);
    }
    puts(HELP_CONF_HEADING);
    for (int i = 0; i < conf_cmds_count; i++) {
        printf("  %s\n", conf_cmds[i]);
    }
}


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

        // Check for help command
        if (strcmp(cmd, "help") == 0) {
            print_help();
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
        bytes_written = write(sockfd, cmd, chars_read * sizeof(char));
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

        // Heap-allocate buffer for output, +1 for null terminator
        out = malloc((len + 1) * sizeof(char));
        if (out == NULL) {
            perror("vswsh");
            return EXIT_FAILURE;
        }

        // Read output from the socket and ensure null termination
        bytes_read = read(sockfd, out, len * sizeof(char));
        if (bytes_read == -1) {
            perror("vswsh");
            return EXIT_FAILURE;
        }
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