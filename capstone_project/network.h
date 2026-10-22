#ifndef NETWORK_H
#define NETWORK_H

#include "event_store.h"

#define CALENDAR_SERVER_PORT 5050

typedef enum {
    NETWORK_OK,
    NETWORK_INVALID,
    NETWORK_CONNECTION_ERROR,
    NETWORK_IO_ERROR,
    NETWORK_PROTOCOL_ERROR,
    NETWORK_CONFLICT,
    NETWORK_SERVER_ERROR
} NetworkResult;

/*
 * Sends one event to the local sharing server.
 * The server assigns its own event ID.
 * Success means the server has saved the event.
 */
NetworkResult network_share_event(const Event *event);

/*
 * Runs the local sharing server.
 * Handles clients one at a time to protect its shared store.
 * Returns 0 for normal shutdown or nonzero for startup/fatal errors.
 */
int network_run_server(EventStore *store);

#endif
