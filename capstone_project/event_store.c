#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include "event_store.h"
#include <limits.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

typedef struct EventNode {
    Event event;
    struct EventNode *next;
} EventNode;

struct EventStore {
    EventNode *head;
    size_t count;
    unsigned int next_id;
};

EventStore *event_store_create(void)
{
    EventStore *store = malloc(sizeof *store);

    if (store == NULL) {
        return NULL;
    }

    store->head = NULL;
    store->count = 0;
    store->next_id = 1;

    return store;
}

void event_store_destroy(EventStore *store)
{
    if (store == NULL) {
        return;
    }

    EventNode *current = store->head;

    while (current != NULL) {
        EventNode *next = current->next;
        free(current);
        current = next;
    }

    free(store);
}

size_t event_store_count(const EventStore *store)
{
    if (store == NULL) {
        return 0;
    }

    return store->count;
}

StoreResult event_store_get(const EventStore *store,
                            unsigned int id,
                            Event *out_event)
{
    if (store == NULL || out_event == NULL || id == 0) {
        return STORE_INVALID;
    }

    const EventNode *current = store->head;

    while (current != NULL) {
        if (current->event.id == id) {
            *out_event = current->event;
            return STORE_OK;
        }

        current = current->next;
    }

    return STORE_NOT_FOUND;
}

StoreResult event_store_get_at(const EventStore *store,
                               size_t index,
                               Event *out_event)
{
    if (store == NULL || out_event == NULL) {
        return STORE_INVALID;
    }

    if (index >= store->count) {
        return STORE_NOT_FOUND;
    }

    const EventNode *current = store->head;

    for (size_t position = 0; position < index; position++) {
        current = current->next;
    }

    *out_event = current->event;
    return STORE_OK;
}

static StoreResult check_series(const EventStore *store,
                                 const Event *event,
                                 unsigned int excluded_id)
{
    time_t starts[MAX_OCCURRENCES];
    time_t ends[MAX_OCCURRENCES];
    unsigned int count = event->recurrence.occurrences;

    /* Calculate and validate the complete proposed series. */
    for (unsigned int i = 0; i < count; i++) {
        if (!event_get_occurrence(event, i, &starts[i], &ends[i])) {
            return STORE_INVALID;
        }

        /* Reject overlapping occurrences within this series. */
        for (unsigned int j = 0; j < i; j++) {
            if (starts[i] < ends[j] && starts[j] < ends[i]) {
                return STORE_CONFLICT;
            }
        }
    }

    /* Compare against every occurrence of every other event. */
    const EventNode *current = store->head;

    while (current != NULL) {
        if (current->event.id != excluded_id) {
            for (unsigned int j = 0;
                 j < current->event.recurrence.occurrences;
                 j++) {
                time_t existing_start;
                time_t existing_end;

                if (!event_get_occurrence(&current->event, j,
                                           &existing_start,
                                           &existing_end)) {
                    return STORE_INVALID;
                }

                for (unsigned int i = 0; i < count; i++) {
                    if (starts[i] < existing_end &&
                        existing_start < ends[i]) {
                        return STORE_CONFLICT;
                    }
                }
            }
        }

        current = current->next;
    }

    return STORE_OK;
}

StoreResult event_store_add(EventStore *store,
                            const Event *event,
                            unsigned int *assigned_id)
{
    if (store == NULL || assigned_id == NULL ||
        !event_is_valid(event)) {
        return STORE_INVALID;
    }

    if (store->count >= MAX_EVENTS) {
        return STORE_FULL;
    }

    StoreResult result = check_series(store, event, 0);

    if (result != STORE_OK) {
        return result;
    }

    if (store->next_id == 0) {
        return STORE_ID_EXHAUSTED;
    }

    EventNode *current = store->head;
    EventNode *tail = NULL;

    while (current != NULL) {
        tail = current;
        current = current->next;
    }

    EventNode *node = malloc(sizeof *node);

    if (node == NULL) {
        return STORE_NO_MEMORY;
    }

    node->event = *event;
    node->event.id = store->next_id;
    node->next = NULL;

    if (tail == NULL) {
        store->head = node;
    } else {
        tail->next = node;
    }

    store->count++;
    *assigned_id = node->event.id;

    if (store->next_id == UINT_MAX) {
        store->next_id = 0;
    } else {
        store->next_id++;
    }

    return STORE_OK;
}

StoreResult event_store_delete(EventStore *store, unsigned int id)
{
    if (store == NULL || id == 0) {
        return STORE_INVALID;
    }

    EventNode *current = store->head;
    EventNode *previous = NULL;

    while (current != NULL) {
        if (current->event.id == id) {
            if (previous == NULL) {
                store->head = current->next;
            } else {
                previous->next = current->next;
            }

            free(current);
            store->count--;

            return STORE_OK;
        }

        previous = current;
        current = current->next;
    }

    return STORE_NOT_FOUND;
}

StoreResult event_store_update(EventStore *store,
                               unsigned int id,
                               const Event *replacement)
{
    if (store == NULL || id == 0 ||
        !event_is_valid(replacement)) {
        return STORE_INVALID;
    }

    EventNode *target = store->head;

    while (target != NULL && target->event.id != id) {
        target = target->next;
    }

    if (target == NULL) {
        return STORE_NOT_FOUND;
    }

    StoreResult result = check_series(store, replacement, id);

    if (result != STORE_OK) {
        return result;
    }

    target->event = *replacement;
    target->event.id = id;

    return STORE_OK;
}

/*
 * Encodes text as hexadecimal so spaces, newlines, and separators
 * cannot be confused with the database structure.
 */
static int write_hex_text(FILE *file, const char *text)
{
    const unsigned char *current = (const unsigned char *)text;

    while (*current != '\0') {
        if (fprintf(file, "%02x", (unsigned int)*current) < 0) {
            return 0;
        }

        current++;
    }

    return fputc('\n', file) != EOF;
}

StoreFileResult event_store_save(const EventStore *store,
                                 const char *path)
{
    if (store == NULL || path == NULL || path[0] == '\0') {
        return STORE_FILE_INVALID;
    }

    const char suffix[] = ".tmp.XXXXXX";
    size_t path_length = strlen(path);

    if (path_length > SIZE_MAX - sizeof suffix) {
        return STORE_FILE_INVALID;
    }

    char *temporary = malloc(path_length + sizeof suffix);

    if (temporary == NULL) {
        return STORE_FILE_NO_MEMORY;
    }

    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, suffix, sizeof suffix);

    int descriptor = mkstemp(temporary);

    if (descriptor == -1) {
        free(temporary);
        return STORE_FILE_IO_ERROR;
    }

    FILE *file = fdopen(descriptor, "w");

    if (file == NULL) {
        close(descriptor);
        unlink(temporary);
        free(temporary);
        return STORE_FILE_IO_ERROR;
    }

    int success =
        fprintf(file, "CALENDAR 1\nNEXT %u\nCOUNT %zu\n",
                store->next_id, store->count) >= 0;

    const EventNode *current = store->head;

    while (success && current != NULL) {
        const Event *event = &current->event;

        success =
            fprintf(file, "EVENT %u %jd %jd %u %u\n",
                    event->id,
                    (intmax_t)event->start,
                    (intmax_t)event->end,
                    (unsigned int)event->recurrence.type,
                    event->recurrence.occurrences) >= 0;

        if (success) {
            success = write_hex_text(file, event->title);
        }

        if (success) {
            success = write_hex_text(file, event->location);
        }

        current = current->next;
    }

    if (success && fflush(file) == EOF) {
        success = 0;
    }

    if (success && fsync(descriptor) == -1) {
        success = 0;
    }

    if (fclose(file) == EOF) {
        success = 0;
    }

    if (success && rename(temporary, path) == -1) {
        success = 0;
    }

    if (!success) {
        unlink(temporary);
    }

    free(temporary);

    return success ? STORE_FILE_OK : STORE_FILE_IO_ERROR;
}

/* Reads one bounded line, rejecting embedded null characters. */
static int database_read_line(FILE *file, char *buffer, size_t size)
{
    size_t used = 0;
    int ch;

    while ((ch = fgetc(file)) != EOF) {
        if (ch == '\n') {
            buffer[used] = '\0';
            return 1;
        }

        if (ch == '\0' || used >= size - 1) {
            return 0;
        }

        buffer[used++] = (char)ch;
    }

    /* Saved database lines must end with a newline. */
    return 0;
}

/* Parses an exact prefix followed by space-separated integers. */
static int database_read_numbers(FILE *file, const char *prefix,
                                  intmax_t *values, size_t count)
{
    char line[256];

    if (!database_read_line(file, line, sizeof line)) {
        return 0;
    }

    size_t prefix_length = strlen(prefix);

    if (strncmp(line, prefix, prefix_length) != 0) {
        return 0;
    }

    const char *position = line + prefix_length;

    for (size_t i = 0; i < count; i++) {
        const char *digits = position;

        if (*digits == '-') {
            digits++;
        }

        if (*digits < '0' || *digits > '9') {
            return 0;
        }

        errno = 0;
        char *end;
        values[i] = strtoimax(position, &end, 10);

        if (errno == ERANGE) {
            return 0;
        }

        if (i + 1 < count) {
            if (*end != ' ') {
                return 0;
            }

            position = end + 1;
        } else if (*end != '\0') {
            return 0;
        }
    }

    return 1;
}

static int database_read_text(FILE *file, char *text, size_t size)
{
    char line[2 * EVENT_TITLE_SIZE + 2];

    if (!database_read_line(file, line, sizeof line)) {
        return 0;
    }

    size_t length = strlen(line);

    if (length % 2 != 0 || length / 2 >= size) {
        return 0;
    }

    for (size_t i = 0; i < length; i += 2) {
        unsigned int byte = 0;

        for (size_t j = 0; j < 2; j++) {
            char ch = line[i + j];
            unsigned int digit;

            if (ch >= '0' && ch <= '9') {
                digit = (unsigned int)(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                digit = (unsigned int)(ch - 'a') + 10;
            } else {
                return 0;
            }

            byte = byte * 16 + digit;
        }

        if (byte == 0) {
            return 0;
        }

        ((unsigned char *)text)[i / 2] = (unsigned char)byte;
    }

    text[length / 2] = '\0';
    return 1;
}

StoreFileResult event_store_load(const char *path,
                                 EventStore **out_store)
{
    if (path == NULL || path[0] == '\0' || out_store == NULL) {
        return STORE_FILE_INVALID;
    }

    FILE *file = fopen(path, "r");

    if (file == NULL) {
        return errno == ENOENT
            ? STORE_FILE_NOT_FOUND
            : STORE_FILE_IO_ERROR;
    }

    EventStore *loaded = event_store_create();

    if (loaded == NULL) {
        fclose(file);
        return STORE_FILE_NO_MEMORY;
    }

    StoreFileResult result = STORE_FILE_INVALID;
    char header[32];
    intmax_t next_value;
    intmax_t count_value;
    unsigned int largest_id = 0;

    if (!database_read_line(file, header, sizeof header) ||
        strcmp(header, "CALENDAR 1") != 0 ||
        !database_read_numbers(file, "NEXT ", &next_value, 1) ||
        next_value < 0 || (uintmax_t)next_value > UINT_MAX ||
        !database_read_numbers(file, "COUNT ", &count_value, 1) ||
        count_value < 0 || count_value > MAX_EVENTS) {
        goto finish;
    }

    EventNode *tail = NULL;

    for (intmax_t i = 0; i < count_value; i++) {
        intmax_t fields[5];

        if (!database_read_numbers(file, "EVENT ", fields, 5) ||
            fields[0] <= 0 || (uintmax_t)fields[0] > UINT_MAX ||
            fields[3] < REPEAT_NONE || fields[3] > REPEAT_WEEKLY ||
            fields[4] < 1 || fields[4] > MAX_OCCURRENCES) {
            goto finish;
        }

        Event event = {
            .id = (unsigned int)fields[0],
            .start = (time_t)fields[1],
            .end = (time_t)fields[2],
            .recurrence = {
                (RepeatType)fields[3],
                (unsigned int)fields[4]
            }
        };

        if ((intmax_t)event.start != fields[1] ||
            (intmax_t)event.end != fields[2] ||
            !database_read_text(file, event.title,
                                sizeof event.title) ||
            !database_read_text(file, event.location,
                                sizeof event.location) ||
            !event_is_valid(&event)) {
            goto finish;
        }

        Event existing;

        if (event_store_get(loaded, event.id, &existing) !=
                STORE_NOT_FOUND ||
            check_series(loaded, &event, 0) != STORE_OK) {
            goto finish;
        }

        EventNode *node = malloc(sizeof *node);

        if (node == NULL) {
            result = STORE_FILE_NO_MEMORY;
            goto finish;
        }

        node->event = event;
        node->next = NULL;

        if (tail == NULL) {
            loaded->head = node;
        } else {
            tail->next = node;
        }

        tail = node;
        loaded->count++;

        if (event.id > largest_id) {
            largest_id = event.id;
        }
    }

    /* Zero means the ID counter was exhausted. */
    if (next_value != 0 &&
        (unsigned int)next_value <= largest_id) {
        goto finish;
    }

    /* Reject unexpected trailing content. */
    if (fgetc(file) != EOF) {
        goto finish;
    }

    loaded->next_id = (unsigned int)next_value;
    result = STORE_FILE_OK;

finish:
    if (ferror(file)) {
        result = STORE_FILE_IO_ERROR;
    }

    if (fclose(file) == EOF) {
        result = STORE_FILE_IO_ERROR;
    }

    if (result != STORE_FILE_OK) {
        event_store_destroy(loaded);
        return result;
    }

    *out_store = loaded;
    return STORE_FILE_OK;
}
