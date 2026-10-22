#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "event.h"

int event_is_valid(const Event *event)
{
    if (event == NULL) {
        return 0;
    }

    /* Both text fields must contain a terminating null character. */
    if (memchr(event->title, '\0', sizeof event->title) == NULL ||
        memchr(event->location, '\0', sizeof event->location) == NULL) {
        return 0;
    }

    /* A title must contain at least one non-whitespace character. */
    int has_title = 0;

    for (size_t i = 0; event->title[i] != '\0'; i++) {
        if (!isspace((unsigned char)event->title[i])) {
            has_title = 1;
            break;
        }
    }

    if (!has_title || event->end <= event->start) {
        return 0;
    }

    if (event->recurrence.occurrences == 0 ||
        event->recurrence.occurrences > MAX_OCCURRENCES) {
        return 0;
    }

    switch (event->recurrence.type) {
        case REPEAT_NONE:
            return event->recurrence.occurrences == 1;

        case REPEAT_DAILY:
        case REPEAT_WEEKLY:
            return 1;

        default:
            return 0;
    }
}

/*
 * Moves a timestamp forward by calendar days, preserving local
 * clock time. Years are limited to 1970 through 9999.
 */
static int shift_local_days(time_t original,
                            unsigned int days,
                            time_t *result)
{
    struct tm *local = localtime(&original);

    if (local == NULL) {
        return 0;
    }

    struct tm base = *local;

    if (base.tm_year < 70 || base.tm_year > 8099) {
        return 0;
    }

    /*
     * First determine the target calendar date using noon.
     * The caller bounds days to at most 2548.
     */
    struct tm date = base;
    date.tm_hour = 12;
    date.tm_min = 0;
    date.tm_sec = 0;
    date.tm_mday += (int)days;
    date.tm_isdst = -1;

    time_t noon = mktime(&date);

    if (noon == (time_t)-1 ||
        date.tm_year < 70 || date.tm_year > 8099) {
        return 0;
    }

    struct tm candidate = date;
    candidate.tm_hour = base.tm_hour;
    candidate.tm_min = base.tm_min;
    candidate.tm_sec = base.tm_sec;
    candidate.tm_isdst = -1;

    time_t shifted = mktime(&candidate);

    if (shifted == (time_t)-1) {
        return 0;
    }

    /*
     * mktime can normalize a nonexistent clock time.
     * Reject that change instead of silently accepting it.
     */
    if (candidate.tm_year != date.tm_year ||
        candidate.tm_mon != date.tm_mon ||
        candidate.tm_mday != date.tm_mday ||
        candidate.tm_hour != base.tm_hour ||
        candidate.tm_min != base.tm_min ||
        candidate.tm_sec != base.tm_sec) {
        return 0;
    }

    *result = shifted;
    return 1;
}

int event_get_occurrence(const Event *event,
                         unsigned int index,
                         time_t *out_start,
                         time_t *out_end)
{
    if (!event_is_valid(event) ||
        out_start == NULL || out_end == NULL ||
        out_start == out_end ||
        index >= event->recurrence.occurrences) {
        return 0;
    }

    if (index == 0) {
        *out_start = event->start;
        *out_end = event->end;
        return 1;
    }

    unsigned int days;

    switch (event->recurrence.type) {
        case REPEAT_DAILY:
            days = index;
            break;

        case REPEAT_WEEKLY:
            days = index * 7;
            break;

        default:
            return 0;
    }

    time_t start;
    time_t end;

    if (!shift_local_days(event->start, days, &start) ||
        !shift_local_days(event->end, days, &end) ||
        end <= start) {
        return 0;
    }

    *out_start = start;
    *out_end = end;
    return 1;
}

int event_parse_datetime(const char *text, time_t *out_time)
{
    if (text == NULL || out_time == NULL || strlen(text) != 16) {
        return 0;
    }

    if (text[4] != '-' || text[7] != '-' ||
        text[10] != ' ' || text[13] != ':') {
        return 0;
    }

    for (size_t i = 0; i < 16; i++) {
        if (i == 4 || i == 7 || i == 10 || i == 13) {
            continue;
        }

        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
    }

    int year;
    int month;
    int day;
    int hour;
    int minute;

    if (sscanf(text, "%4d-%2d-%2d %2d:%2d",
               &year, &month, &day, &hour, &minute) != 5) {
        return 0;
    }

    if (year < 1970 || year > 9999 ||
        month < 1 || month > 12 ||
        day < 1 || day > 31 ||
        hour < 0 || hour > 23 ||
        minute < 0 || minute > 59) {
        return 0;
    }

    struct tm date = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_sec = 0,
        .tm_isdst = -1
    };

    time_t parsed = mktime(&date);

    if (parsed == (time_t)-1) {
        return 0;
    }

    /* Reject invalid dates or nonexistent times normalized by mktime. */
    if (date.tm_year != year - 1900 ||
        date.tm_mon != month - 1 ||
        date.tm_mday != day ||
        date.tm_hour != hour ||
        date.tm_min != minute ||
        date.tm_sec != 0) {
        return 0;
    }

    *out_time = parsed;
    return 1;
}
