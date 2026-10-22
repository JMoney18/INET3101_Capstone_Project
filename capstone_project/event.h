#ifndef EVENT_H
#define EVENT_H

#include <time.h>

#define EVENT_TITLE_SIZE 100
#define EVENT_LOCATION_SIZE 100
#define MAX_OCCURRENCES 365

typedef enum {
    REPEAT_NONE,
    REPEAT_DAILY,
    REPEAT_WEEKLY
} RepeatType;

typedef struct {
    RepeatType type;
    unsigned int occurrences;
} Recurrence;

typedef struct {
    unsigned int id;
    char title[EVENT_TITLE_SIZE];
    char location[EVENT_LOCATION_SIZE];
    time_t start;
    time_t end;
    Recurrence recurrence;
} Event;

/*
 * Calculates one occurrence using a zero-based index.
 * Index 0 is the original appointment.
 *
 * Returns 1 on success, or 0 for invalid input or an
 * occurrence that cannot be represented.
 *
 * On failure, output values remain unchanged.
 * out_start and out_end must point to separate time_t variables.
 */
int event_get_occurrence(const Event *event,
                         unsigned int index,
                         time_t *out_start,
                         time_t *out_end);

/*
 * Parses local time in exactly YYYY-MM-DD HH:MM format.
 * Supports years 1970 through 9999.
 * Returns 1 on success; failure leaves out_time unchanged.
 */
int event_parse_datetime(const char *text, time_t *out_time);

/* Returns 1 for valid event fields, or 0 for invalid fields. */
int event_is_valid(const Event *event);

#endif
