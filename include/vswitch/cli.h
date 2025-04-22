#ifndef _VSW_CLI_H
#define _VSW_CLI_H

#include <stdbool.h>
#include "vswitch/state.h"

/*
Waits for incoming CLI connections over Unix domain sockets.
Receives commands and returns appropriate responses.
This function should run in its own thread.
*/
void *vsw_cli_serve(void *_vswitch);

#endif