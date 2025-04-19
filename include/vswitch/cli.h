#ifndef _VSW_CLI_H
#define _VSW_CLI_H

#include <stdbool.h>
#include "vswitch/state.h"

/*
Process the CLI command pointed to by 'cmd'. The output string will get pointed to by
'out'. The return value indicates whether the data pointed to by 'out' is allocated on the heap,
(in which case it should be freed later) or in static memory.
*/
bool vsw_cli_process_cmd(vsw_state_t *vswitch, char *cmd, char **out);

/*
Receives and processes a command from the connected client at 'client_sockfd'. If needed, sends
a response back to that client. The return value indicates whether the connection is still active or not.
*/
bool vsw_cli_handle_client(vsw_state_t *vswitch, int client_sockfd);

/*
Waits for incoming CLI connections over Unix domain sockets.
Receives commands and returns appropriate responses.
This function should run in its own thread.
*/
void *vsw_cli_serve(void *_vswitch);

#endif