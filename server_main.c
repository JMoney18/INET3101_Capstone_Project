#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>
#include "network.h"

int main(void)
{
    int lock_fd = open("server_calendar.lock",
                       O_CREAT | O_RDWR, 0600);

    if (lock_fd == -1) {
        perror("Unable to open server database lock");
        return EXIT_FAILURE;
    }

    if (flock(lock_fd, LOCK_EX | LOCK_NB) == -1) {
        perror("Unable to lock server database");
        close(lock_fd);
        return EXIT_FAILURE;
    }

    EventStore *store = NULL;

    StoreFileResult result =
        event_store_load("server_calendar.db", &store);

    if (result == STORE_FILE_NOT_FOUND) {
        store = event_store_create();

        if (store == NULL) {
            fprintf(stderr, "Unable to create server event store.\n");
            close(lock_fd);
            return EXIT_FAILURE;
        }
    } else if (result != STORE_FILE_OK) {
        fprintf(stderr,
                "Unable to load server_calendar.db (error code %d).\n"
                "The saved file has not been changed.\n",
                (int)result);
        close(lock_fd);
        return EXIT_FAILURE;
    }

    int exit_status = network_run_server(store) == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;

    /*
     * Save after a normal shutdown.
     * Event-sharing requests will also save before acknowledging success.
     */
    if (exit_status == EXIT_SUCCESS) {
        result = event_store_save(store, "server_calendar.db");

        if (result != STORE_FILE_OK) {
            fprintf(stderr, "Unable to save server calendar.\n");
            exit_status = EXIT_FAILURE;
        } else {
            printf("Server calendar saved.\n");
        }
    }

    event_store_destroy(store);

    if (close(lock_fd) == -1) {
        perror("Unable to close server database lock");
        exit_status = EXIT_FAILURE;
    }

    return exit_status;
}