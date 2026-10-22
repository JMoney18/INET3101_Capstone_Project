#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "network.h"

static int send_all(int socket_fd, const char *data, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        ssize_t result = send(socket_fd, data + sent,
                              length - sent, 0);

        if (result < 0 && errno == EINTR) {
            continue;
        }

        if (result <= 0) {
            return 0;
        }

        sent += (size_t)result;
    }

    return 1;
}

/* Reads a newline-terminated message without overflowing the buffer. */
static int receive_line(int socket_fd, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return 0;
    }

    size_t used = 0;

    for (;;) {
        char ch;
        ssize_t result = recv(socket_fd, &ch, 1, 0);

        if (result < 0 && errno == EINTR) {
            continue;
        }

        if (result <= 0 || ch == '\0') {
            return 0;
        }

        /*
         * Check for the newline before checking remaining space.
         * This allows exactly size - 1 text characters.
         */
        if (ch == '\n') {
            buffer[used] = '\0';
            return 1;
        }

        if (used >= size - 1) {
            return 0;
        }

        buffer[used++] = ch;
    }
}

static void send_reply(int client, const char *reply)
{
    if (!send_all(client, reply, strlen(reply))) {
        fprintf(stderr, "Unable to send server response.\n");
    }
}

static int receive_number(int client, unsigned int maximum,
                           unsigned int *out_value)
{
    char text[16];

    if (!receive_line(client, text, sizeof text) ||
        text[0] == '\0') {
        return 0;
    }

    unsigned int value = 0;

    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }

        unsigned int digit = (unsigned int)(text[i] - '0');

        if (value > maximum / 10 ||
            (value == maximum / 10 && digit > maximum % 10)) {
            return 0;
        }

        value = value * 10 + digit;
    }

    *out_value = value;
    return 1;
}

static void receive_shared_event(int client, EventStore *store)
{
    Event event = {0};
    char start_text[32];
    char end_text[32];
    unsigned int repeat;
    unsigned int occurrences;

    if (!receive_line(client, event.title, sizeof event.title) ||
        !receive_line(client, event.location, sizeof event.location) ||
        !receive_line(client, start_text, sizeof start_text) ||
        !receive_line(client, end_text, sizeof end_text) ||
        !receive_number(client, 2, &repeat) ||
        !receive_number(client, MAX_OCCURRENCES, &occurrences) ||
        !event_parse_datetime(start_text, &event.start) ||
        !event_parse_datetime(end_text, &event.end)) {
        send_reply(client, "INVALID\n");
        return;
    }

    event.recurrence.type = (RepeatType)repeat;
    event.recurrence.occurrences = occurrences;

    unsigned int id = 0;
    StoreResult result = event_store_add(store, &event, &id);

    if (result == STORE_CONFLICT) {
        send_reply(client, "CONFLICT\n");
        return;
    }

    if (result == STORE_INVALID) {
        send_reply(client, "INVALID\n");
        return;
    }

    if (result != STORE_OK) {
        send_reply(client, "SERVER_ERROR\n");
        return;
    }

    if (event_store_save(store, "server_calendar.db") !=
            STORE_FILE_OK) {
        /*
         * Remove the unsaved event so subsequent requests do not
         * conflict with an event whose save failed.
         * The consumed ID remains retired in this running process.
         */
        event_store_delete(store, id);
        fprintf(stderr, "Shared event could not be saved.\n");
        send_reply(client, "SERVER_ERROR\n");
        return;
    }

    printf("Shared event saved with server ID %u: %s\n",
           id, event.title);
    send_reply(client, "OK\n");
}

int network_run_server(EventStore *store)
{
    if (store == NULL) {
        fprintf(stderr, "Server requires an event store.\n");
        return 1;
    }

    printf("Server calendar contains %zu event records.\n",
           event_store_count(store));

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("Unable to configure socket signal handling");
        return 1;
    }

    int listener = socket(AF_INET, SOCK_STREAM, 0);

    if (listener == -1) {
        perror("Unable to create server socket");
        return 1;
    }

    int reuse = 1;

    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof reuse) == -1) {
        perror("Unable to configure server socket");
        close(listener);
        return 1;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(CALENDAR_SERVER_PORT)
    };

    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listener, (struct sockaddr *)&address,
             sizeof address) == -1 ||
        listen(listener, 5) == -1) {
        perror("Unable to start server");
        close(listener);
        return 1;
    }

    printf("Listening on 127.0.0.1:%d\n", CALENDAR_SERVER_PORT);
    printf("Commands: PING checks the connection; STOP shuts down.\n");
    fflush(stdout);

    int exit_status = 0;
    int running = 1;

    while (running) {
        int client;

        do {
            client = accept(listener, NULL, NULL);
        } while (client == -1 && errno == EINTR);

        if (client == -1) {
            perror("Unable to accept client");
            exit_status = 1;
            break;
        }

        struct timeval timeout = {
            .tv_sec = 5,
            .tv_usec = 0
        };

        if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                       &timeout, sizeof timeout) == -1 ||
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                       &timeout, sizeof timeout) == -1) {
            perror("Unable to set client timeouts");
            close(client);
            continue;
        }

        char request[32];

        if (!receive_line(client, request, sizeof request)) {
            fprintf(stderr, "Incomplete or oversized request.\n");
        } else if (strcmp(request, "PING") == 0) {
            if (send_all(client, "PONG\n", 5)) {
                printf("Received PING; sent PONG.\n");
            } else {
                fprintf(stderr, "Unable to send response.\n");
            }
        } else if (strcmp(request, "ADD1") == 0) {
            receive_shared_event(client, store);
        } else if (strcmp(request, "STOP") == 0) {
            if (!send_all(client, "BYE\n", 4)) {
                fprintf(stderr, "Unable to send shutdown response.\n");
            }

            running = 0;
        } else {
            if (!send_all(client, "ERROR\n", 6)) {
                fprintf(stderr, "Unable to send error response.\n");
            }

            fprintf(stderr, "Unexpected request.\n");
        }

        close(client);
        fflush(stdout);
    }

    close(listener);
    printf("Server stopped.\n");

    return exit_status;
}

NetworkResult network_share_event(const Event *event)
{
    if (!event_is_valid(event) ||
        strchr(event->title, '\n') != NULL ||
        strchr(event->location, '\n') != NULL) {
        return NETWORK_INVALID;
    }

    char start_text[32];
    char end_text[32];

    struct tm *local = localtime(&event->start);

    if (local == NULL ||
        strftime(start_text, sizeof start_text,
                 "%Y-%m-%d %H:%M", local) != 16) {
        return NETWORK_INVALID;
    }

    local = localtime(&event->end);

    if (local == NULL ||
        strftime(end_text, sizeof end_text,
                 "%Y-%m-%d %H:%M", local) != 16) {
        return NETWORK_INVALID;
    }

    /*
     * ADD1 transmits local times to minute precision.
     * Reject a conversion that would change the original timestamps.
     */
    time_t checked_start;
    time_t checked_end;

    if (!event_parse_datetime(start_text, &checked_start) ||
        !event_parse_datetime(end_text, &checked_end) ||
        checked_start != event->start ||
        checked_end != event->end) {
        return NETWORK_INVALID;
    }

    char message[512];

    int length = snprintf(message, sizeof message,
                          "ADD1\n%s\n%s\n%s\n%s\n%u\n%u\n",
                          event->title,
                          event->location,
                          start_text,
                          end_text,
                          (unsigned int)event->recurrence.type,
                          event->recurrence.occurrences);

    if (length < 0 || (size_t)length >= sizeof message) {
        return NETWORK_INVALID;
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return NETWORK_IO_ERROR;
    }

    int connection = socket(AF_INET, SOCK_STREAM, 0);

    if (connection == -1) {
        return NETWORK_CONNECTION_ERROR;
    }

    struct timeval timeout = {
        .tv_sec = 5,
        .tv_usec = 0
    };

    if (setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof timeout) == -1 ||
        setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof timeout) == -1) {
        close(connection);
        return NETWORK_IO_ERROR;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(CALENDAR_SERVER_PORT)
    };

    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(connection, (struct sockaddr *)&address,
                sizeof address) == -1) {
        close(connection);
        return NETWORK_CONNECTION_ERROR;
    }

    if (!send_all(connection, message, (size_t)length)) {
        close(connection);
        return NETWORK_IO_ERROR;
    }

    char response[32];

    if (!receive_line(connection, response, sizeof response)) {
        close(connection);
        return NETWORK_IO_ERROR;
    }

    close(connection);

    if (strcmp(response, "OK") == 0) {
        return NETWORK_OK;
    }

    if (strcmp(response, "CONFLICT") == 0) {
        return NETWORK_CONFLICT;
    }

    if (strcmp(response, "INVALID") == 0) {
        return NETWORK_INVALID;
    }

    if (strcmp(response, "SERVER_ERROR") == 0) {
        return NETWORK_SERVER_ERROR;
    }

    return NETWORK_PROTOCOL_ERROR;
}
