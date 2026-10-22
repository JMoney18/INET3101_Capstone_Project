#include <stdio.h>
#include <stdlib.h>
#include "event_store.h"
#include <string.h>

static int check_date_shift(int year, int month, int day,
                            int hour, int minute,
                            int expected_month, int expected_day,
                            int should_succeed)
{
    struct tm start = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_isdst = -1
    };

    struct tm end = start;
    end.tm_hour++;

    Event event = {
        .title = "Date boundary check",
        .recurrence = { REPEAT_DAILY, 2 }
    };

    event.start = mktime(&start);
    event.end = mktime(&end);

    if (event.start == (time_t)-1 || event.end == (time_t)-1) {
        return 0;
    }

    time_t next_start = 0;
    time_t next_end = 0;

    int success = event_get_occurrence(&event, 1,
                                       &next_start, &next_end);

    if (!should_succeed) {
        return !success && next_start == 0 && next_end == 0;
    }

    if (!success) {
        return 0;
    }

    struct tm *local = localtime(&next_start);

    if (local == NULL ||
        local->tm_year != year - 1900 ||
        local->tm_mon != expected_month - 1 ||
        local->tm_mday != expected_day ||
        local->tm_hour != hour ||
        local->tm_min != minute) {
        return 0;
    }

    local = localtime(&next_end);

    return local != NULL &&
           local->tm_year == year - 1900 &&
           local->tm_mon == expected_month - 1 &&
           local->tm_mday == expected_day &&
           local->tm_hour == hour + 1 &&
           local->tm_min == minute;
}

int main(void)
{
    EventStore *store = event_store_create();

    if (store == NULL) {
        fprintf(stderr, "Failed to create the event store.\n");
        return EXIT_FAILURE;
    }

    printf("Calendar Management System\n");
    printf("Events stored: %zu\n", event_store_count(store));

    Event event;

    if (event_store_get(store, 1, &event) != STORE_NOT_FOUND ||
        event_store_get_at(store, 0, &event) != STORE_NOT_FOUND ||
        event_store_get(store, 1, NULL) != STORE_INVALID) {
        fprintf(stderr, "Event lookup checks failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Empty-store and invalid-output checks passed.\n");

    Event sample = {
        .title = "Study session",
        .location = "Library",
        .start = 1000,
        .end = 2000,
        .recurrence = { REPEAT_NONE, 1 }
    };

    if (!event_is_valid(&sample)) {
        fprintf(stderr, "Valid event was rejected.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    sample.end = sample.start;

    if (event_is_valid(&sample) || event_is_valid(NULL)) {
        fprintf(stderr, "Invalid event was accepted.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Event validation checks passed.\n");

    /* Restore the valid end time changed by the earlier test. */
    sample.end = 2000;

    unsigned int assigned_id = 0;
    Event retrieved;

    if (event_store_add(store, &sample, &assigned_id) != STORE_OK ||
        assigned_id != 1 ||
        event_store_count(store) != 1 ||
        event_store_get(store, assigned_id, &retrieved) != STORE_OK ||
        retrieved.start != sample.start ||
        retrieved.end != sample.end) {
        fprintf(stderr, "Add or retrieval checks failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    /* The same time slot must be rejected without changing the count. */
    if (event_store_add(store, &sample, &assigned_id) != STORE_CONFLICT ||
        event_store_count(store) != 1) {
        fprintf(stderr, "Conflict rejection check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    /* An event starting exactly when the first ends is allowed. */
    sample.start = 2000;
    sample.end = 3000;

    if (event_store_add(store, &sample, &assigned_id) != STORE_OK ||
        assigned_id != 2 ||
        event_store_count(store) != 2 ||
        event_store_get_at(store, 1, &retrieved) != STORE_OK ||
        retrieved.id != assigned_id) {
        fprintf(stderr, "Back-to-back event check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Add, retrieval, and conflict checks passed.\n");
    printf("Events stored after additions: %zu\n",
           event_store_count(store));

    /* Remove the second node and verify the first remains. */
    if (event_store_delete(store, 2) != STORE_OK ||
        event_store_count(store) != 1 ||
        event_store_get(store, 2, &retrieved) != STORE_NOT_FOUND ||
        event_store_get_at(store, 0, &retrieved) != STORE_OK ||
        retrieved.id != 1) {
        fprintf(stderr, "Non-head deletion check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    /* Deleting a missing ID must leave the list unchanged. */
    if (event_store_delete(store, 2) != STORE_NOT_FOUND ||
        event_store_delete(store, 0) != STORE_INVALID ||
        event_store_count(store) != 1) {
        fprintf(stderr, "Invalid deletion checks failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    /* Remove the head, leaving an empty store. */
    if (event_store_delete(store, 1) != STORE_OK ||
        event_store_count(store) != 0 ||
        event_store_get_at(store, 0, &retrieved) != STORE_NOT_FOUND ||
        event_store_delete(store, 1) != STORE_NOT_FOUND) {
        fprintf(stderr, "Head deletion check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Deletion checks passed.\n");
    printf("Events stored after deletions: %zu\n",
           event_store_count(store));

    Event first = {
        .title = "First appointment",
        .location = "Library",
        .start = 1000,
        .end = 2000,
        .recurrence = { REPEAT_NONE, 1 }
    };

    Event second = {
        .title = "Second appointment",
        .location = "Classroom",
        .start = 3000,
        .end = 4000,
        .recurrence = { REPEAT_NONE, 1 }
    };

    unsigned int first_id = 0;
    unsigned int second_id = 0;

    if (event_store_add(store, &first, &first_id) != STORE_OK ||
        event_store_add(store, &second, &second_id) != STORE_OK) {
        fprintf(stderr, "Update test setup failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    /* This overlaps its own old time, which must be allowed. */
    Event replacement = first;
    replacement.id = 999;
    replacement.start = 1500;
    replacement.end = 2500;

    if (event_store_update(store, first_id, &replacement) != STORE_OK ||
        event_store_get(store, first_id, &retrieved) != STORE_OK ||
        retrieved.id != first_id ||
        retrieved.start != 1500 ||
        retrieved.end != 2500) {
        fprintf(stderr, "Successful update check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    /* This overlaps the second appointment and must be rejected. */
    replacement.start = 3500;
    replacement.end = 4500;

    if (event_store_update(store, first_id, &replacement) !=
            STORE_CONFLICT ||
        event_store_get(store, first_id, &retrieved) != STORE_OK ||
        retrieved.start != 1500 ||
        retrieved.end != 2500) {
        fprintf(stderr, "Rejected update changed the event.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    /* Invalid times must also leave the original unchanged. */
    replacement.end = replacement.start;

    if (event_store_update(store, first_id, &replacement) !=
            STORE_INVALID ||
        event_store_get(store, first_id, &retrieved) != STORE_OK ||
        retrieved.start != 1500 ||
        retrieved.end != 2500 ||
        event_store_count(store) != 2) {
        fprintf(stderr, "Invalid update check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Update and unchanged-on-failure checks passed.\n");

    struct tm start_date = {
        .tm_year = 2026 - 1900,
        .tm_mon = 0,
        .tm_mday = 15,
        .tm_hour = 10,
        .tm_isdst = -1
    };

    struct tm end_date = start_date;
    end_date.tm_hour = 11;

    Event recurring = {
        .title = "Recurring study session",
        .location = "Library",
        .recurrence = { REPEAT_DAILY, 3 }
    };

    recurring.start = mktime(&start_date);
    recurring.end = mktime(&end_date);

    time_t occurrence_start;
    time_t occurrence_end;

    if (recurring.start == (time_t)-1 ||
        recurring.end == (time_t)-1 ||
        !event_get_occurrence(&recurring, 2,
                              &occurrence_start, &occurrence_end)) {
        fprintf(stderr, "Daily occurrence calculation failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    struct tm *calculated = localtime(&occurrence_start);

    if (calculated == NULL ||
        calculated->tm_year != 2026 - 1900 ||
        calculated->tm_mon != 0 ||
        calculated->tm_mday != 17 ||
        calculated->tm_hour != 10) {
        fprintf(stderr, "Daily occurrence date is incorrect.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    recurring.recurrence.type = REPEAT_WEEKLY;

    if (!event_get_occurrence(&recurring, 2,
                              &occurrence_start, &occurrence_end)) {
        fprintf(stderr, "Weekly occurrence calculation failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    calculated = localtime(&occurrence_start);

    if (calculated == NULL ||
        calculated->tm_year != 2026 - 1900 ||
        calculated->tm_mon != 0 ||
        calculated->tm_mday != 29 ||
        calculated->tm_hour != 10) {
        fprintf(stderr, "Weekly occurrence date is incorrect.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    time_t saved_start = occurrence_start;
    time_t saved_end = occurrence_end;

    if (event_get_occurrence(&recurring, 3,
                             &occurrence_start, &occurrence_end) ||
        occurrence_start != saved_start ||
        occurrence_end != saved_end) {
        fprintf(stderr, "Invalid occurrence index check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Daily and weekly occurrence checks passed.\n");

    /*
     * Run these checks with TZ=America/Chicago.
     * The spring clock change in 2026 occurs on March 8.
     */
    if (!check_date_shift(2026, 1, 31, 10, 0, 2, 1, 1) ||
        !check_date_shift(2028, 2, 28, 10, 0, 2, 29, 1) ||
        !check_date_shift(2026, 2, 28, 10, 0, 3, 1, 1) ||
        !check_date_shift(2026, 3, 7, 10, 0, 3, 8, 1) ||
        !check_date_shift(2026, 3, 7, 2, 30, 3, 8, 0) ||
        !check_date_shift(2026, 10, 31, 10, 0, 11, 1, 1)) {
        fprintf(stderr, "Calendar boundary checks failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Month, leap-year, and daylight-saving checks passed.\n");

    /* The earlier recurring variable is a three-week series. */
    unsigned int series_id = 0;
    size_t count_before = event_store_count(store);

    if (event_store_add(store, &recurring, &series_id) != STORE_OK ||
        event_store_count(store) != count_before + 1 ||
        !event_get_occurrence(&recurring, 2,
                              &occurrence_start, &occurrence_end)) {
        fprintf(stderr, "Recurring event addition failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    Event collision = {
        .title = "Conflicting appointment",
        .start = occurrence_start,
        .end = occurrence_end,
        .recurrence = { REPEAT_NONE, 1 }
    };

    unsigned int unused_id = 0;

    if (event_store_add(store, &collision, &unused_id) !=
            STORE_CONFLICT ||
        event_store_count(store) != count_before + 1) {
        fprintf(stderr, "Later-occurrence conflict check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    /* An unchanged series must not conflict with itself on update. */
    if (event_store_update(store, series_id, &recurring) != STORE_OK) {
        fprintf(stderr, "Recurring event update failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Recurring storage and later-occurrence checks passed.\n");

    time_t parsed_time = 0;

    if (!event_parse_datetime("2028-02-29 14:30", &parsed_time)) {
        fprintf(stderr, "Valid date parsing failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    time_t saved_time = parsed_time;

    if (event_parse_datetime("2026-02-29 14:30", &parsed_time) ||
        event_parse_datetime("2026-11-15 25:00", &parsed_time) ||
        event_parse_datetime("2026-3-08 10:00", &parsed_time) ||
        event_parse_datetime("2026-03-08 02:30", &parsed_time) ||
        parsed_time != saved_time) {
        fprintf(stderr, "Invalid date rejection failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Date input parsing checks passed.\n");

    /*
     * Dedicated test file: do not run multiple test copies at once.
     * The interactive application will use a different database.
     */
    if (event_store_save(store, "test_calendar.db") != STORE_FILE_OK) {
        fprintf(stderr, "Database save check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Database save check passed.\n");

    EventStore *loaded_store = NULL;

    if (event_store_load("test_calendar.db", &loaded_store) !=
            STORE_FILE_OK) {
        fprintf(stderr, "Database load check failed.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    int matches =
        event_store_count(loaded_store) == event_store_count(store);

    for (size_t i = 0; matches && i < event_store_count(store); i++) {
        Event before;
        Event after;

        if (event_store_get_at(store, i, &before) != STORE_OK ||
            event_store_get_at(loaded_store, i, &after) != STORE_OK ||
            before.id != after.id ||
            strcmp(before.title, after.title) != 0 ||
            strcmp(before.location, after.location) != 0 ||
            before.start != after.start ||
            before.end != after.end ||
            before.recurrence.type != after.recurrence.type ||
            before.recurrence.occurrences !=
                after.recurrence.occurrences) {
            matches = 0;
        }
    }

    Event extra = {
        .title = "ID preservation check",
        .recurrence = { REPEAT_NONE, 1 }
    };

    unsigned int restored_id = 0;

    if (!event_parse_datetime("2030-01-15 10:00", &extra.start) ||
        !event_parse_datetime("2030-01-15 11:00", &extra.end) ||
        event_store_add(loaded_store, &extra, &restored_id) !=
            STORE_OK ||
        restored_id != 6) {
        fprintf(stderr, "Saved next-ID check failed.\n");
        event_store_destroy(loaded_store);
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Saved next-ID check passed.\n");

    event_store_destroy(loaded_store);

    if (!matches) {
        fprintf(stderr, "Loaded event records do not match.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Database load and record checks passed.\n");

    FILE *damaged = fopen("test_calendar_invalid.db", "w");

    if (damaged == NULL) {
        fprintf(stderr, "Unable to create damaged-file fixture.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    /* Declares one event, but omits that event's data. */
    int fixture_ok =
        fputs("CALENDAR 1\nNEXT 2\nCOUNT 1\n", damaged) != EOF;

    if (fclose(damaged) == EOF) {
        fixture_ok = 0;
    }

    if (!fixture_ok) {
        fprintf(stderr, "Unable to write damaged-file fixture.\n");
        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    EventStore *unchanged = store;

    StoreFileResult load_result =
        event_store_load("test_calendar_invalid.db", &unchanged);

    if (load_result != STORE_FILE_INVALID || unchanged != store) {
        fprintf(stderr, "Damaged-file rejection check failed.\n");

        if (unchanged != store) {
            event_store_destroy(unchanged);
        }

        event_store_destroy(store);
        return EXIT_FAILURE;
    }

    printf("Damaged-file rejection and unchanged-output checks passed.\n");

    event_store_destroy(store);

    return EXIT_SUCCESS;
}