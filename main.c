#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "event_store.h"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include "network.h"

/*
 * Returns 1 for a complete line, 0 for end-of-input,
 * or -1 when the line is too long.
 */
static int read_line(char *buffer, size_t size)
{
    if (fgets(buffer, (int)size, stdin) == NULL) {
        return 0;
    }

    char *newline = strchr(buffer, '\n');

    if (newline != NULL) {
        *newline = '\0';
        return 1;
    }

    int ch = getchar();

    if (ch == '\n' || ch == EOF) {
        return 1;
    }

    while ((ch = getchar()) != '\n' && ch != EOF) {
        /* Discard the rest of an overlong line. */
    }

    return -1;
}

static int prompt_text(const char *prompt, char *buffer, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);

    int result = read_line(buffer, size);

    if (result == 0) {
        printf("\nInput ended. Event entry canceled.\n");
        return 0;
    }

    if (result == -1) {
        printf("Input is too long. Event entry canceled.\n");
        return 0;
    }

    return 1;
}

static int prompt_recurrence(Recurrence *recurrence)
{
    char input[32];

    if (!prompt_text("Repeat (0 = none, 1 = daily, 2 = weekly): ",
                     input, sizeof input)) {
        return 0;
    }

    if (strcmp(input, "0") == 0) {
        recurrence->type = REPEAT_NONE;
        recurrence->occurrences = 1;
        return 1;
    }

    if (strcmp(input, "1") == 0) {
        recurrence->type = REPEAT_DAILY;
    } else if (strcmp(input, "2") == 0) {
        recurrence->type = REPEAT_WEEKLY;
    } else {
        printf("Invalid repeat option. Event entry canceled.\n");
        return 0;
    }

    if (!prompt_text("Total occurrences (1-365, including first): ",
                     input, sizeof input)) {
        return 0;
    }

    unsigned int count = 0;

    if (input[0] == '\0') {
        printf("An occurrence count is required.\n");
        return 0;
    }

    for (size_t i = 0; input[i] != '\0'; i++) {
        if (input[i] < '0' || input[i] > '9') {
            printf("Occurrence count must contain only digits.\n");
            return 0;
        }

        unsigned int digit = (unsigned int)(input[i] - '0');

        if (count > MAX_OCCURRENCES / 10 ||
            (count == MAX_OCCURRENCES / 10 &&
             digit > MAX_OCCURRENCES % 10)) {
            printf("Occurrence count must be between 1 and 365.\n");
            return 0;
        }

        count = count * 10 + digit;
    }

    if (count == 0) {
        printf("Occurrence count must be between 1 and 365.\n");
        return 0;
    }

    recurrence->occurrences = count;
    return 1;
}

static void add_event(EventStore *store)
{
    Event event = {
        .recurrence = { REPEAT_NONE, 1 }
    };

    char date_text[32];

    if (!prompt_text("Title: ", event.title, sizeof event.title) ||
        !prompt_text("Location (optional): ",
                     event.location, sizeof event.location)) {
        return;
    }

    if (!prompt_text("Start (YYYY-MM-DD HH:MM): ",
                     date_text, sizeof date_text)) {
        return;
    }

    if (!event_parse_datetime(date_text, &event.start)) {
        printf("Invalid start date or time. Event not added.\n");
        return;
    }

    if (!prompt_text("End   (YYYY-MM-DD HH:MM): ",
                     date_text, sizeof date_text)) {
        return;
    }

    if (!event_parse_datetime(date_text, &event.end)) {
        printf("Invalid end date or time. Event not added.\n");
        return;
    }

    if (!prompt_recurrence(&event.recurrence)) {
        return;
    }

    unsigned int id = 0;
    StoreResult result = event_store_add(store, &event, &id);

    switch (result) {
        case STORE_OK:
            printf("Event added with ID %u.\n", id);
            break;

        case STORE_CONFLICT:
            printf("Scheduling conflict. Event not added.\n");
            break;

        case STORE_INVALID:
            printf("Invalid event. Check the title, time order, and "
                   "recurrence dates. A repeated local time may not "
                   "exist during a clock change.\n");
            break;

        case STORE_NO_MEMORY:
            printf("Not enough memory. Event not added.\n");
            break;

        case STORE_ID_EXHAUSTED:
            printf("No event IDs remain. Event not added.\n");
            break;

        case STORE_FULL:
            printf("Calendar is full. Delete an event before "
                   "adding another.\n");
            break;

        default:
            printf("Unable to add the event.\n");
            break;
    }
}

static int prompt_event_id(unsigned int *out_id)
{
    char input[32];

    if (!prompt_text("Event ID: ", input, sizeof input)) {
        return 0;
    }

    if (input[0] == '\0') {
        printf("An event ID is required.\n");
        return 0;
    }

    unsigned int id = 0;

    for (size_t i = 0; input[i] != '\0'; i++) {
        if (input[i] < '0' || input[i] > '9') {
            printf("Event ID must contain only digits.\n");
            return 0;
        }

        unsigned int digit = (unsigned int)(input[i] - '0');

        if (id > UINT_MAX / 10 ||
            (id == UINT_MAX / 10 && digit > UINT_MAX % 10)) {
            printf("Event ID is too large.\n");
            return 0;
        }

        id = id * 10 + digit;
    }

    if (id == 0) {
        printf("Event ID must be greater than zero.\n");
        return 0;
    }

    *out_id = id;
    return 1;
}

static void delete_event(EventStore *store)
{
    unsigned int id;

    if (!prompt_event_id(&id)) {
        return;
    }

    Event event;

    if (event_store_get(store, id, &event) != STORE_OK) {
        printf("No event found with ID %u.\n", id);
        return;
    }

    printf("Delete \"%s\" and all its occurrences?\n", event.title);

    char confirmation[16];

    if (!prompt_text("Enter yes to confirm: ",
                     confirmation, sizeof confirmation)) {
        return;
    }

    if (strcmp(confirmation, "yes") != 0) {
        printf("Deletion canceled.\n");
        return;
    }

    StoreResult result = event_store_delete(store, id);

    if (result == STORE_OK) {
        printf("Event %u and all its occurrences deleted.\n", id);
    } else {
        printf("Unable to delete event %u.\n", id);
    }
}

static void edit_event(EventStore *store)
{
    unsigned int id;

    if (!prompt_event_id(&id)) {
        return;
    }

    Event original;

    if (event_store_get(store, id, &original) != STORE_OK) {
        printf("No event found with ID %u.\n", id);
        return;
    }

    printf("Editing \"%s\" and its whole series.\n", original.title);
    printf("Enter all replacement details.\n");

    Event replacement = {
        .recurrence = { REPEAT_NONE, 1 }
    };

    char date_text[32];

    if (!prompt_text("New title: ",
                     replacement.title, sizeof replacement.title) ||
        !prompt_text("New location (optional): ",
                     replacement.location,
                     sizeof replacement.location)) {
        return;
    }

    if (!prompt_text("New start (YYYY-MM-DD HH:MM): ",
                     date_text, sizeof date_text)) {
        return;
    }

    if (!event_parse_datetime(date_text, &replacement.start)) {
        printf("Invalid start date or time. Original unchanged.\n");
        return;
    }

    if (!prompt_text("New end   (YYYY-MM-DD HH:MM): ",
                     date_text, sizeof date_text)) {
        return;
    }

    if (!event_parse_datetime(date_text, &replacement.end)) {
        printf("Invalid end date or time. Original unchanged.\n");
        return;
    }

    if (!prompt_recurrence(&replacement.recurrence)) {
        printf("Original event unchanged.\n");
        return;
    }

    StoreResult result = event_store_update(store, id, &replacement);

    switch (result) {
        case STORE_OK:
            printf("Event %u updated.\n", id);
            break;

        case STORE_CONFLICT:
            printf("Scheduling conflict. Original event unchanged.\n");
            break;

        case STORE_INVALID:
            printf("Invalid event details or recurrence dates. "
                   "Original event unchanged.\n");
            break;

        case STORE_NOT_FOUND:
            printf("No event found with ID %u.\n", id);
            break;

        default:
            printf("Unable to update. Original event unchanged.\n");
            break;
    }
}

static void share_event(const EventStore *store)
{
    unsigned int id;

    if (!prompt_event_id(&id)) {
        return;
    }

    Event event;

    if (event_store_get(store, id, &event) != STORE_OK) {
        printf("No event found with ID %u.\n", id);
        return;
    }

    NetworkResult result = network_share_event(&event);

    switch (result) {
        case NETWORK_OK:
            printf("Event shared and saved by the server.\n");
            break;

        case NETWORK_CONFLICT:
            printf("The event conflicts with the server calendar.\n");
            break;

        case NETWORK_CONNECTION_ERROR:
            printf("Unable to connect. Check that the server is running.\n");
            break;

        case NETWORK_INVALID:
            printf("The event cannot be shared in this message format "
                   "or was rejected as invalid.\n");
            break;

        case NETWORK_SERVER_ERROR:
            printf("The server could not store the event.\n");
            break;

        default:
            printf("Sharing was not confirmed because communication "
                   "failed. The server may have saved the event.\n");
            break;
    }
}

static void list_events(const EventStore *store)
{
    size_t count = event_store_count(store);

    if (count == 0) {
        printf("No events scheduled.\n");
        return;
    }

    for (size_t i = 0; i < count; i++) {
        Event event;

        if (event_store_get_at(store, i, &event) != STORE_OK) {
            fprintf(stderr, "Unable to retrieve an event.\n");
            return;
        }

        printf("\nID: %u | %s\n", event.id, event.title);
        printf("Location: %s\n",
               event.location[0] != '\0' ? event.location : "(none)");

        const char *repeat;

        switch (event.recurrence.type) {
            case REPEAT_DAILY:
                repeat = "Daily";
                break;

            case REPEAT_WEEKLY:
                repeat = "Weekly";
                break;

            default:
                repeat = "None";
                break;
        }

        printf("Repeat: %s | Occurrences: %u\n",
               repeat, event.recurrence.occurrences);

        for (unsigned int j = 0;
             j < event.recurrence.occurrences;
             j++) {
            time_t start;
            time_t end;
            char start_text[32];
            char end_text[32];

            if (!event_get_occurrence(&event, j, &start, &end)) {
                fprintf(stderr, "Unable to calculate occurrence.\n");
                return;
            }

            struct tm *local = localtime(&start);

            if (local == NULL ||
                strftime(start_text, sizeof start_text,
                         "%Y-%m-%d %H:%M", local) == 0) {
                fprintf(stderr, "Unable to format start time.\n");
                return;
            }

            local = localtime(&end);

            if (local == NULL ||
                strftime(end_text, sizeof end_text,
                         "%Y-%m-%d %H:%M", local) == 0) {
                fprintf(stderr, "Unable to format end time.\n");
                return;
            }

            printf("  %u. %s -> %s\n",
                   j + 1, start_text, end_text);
        }
    }
}

int main(void)
{
    int lock_fd = open("calendar.lock", O_CREAT | O_RDWR, 0600);

    if (lock_fd == -1) {
        perror("Unable to open calendar lock");
        return EXIT_FAILURE;
    }

    if (flock(lock_fd, LOCK_EX | LOCK_NB) == -1) {
        perror("Unable to lock calendar; another copy may be running");
        close(lock_fd);
        return EXIT_FAILURE;
    }

    EventStore *store = NULL;

    StoreFileResult load_result =
        event_store_load("calendar.db", &store);

    if (load_result == STORE_FILE_NOT_FOUND) {
        store = event_store_create();

        if (store == NULL) {
            fprintf(stderr, "Failed to create the event store.\n");
            close(lock_fd);
            return EXIT_FAILURE;
        }

        printf("Starting a new calendar.\n");
    } else if (load_result != STORE_FILE_OK) {
        fprintf(stderr,
                "Unable to load calendar.db (error code %d).\n"
                "The saved file has not been changed.\n",
                (int)load_result);
        close(lock_fd);
        return EXIT_FAILURE;
    } else {
        printf("Loaded %zu event records.\n",
               event_store_count(store));
    }

    char choice[32];
    int running = 1;

    printf("Calendar Management System\n");

    while (running) {
        printf("\n1. List events\n");
        printf("2. Add event\n");
        printf("3. Delete event\n");
        printf("4. Edit event\n");
        printf("5. Share event with server\n");
        printf("0. Exit\n");
        printf("Choose an option: ");
        fflush(stdout);

        int result = read_line(choice, sizeof choice);

        if (result == 0) {
            printf("\nInput ended. Closing calendar.\n");
            break;
        }

        if (result == -1) {
            printf("Input is too long. Enter 0 through 5.\n");
            continue;
        }

        if (strcmp(choice, "1") == 0) {
            list_events(store);
        } else if (strcmp(choice, "2") == 0) {
            add_event(store);
        } else if (strcmp(choice, "3") == 0) {
            delete_event(store);
        } else if (strcmp(choice, "4") == 0) {
            edit_event(store);
        } else if (strcmp(choice, "5") == 0) {
            share_event(store);
        } else if (strcmp(choice, "0") == 0) {
            running = 0;
        } else {
            printf("Invalid option. Enter 0 through 5.\n");
        }
    }

    int exit_status = EXIT_SUCCESS;

    StoreFileResult save_result =
        event_store_save(store, "calendar.db");

    if (save_result != STORE_FILE_OK) {
        fprintf(stderr,
                "Calendar save failed (error code %d).\n"
                "This session's changes were not saved.\n",
                (int)save_result);
        exit_status = EXIT_FAILURE;
    } else {
        printf("Calendar saved to calendar.db.\n");
    }

    event_store_destroy(store);

    if (close(lock_fd) == -1) {
        perror("Unable to close calendar lock");
        exit_status = EXIT_FAILURE;
    }

    if (exit_status == EXIT_SUCCESS) {
        printf("Goodbye.\n");
    }

    return exit_status;
}