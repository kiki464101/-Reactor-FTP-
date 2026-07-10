#ifndef SERVER_HANDLER_H
#define SERVER_HANDLER_H

#include "ipc_shm.h"

/** Child-process internal state. */
typedef enum {
    ST_IDLE,        /**< waiting for a command packet */
    ST_UPLOADING,   /**< receiving raw file data for PUT */
} handler_state_t;

/**
 * Child-process entry point.
 * Runs the full per-client lifecycle: LOGIN → command loop → BYE.
 * @param confd  connected socket fd
 * @param ip     client IP string
 * @param port   client port
 * @param shm    pointer to the shared client table
 */
void handle_client(int confd, const char *ip, int port, client_info_t *shm);

#endif
