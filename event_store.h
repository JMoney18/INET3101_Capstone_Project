#ifndef EVENT_STORE_H
#define EVENT_STORE_H

#include <stddef.h>
#include "event.h"

#ifndef MAX_EVENTS
#define MAX_EVENTS 10000
#endif

typedef struct EventStore EventStore;

typedef enum {
    STORE_OK,
    STORE_INVALID,
    STORE_NOT_FOUND,
    STORE_CONFLICT,
    STORE_NO_MEMORY,
    STORE_ID_EXHAUSTED,
    STORE_FULL
} StoreResult;

/* Creates an empty store. Returns NULL if allocation fails. */
EventStore *event_store_create(void);

/* Frees all list nodes and the store itself. Accepts NULL. */
void event_store_destroy(EventStore *store);

/* Returns the number of stored event records. */
size_t event_store_count(const EventStore *store);

/*
 * Adds a copy of an event after validation and conflict checking.
 * The store assigns its ID; the input event's ID is ignored.
 * On success, writes the assigned ID to assigned_id.
 */
StoreResult event_store_add(EventStore *store,
                            const Event *event,
                            unsigned int *assigned_id);

/* Copies the matching event into out_event. */
StoreResult event_store_get(const EventStore *store,
                            unsigned int id,
                            Event *out_event);

/*
 * Replaces an event after validation and conflict checking.
 * Preserves id and ignores replacement->id.
 * Failure leaves the original event unchanged.
 */
StoreResult event_store_update(EventStore *store,
                               unsigned int id,
                               const Event *replacement);

/* Removes an event and frees its list node. */
StoreResult event_store_delete(EventStore *store, unsigned int id);

/*
 * Copies the event at a zero-based list position into out_event.
 * Returns STORE_NOT_FOUND if the position does not exist.
 */
StoreResult event_store_get_at(const EventStore *store,
                               size_t index,
                               Event *out_event);
                            
typedef enum {
    STORE_FILE_OK,
    STORE_FILE_NOT_FOUND,
    STORE_FILE_INVALID,
    STORE_FILE_IO_ERROR,
    STORE_FILE_NO_MEMORY
} StoreFileResult;

/*
 * Saves all events and the next-ID counter.
 * Writes to a temporary file before replacing path.
 * The caller must hold the application's database lock.
 */
StoreFileResult event_store_save(const EventStore *store,
                                 const char *path);

/*
 * Loads and validates a new store from path.
 * On success, assigns the new store to *out_store.
 * On failure, *out_store remains unchanged.
 *
 * Does not destroy any existing store owned by the caller.
 * The caller must hold the application's database lock.
 */
StoreFileResult event_store_load(const char *path,
                                 EventStore **out_store);

#endif