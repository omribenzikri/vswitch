#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/un.h>
#include "proto.h"
#include "util/logging.h"
#include "vswitch/cli.h"

char *VSW_BAD_CMD_MSG  = "Unrecognized command.";
char *VSW_BAD_OBJ_MSG  = "Unrecognized object.";
char *VSW_BAD_ARGS_MSG  = "Incorrect number of arguments passed.";
char *VSW_BAD_ADDR_MSG  = "Invalid IP address supplied.";
char *VSW_MAX_PEERS_MSG  = "Failed to connect to peer vswitch - all local peer slots are in use";
char *VSW_CONN_OK_MSG  = "Successfully connected to neighbor vswitch.";
char *VSW_CONN_FULL_MSG  = "Failed to connect to neighbor vswitch - no available vports on the peer.";
char *VSW_CONN_TIMEOUT_MSG  = "Failed to connect to neighbor vswitch - connection timed out.";


bool vsw_cli_process_cmd(vswitch_t *vswitch, char *cmd, char **out) {
    // Tokenize command
    const char *ARG_DELIM = " \r\n\t\a";
    char *tok = strtok(cmd, ARG_DELIM);
    if (tok == NULL) {
        return false;
    }
    else if (strcmp(tok, "show") == 0) {
        tok = strtok(NULL, ARG_DELIM);
        if (tok == NULL || strtok(NULL, ARG_DELIM) != NULL) {
            *out = VSW_BAD_ARGS_MSG;
            return false;
        }
        else if (strcmp(tok, "macs") == 0) {
            pthread_mutex_lock(vswitch->lock);
            vsw_mac_table_to_str(vswitch->mac_addr_tbl, out);
            pthread_mutex_unlock(vswitch->lock);
            return true;
        }
        else if (strcmp(tok, "vports") == 0) {
            pthread_mutex_lock(vswitch->lock);
            vsw_vport_table_to_str(vswitch->vports, out);
            pthread_mutex_unlock(vswitch->lock);
            return true;
        }
        else {
            *out = VSW_BAD_OBJ_MSG;
            return false;
        }
    }
    else if (strcmp(tok, "trunk") == 0) {
        tok = strtok(NULL, ARG_DELIM);
        if (tok == NULL || strtok(NULL, ARG_DELIM) != NULL) {
            *out = VSW_BAD_ARGS_MSG;
            return false;
        }
        in_addr_t addr = inet_addr(tok);
        if (addr == -1) {
            *out = VSW_BAD_ADDR_MSG;
            return false;
        }
        struct sockaddr_in peer_addr = {
            .sin_family = AF_INET,
            .sin_addr = {
                .s_addr = addr
            },
            .sin_port = htons(VSW_UNDERLAY_PORT)
        };
        pthread_mutex_lock(vswitch->lock);
        neighbor_t *new_neighbor = vsw_add_neighbor(vswitch->neighbors, &peer_addr);
        if (new_neighbor == NULL) {
            *out = VSW_MAX_PEERS_MSG;
            return false;
        }
        pthread_mutex_unlock(vswitch->lock);
        vsw_conn_peer(vswitch, new_neighbor);
        switch (new_neighbor->status) {
            case TRUNK_ESTABLISHED:
                *out = VSW_CONN_OK_MSG;
                break;
            case TRUNK_PEER_FULL:
                *out = VSW_CONN_FULL_MSG;
                break;
            case TRUNK_TIMEOUT:
                *out = VSW_CONN_TIMEOUT_MSG;
                break;
            default:
                break;
        }
        return false;
    }
    else {
        *out = VSW_BAD_CMD_MSG;
        return false;
    }
}


bool vsw_cli_handle_client(vswitch_t *vswitch, int client_sockfd) {
    char *cmd = NULL;
    char *out = NULL;
    size_t len;
    ssize_t bytes_read;
    ssize_t bytes_written;

    // Specifies whether 'out' points to data on the heap (for variable length output)
    // or to data in static memory like the constant string defined above
    bool out_on_heap;

    // Read command length from the socket
    bytes_read = read(client_sockfd, &len, sizeof(len));
    if (bytes_read == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }
    else if (bytes_read == 0) {
        log_message("Local shell disconnected.", LOG_INFO);
        return false;
    }

    // Heap-allocate buffer for command (+1 for null terminator)
    cmd = malloc((len + 1) * sizeof(char));
    if (cmd == NULL) {
        log_sys_error();
        exit(EXIT_FAILURE);        
    }

    // Read command and ensure null termination
    bytes_read = read(client_sockfd, cmd, len * sizeof(char));
    if (bytes_read == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }
    cmd[len] = '\0';

    out_on_heap = vsw_cli_process_cmd(vswitch, cmd, &out);

    // Calculate the length of the output
    len = strlen(out);

    // Write output length to the socket
    bytes_written = write(client_sockfd, &len, sizeof(len));
    if (bytes_written == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }

    // Write output to the socket
    bytes_written = write(client_sockfd, out, len);
    if (bytes_written == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }

    // Free command buffer
    free(cmd);
    cmd = NULL;

    // Free output buffer if needed
    if (out_on_heap) {
        free(out);
    }

    // Session still active
    return true;
}


void *vsw_cli_serve(void *_vswitch) {
    vswitch_t *vswitch = (vswitch_t *)_vswitch;

    const char *SOCK_PATH = "/tmp/vswsh.sock";
    int server_sockfd;
    int client_sockfd;
    int ret;
    bool session_active = true;

    // Delete previously allocated socket
    unlink(SOCK_PATH);

    // Set up Unix domain address
    struct sockaddr_un sock_addr = {
        .sun_family = AF_UNIX,
    };
    strncpy(sock_addr.sun_path, SOCK_PATH, sizeof(sock_addr.sun_path));

    // Initialize Unix domain socket
    server_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        log_sys_error();
         exit(EXIT_FAILURE);
    }

    // Bind & listen
    ret = bind(server_sockfd, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    if (ret == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }
    ret = listen(server_sockfd, 1);
    if (ret == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }
    log_message("Bound local shell server socket.", LOG_INFO);

    while (true) {
        // Accept a new client connection
        client_sockfd = accept(server_sockfd, NULL, NULL);
        if (client_sockfd == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
        log_message("Local shell connected.", LOG_INFO);

        while (session_active) {
            session_active = vsw_cli_handle_client(vswitch, client_sockfd);
        }

        // Close client socket
        close(client_sockfd);
    }

    // Close server socket
    close(server_sockfd);
}