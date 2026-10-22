#include <stdio.h>
#include <stdlib.h>
#include "event_store.h"

int main(void)
{
    EventStore *store = event_store_create();

    if (store == NULL) {
        return EXIT_FAILURE;
    }

    Event event = {
        .title = "Capacity check",
        .recurrence = { REPEAT_NONE, 1 }
    };

    unsigned int id = 0;

    for (unsigned int i = 0; i < MAX_EVENTS; i++) {
        event.start = (time_t)(1000 + i * 100);
        event.end = event.start + 50;

        if (event_store_add(store, &event, &id) != STORE_OK) {
            fprintf(stderr, "Unable to fill store to capacity.\n");
            event_store_destroy(store);
            return EXIT_FAILURE;
        }
    }

    event.start = 10000;
    event.end = 10050;

    unsigned int rejected_id = 999;

    if (event_store_add(store, &event, &rejected_id) != STORE_FULL ||
        event_store_count(store) != MAX_EVENTS ||
        rejected_id != 999) {
        fprintf(stderr, "Capacity rejection check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    if (event_store_save(store, "test_capacity.db") != STORE_FILE_OK) {
        fprintf(stderr, "Full-store save failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    EventStore *loaded = NULL;

    if (event_store_load("test_capacity.db", &loaded) != STORE_FILE_OK) {
        fprintf(stderr, "Full-store load failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    int passed =
        event_store_count(loaded) == MAX_EVENTS &&
        event_store_delete(loaded, 1) == STORE_OK &&
        event_store_add(loaded, &event, &id) == STORE_OK &&
        id == MAX_EVENTS + 1 &&
        event_store_count(loaded) == MAX_EVENTS;

    event_store_destroy(loaded);
    event_store_destroy(store);

    if (!passed) {
        fprintf(stderr, "Capacity recovery check failed.\n");
        return EXIT_FAILURE;
    }

    printf("Capacity, full-store persistence, and recovery checks passed.\n");
    return EXIT_SUCCESS;
}