#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/un.h>
#include "proto.h"
#include "util/logging.h"
#include "vswitch/core.h"
#include "vswitch/cli.h"

char *ARG_DELIM = " \r\n\t\a";

char *VSW_BAD_CMD_MSG  = "Unrecognized command.";
char *VSW_BAD_OBJ_MSG  = "Unrecognized object.";
char *VSW_BAD_ARGS_MSG  = "Incorrect number of arguments passed.";
char *VSW_BAD_ADDR_MSG  = "Invalid IP address supplied.";
char *VSW_MAX_PEERS_MSG  = "Failed to connect to peer vswitch - all local peer slots are in use.";
char *VSW_CONN_OK_MSG  = "Successfully connected to neighbor vswitch.";
char *VSW_CONN_FULL_MSG  = "Failed to connect to neighbor vswitch - no available vports on the peer.";
char *VSW_CONN_TIMEOUT_MSG  = "Failed to connect to neighbor vswitch - connection timed out.";
char *VSW_CONN_EXISTS_MSG = "Trunk link for this neighbor already exists.";
char *VSW_RM_PEER_OK_MSG = "Sucessfully removed the neighbor vswitch.";
char *VSW_RM_PEER_NOT_FOUND_MSG = "Failed to remove the neighbor vswitch - neighbor not found.";

/*
Process trunk connection command. The output of the command will get pointed to by
'out'.
*/
void vsw_cli_process_create_trunk_cmd(vsw_state_t *vswitch, char **out) {
    // Parse command for peer address
    char *tok = strtok(NULL, ARG_DELIM);
    if (tok == NULL || strtok(NULL, ARG_DELIM) != NULL) {
        *out = VSW_BAD_ARGS_MSG;
        return;
    }
    in_addr_t addr = inet_addr(tok);
    if (addr == ( in_addr_t)(-1)) {
        *out = VSW_BAD_ADDR_MSG;
        return;
    }
    struct sockaddr_in peer_addr = {
        .sin_family = AF_INET,
        .sin_addr = {
            .s_addr = addr
        },
        .sin_port = htons(VSW_UNDERLAY_PORT)
    };
    // ENTER CRITICAL SECTION
    pthread_mutex_lock(&vswitch->lock);
    if (vsw_search_neighbor(vswitch->neighbors, VSW_NEIGHBOR_ARR_SIZE, &peer_addr) != NULL) {
        *out = VSW_CONN_EXISTS_MSG;
        pthread_mutex_unlock(&vswitch->lock);
        return;
    }
    vsw_neighbor_t *new_neighbor = vsw_add_neighbor(vswitch->neighbors, VSW_NEIGHBOR_ARR_SIZE, &peer_addr);
    if (new_neighbor == NULL) {
        *out = VSW_MAX_PEERS_MSG;
        pthread_mutex_unlock(&vswitch->lock);
        return;
    }
    // EXIT CRITICAL SECTION
    pthread_mutex_unlock(&vswitch->lock);

    // Attempt to connect to peer
    vsw_connect_neighbor(vswitch, new_neighbor);

    // ENTER CRITICAL SECTION
    pthread_mutex_lock(&vswitch->lock);
    switch (new_neighbor->status)
    {
        case VSW_TRUNK_ESTABLISHED:
            *out = VSW_CONN_OK_MSG;
            break;
        case VSW_TRUNK_PEER_FULL:
            *out = VSW_CONN_FULL_MSG;
            break;
        case VSW_TRUNK_TIMEOUT:
            *out = VSW_CONN_TIMEOUT_MSG;
            break;
        default:
            break;
    }
    // EXIT CRITICAL SECTION
    pthread_mutex_unlock(&vswitch->lock);
}

/*
Process trunk removal command. The output of the command will get pointed to by
'out'.
*/
void vsw_cli_process_rm_trunk_cmd(vsw_state_t *vswitch, char **out) {
    // Parse command for peer address
    char *tok = strtok(NULL, ARG_DELIM);
    if (tok == NULL || strtok(NULL, ARG_DELIM) != NULL) {
        *out = VSW_BAD_ARGS_MSG;
        return;
    }
    in_addr_t addr = inet_addr(tok);
    if (addr == ( in_addr_t)(-1)) {
        *out = VSW_BAD_ADDR_MSG;
        return;
    }
    struct sockaddr_in peer_addr = {
        .sin_family = AF_INET,
        .sin_addr = {
            .s_addr = addr
        },
        .sin_port = htons(VSW_UNDERLAY_PORT)
    };

    // ENTER CRITICAL SECTION
    pthread_mutex_lock(&vswitch->lock);
    vsw_neighbor_t *popped_neighbor = vsw_remove_neighbor(vswitch->neighbors, VSW_NEIGHBOR_ARR_SIZE, &peer_addr);
    if (popped_neighbor != NULL) {
        *out = VSW_RM_PEER_OK_MSG;
    } else {
        *out = VSW_RM_PEER_NOT_FOUND_MSG;
    }
    // EXIT CRITICAL SECTION
    pthread_mutex_unlock(&vswitch->lock);
}

/*
Process a diagnostics command (e.g 'show' command). The output of the command will get pointed to by
'out' and the return value indicates whether the output is allocated on the heap or not.
*/
bool vsw_cli_process_diag_cmd(vsw_state_t *vswitch, char **out) {
    // Parse command for desired object
    char *tok = strtok(NULL, ARG_DELIM);
    if (tok == NULL || strtok(NULL, ARG_DELIM) != NULL) {
        *out = VSW_BAD_ARGS_MSG;
        return false;
    }
    else if (strcmp(tok, "macs") == 0) {
        vsw_mac_table_to_str(vswitch->mac_addr_tbl, VSW_MAC_ADDR_TBL_SIZE, vswitch->vports, VSW_VPORT_ARR_SIZE, out);
        return true;
    }
    else if (strcmp(tok, "vports") == 0) {
        vsw_vport_table_to_str(vswitch->vports, VSW_VPORT_ARR_SIZE, out);
        return true;
    }
    else if (strcmp(tok, "neighbors") == 0) {
        vsw_neighbor_table_to_str(vswitch->neighbors, VSW_NEIGHBOR_ARR_SIZE, vswitch->vports, VSW_VPORT_ARR_SIZE, out);
        return true;
    }
    else if (strcmp(tok, "stats") == 0) {
        vsw_vport_stats_table_to_str(vswitch->vports, VSW_VPORT_ARR_SIZE, out);
        return true;      
    }
    else {
        *out = VSW_BAD_OBJ_MSG;
        return false;
    }
}

/*
Process a configuration addition command (e.g 'add' command).
The output of the command will get pointed to by 'out'.
*/
void vsw_cli_process_create_cmd(vsw_state_t *vswitch, char **out) {
    char *tok = strtok(NULL, ARG_DELIM);
    if (tok == NULL) {
        *out = VSW_BAD_ARGS_MSG;
    }
    else if (strcmp(tok, "trunk") == 0) {
        vsw_cli_process_create_trunk_cmd(vswitch, out);
    }
    else {
        *out = VSW_BAD_OBJ_MSG;
    }
}

/*
Process a configuration removal command (e.g 'remove' command).
The output of the command will get pointed to by 'out'.
*/
void vsw_cli_process_rm_cmd(vsw_state_t *vswitch, char **out) {
    char *tok = strtok(NULL, ARG_DELIM);
    if (tok == NULL) {
        *out = VSW_BAD_ARGS_MSG;
    }
    else if (strcmp(tok, "trunk") == 0) {
        vsw_cli_process_rm_trunk_cmd(vswitch, out);
    }
    else {
        *out = VSW_BAD_OBJ_MSG;
    }
}

/*
Process the CLI command pointed to by 'cmd'. The output string will get pointed to by
'out'. The return value indicates whether the data pointed to by 'out' is allocated on the heap,
(in which case it should be freed later) or in static memory.
*/
bool vsw_cli_process_cmd(vsw_state_t *vswitch, char *cmd, char **out) {
    bool out_on_heap = false;
    
    char *tok = strtok(cmd, ARG_DELIM);
    if (tok == NULL) {
        *out = VSW_BAD_CMD_MSG;
    }
    else if (strcmp(tok, "show") == 0) {
        // ENTER CRITICAL SECTION
        pthread_mutex_lock(&vswitch->lock);
        out_on_heap = vsw_cli_process_diag_cmd(vswitch, out);
        // EXIT CRITICAL SECTION
        pthread_mutex_unlock(&vswitch->lock);
    }
    else if (strcmp(tok, "create") == 0) {
        vsw_cli_process_create_cmd(vswitch, out);
    }
    else if (strcmp(tok, "remove") == 0) {
        vsw_cli_process_rm_cmd(vswitch, out);
    }
    else {
        *out = VSW_BAD_CMD_MSG;
    }
    return out_on_heap;
}

/*
Receives and processes a command from the connected client at 'client_sockfd'. If needed, sends
a response back to that client. The return value indicates whether the connection is still active or not.
*/
bool vsw_cli_handle_client(vsw_state_t *vswitch, int client_sockfd) {
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

/*
Documented in header file
*/
void *vsw_cli_serve(void *_vswitch) {
    vsw_state_t *vswitch = (vsw_state_t *)_vswitch;

    const char *SOCK_PATH = "/tmp/vswsh.sock";
    int server_sockfd;
    int client_sockfd;
    int ret;
    bool session_active = false;

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
        session_active = true;

        while (session_active) {
            session_active = vsw_cli_handle_client(vswitch, client_sockfd);
        }

        // Close client socket
        close(client_sockfd);
    }

    // Close server socket
    close(server_sockfd);
}