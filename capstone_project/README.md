# Calendar Management System

## Problem Statement

For my capstone project, I chose to build a calendar management system in C. With the help of ChatGPT, I developed the program step by step and tested each part in VS Code before moving on. The program lets a user add, view, edit, and delete appointments through a terminal menu. Events can happen once or repeat daily or weekly. The program also checks for scheduling conflicts, saves events between runs, and lets the user share events with a TCP server.

This project brings together several topics from the class, including structs, pointers, dynamic memory, linked lists, file I/O, TCP sockets, and file locking.

## Building and Running the Program

### Requirements

I developed and tested this project on macOS using Apple Clang. It requires:

- A C compiler that supports C11.
- Make.
- A Unix-like system with POSIX sockets and `flock`.
- Netcat (`nc`) for the `make ping` and `make stop` commands.

I have not verified the program on other operating systems.

Run the commands below from the `capstone_project` folder.

### Compile

```sh
make
```

This builds the calendar application and the sharing server.

### Start the Server

In one terminal, run:

```sh
./calendar_server
```

### Start the Calendar

In a second terminal, run:

```sh
./calendar
```

The calendar menu has these options:

| Option | Action |
| --- | --- |
| `1` | List events and their occurrences |
| `2` | Add an event |
| `3` | Delete an event |
| `4` | Edit an event |
| `5` | Share an event with the server |
| `0` | Save and exit |

Dates must use the format `YYYY-MM-DD HH:MM`, with a 24-hour clock. For example:

```text
2027-12-10 14:00
```

The end time must be after the start time. A title is required, but the location can be left blank. Titles and locations can each contain up to 99 bytes of text, which means 99 characters when using ordinary ASCII text.

Events can repeat daily or weekly, with between 1 and 365 total occurrences. The count includes the first event. Editing or deleting a recurring event changes or removes the whole series.

### Check or Stop the Server

From a terminal at the shell prompt, run:

```sh
make ping
```

The server should respond with:

```text
PONG
```

To shut down the server normally, run:

```sh
make stop
```

The response should be:

```text
BYE
```

The server then saves its calendar and closes. If the calendar menu is running in that terminal, exit it before entering these commands.

## Testing

Run the automated tests with:

```sh
make test
```

This runs two test programs:

- `test_calendar` checks event validation, CRUD operations, recurrence, conflicts, and file persistence.
- `test_capacity` checks what happens when the event store reaches its limit.

The capacity test uses a separate build with a three-event limit so the boundary can be tested quickly. The normal application limit is 10,000 event records.

The tests use the `America/Chicago` time zone so the daylight-saving checks have predictable results. They also create their own test database files. Only one copy of the tests should run at a time because the filenames are shared.

### Sanitizer Checks

I also tested the program with AddressSanitizer and UndefinedBehaviorSanitizer. These tools help find problems such as out-of-bounds memory access, use-after-free errors, and some types of undefined behavior.

To build and run with these checks enabled, stop the calendar and server, then run:

```sh
make clean
make CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer"
make test CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer"
```

To return to a normal build:

```sh
make clean
make
```

The clean command removes compiled files but keeps the saved databases.

## Design & Architecture Details

### File Organization

With help from ChatGPT, I split the program into multiple files so the menu, event logic, storage, and networking have separate responsibilities. This also made it easier to work on one part at a time and check that it worked before adding more features.

| Files | Purpose |
| --- | --- |
| `event.h`, `event.c` | Define events and recurrence, validate fields, parse dates, and calculate occurrences |
| `event_store.h`, `event_store.c` | Manage the linked list, CRUD operations, conflict detection, and file persistence |
| `network.h`, `network.c` | Handle TCP communication and event sharing |
| `main.c` | Run the calendar menu and manage the local database and lock |
| `server_main.c` | Start the server and manage its database and lock |
| `test_calendar.c` | Test calendar functionality |
| `test_capacity.c` | Test the event capacity limit |
| `Makefile` | Build the programs, run tests, and provide server shortcuts |

### Event Structures

Each `Event` stores:

- An unsigned event ID.
- A title.
- A location.
- Start and end times using `time_t`.
- A `Recurrence` struct.

The recurrence struct stores the repeat type and the number of occurrences. An enum identifies whether an event happens once, daily, or weekly.

I used fixed-size arrays for the title and location to keep text storage simple and predictable. The program checks that the text fits before accepting it.

### Linked-List EventStore ADT

The `EventStore` is an Abstract Data Type implemented with a linked list. Its internal nodes are hidden in `event_store.c`. Other parts of the program use functions to add, retrieve, update, or delete events instead of changing the pointers directly.

Each node is allocated dynamically and stores a copy of an event. The store owns that memory. Deleting an event frees its node, and destroying the store frees all remaining nodes and the store itself.

Retrieval functions copy event data into the caller's variable. They do not return pointers to internal nodes, which helps prevent other parts of the program from accidentally changing the list.

Event IDs start at 1. Deleted IDs are not reused. The next available ID is saved in the database so this behavior continues after restarting.

ChatGPT helped me plan the memory ownership rules before implementing the list. Having clear rules for allocating, copying, and freeing events made the pointer-based code easier to follow.

### Recurrence and Conflict Detection

Daily and weekly events repeat at the same local clock times. The program advances calendar dates instead of adding a fixed number of seconds because daylight-saving changes can make a day shorter or longer than 24 hours.

The program rejects repeated times that do not exist during a clock change. Normal date input supports years from 1970 through 9999.

Two appointments conflict if each starts before the other ends. An appointment ending at 15:00 does not conflict with one starting at 15:00.

The program checks every occurrence in a recurring series. It also checks whether occurrences within the proposed series overlap each other. When updating an event, the conflict check skips that event's original record.

Invalid or conflicting additions and updates leave the existing calendar unchanged.

Recurrence was one of the more complicated parts of the project. With AI assistance, I worked through cases involving month boundaries, leap years, and daylight-saving changes, then ran tests to check the results.

### File Persistence

The local calendar uses:

```text
calendar.db
calendar.lock
```

The server uses separate files:

```text
server_calendar.db
server_calendar.lock
```

This allows the calendar and server to run at the same time.

The database uses a versioned text format beginning with:

```text
CALENDAR 1
```

It then stores the next event ID, record count, and event records. Titles and locations are encoded as hexadecimal text so their contents cannot be confused with the file's separators.

Saving writes to a temporary file first. The program checks the writes, flushes and synchronizes the file, closes it, and then renames it over the database. This keeps a failed write from replacing the previous database with a partially written file.

Loading builds a separate event store and validates its records. It checks IDs, recurrence information, and scheduling conflicts. If loading fails, the partially loaded store is freed and the caller's store pointer is left unchanged.

A missing database starts a new calendar. An invalid or unreadable database stops startup instead of being treated as an empty calendar and overwritten.

The local calendar saves when the user exits normally. The server saves each shared event before confirming success, and saves again during normal shutdown.

ChatGPT helped design the temporary-file approach and the checks used when loading records. I verified the persistence behavior by creating, editing, and deleting events, then restarting the program after each change.

### Concurrency Protection

Each application uses an exclusive `flock` on its lock file. The lock is acquired before loading and held until saving and cleanup are finished.

A second copy using the same lock file exits with an error instead of opening the same database. The lock files are separate from the database files because saving replaces the database file.

The server handles clients one at a time. This keeps two requests from changing its event store at the same time.

### TCP Event Sharing

The server listens on:

```text
127.0.0.1:5050
```

This limits connections to the same computer.

The protocol supports these commands:

| Command | Response |
| --- | --- |
| `PING` | `PONG` |
| `STOP` | `BYE`, followed by normal shutdown |
| `ADD1` | `OK`, `CONFLICT`, `INVALID`, or `SERVER_ERROR` |
| Unknown command | `ERROR` |

An `ADD1` request contains seven newline-separated lines:

```text
ADD1
Shared study
Library
2027-11-15 14:00
2027-11-15 15:00
2
3
```

The last two values identify the repeat type and occurrence count. Repeat types are `0` for none, `1` for daily, and `2` for weekly. An empty location is sent as an empty line.

The server validates the event, checks for conflicts, assigns its own ID, and saves the event before returning `OK`.

The socket code handles partial reads and writes, checks message lengths, and closes connections after processing. Send and receive timeouts help prevent an inactive connection from blocking the server indefinitely. SIGPIPE is ignored so a disconnected client does not terminate the program.

The client and server must use the same local time zone because the protocol sends local date and time strings.

With help from ChatGPT, I built the networking portion in stages. I started with `PING` and `PONG`, then added multiple connections, server storage, and event sharing through the C client. Testing each stage made it easier to find problems before connecting everything together.

## Pros & Cons of Solution

### What Worked Well

Developing the project step by step with AI assistance helped keep the work manageable. The modular structure made it easier to build and check one part at a time. ChatGPT also helped explain the purpose of the functions and data structures as they were added. Keeping the linked-list implementation behind an interface made memory ownership easier to follow.

The program supports persistent CRUD operations and recurring events, and the tests check that rejected changes do not damage existing records. File locking prevents cooperating copies from overwriting each other's databases.

Using a sequential server kept the networking design manageable while still demonstrating TCP event sharing and conflict handling.

### Limitations and Possible Improvements

The linked list works for this project, but searching requires walking through records. Listing events by repeatedly retrieving an index is also inefficient for a large calendar. Conflict checking takes more work as the number of events and occurrences grows. The 10,000-record limit is a capacity limit, not a guarantee of fast performance.

Fixed-size text arrays simplify memory management, but they limit text length and reserve space even for short titles and locations.

Local changes are saved only when the user exits normally. Force-quitting can lose changes from that session. Autosaving and allowing the user to retry a failed save would improve this.

The server handles one client at a time. Its timeouts apply to individual socket operations, so a client sending data slowly could still delay other requests. A total request deadline would improve this.

Sharing creates a separate server record. Editing or deleting the local event does not update the server's copy. Full synchronization would need additional commands and a way to track the relationship between local and server IDs.

If the server saves an event but the response is lost, the client cannot know for sure whether sharing succeeded. Request IDs could make retries easier to handle.

Time-zone support is also limited. The programs must use the same time zone, and ambiguous times during the fall clock change use the C library's choice.

The server has no authentication and is intended for localhost use. Supporting connections from other computers would require more work on security and configuration.

Finally, saving synchronizes the temporary file but does not synchronize the containing directory after renaming it. It does not guarantee recovery from every possible power-loss situation.

## AI Audit Reflection

### How I Used AI

I used ChatGPT throughout the project to help plan the architecture, develop the code step by step, explain implementation choices, and review possible errors. I compiled and ran the program in VS Code after each checkpoint and shared the results before moving on.

The conversation included reviewing the event structures and ADT interface before implementation, checking memory ownership and cleanup, reviewing file persistence and socket code, and developing tests for edge cases.

AI assistance was useful, but the first versions still had issues. Compiling, testing, and reviewing the actual files were necessary to find them.

### Initial Architecture Review

Before implementing the calendar logic, I used ChatGPT to help design the `Event`, `Recurrence`, and `EventStore` interfaces.

The main decisions were to keep event data separate from list management, hide linked-list nodes, and make the store responsible for freeing its allocated memory. Retrieval would copy event values instead of exposing internal pointers.

The review also identified cases to handle during implementation:

- Blank or oversized titles.
- End times that are not after start times.
- Invalid recurrence values.
- Daylight-saving changes.
- Failed memory allocations.
- Event ID exhaustion.
- Updates accidentally conflicting with their own original records.

### Problems Found During Development and Review

One issue appeared when replacing the update function's conflict logic. The old loop remained alongside the new code, causing a duplicate variable declaration. The compiler caught this, and replacing the complete function resolved it.

A later source review found that the network line reader rejected a valid 99-character title or location. It filled its buffer before reading the final newline. The fix was to check for the newline before rejecting a full text buffer.

I tested this by sending a 99-character title, which was accepted, and a 100-character title, which was rejected. The server continued responding afterward.

The review also found a mismatch between storage and loading. The loader accepted at most 10,000 records, but additions did not enforce that limit. This could let the program save a database it could not reopen. A shared `MAX_EVENTS` limit and `STORE_FULL` result fixed the mismatch.

Other corrections included moving the `event_is_valid` declaration into the event header and updating menu error messages after adding option 5.

These issues were missed during the earlier AI-assisted implementation and initial tests. They showed why passing the first tests was not enough.

### Testing and Refactoring Results

The tests covered empty-store behavior, event validation, additions, retrieval, deletion, updates, overlap rejection, and back-to-back appointments. They also checked that rejected updates preserved the original event.

Recurrence checks covered daily and weekly events, conflicts with later occurrences, month boundaries, leap years, and daylight-saving changes.

Persistence checks compared saved and loaded records, including text, IDs, times, and recurrence. They also checked the next available ID and rejection of a truncated database without changing the caller's store pointer.

A separate capacity test used a three-event limit to check filling the store, rejecting another event, saving and loading a full store, and adding again after deletion. This tested the boundary logic without needing to create 10,000 records.

Manual checks demonstrated persistent CRUD across restarts, application locking, successful TCP sharing, duplicate conflict rejection, and handling an unavailable server without changing the local event.

### Sanitizer and Invalid-Input Checks

I rebuilt the application, server, and tests with AddressSanitizer and UndefinedBehaviorSanitizer. Both test programs passed without sanitizer reports.

During this process, the capacity test executable was initially reused because the Makefile's clean rule did not remove it. The clean rule was corrected, and the capacity test was forced to rebuild with sanitizers before accepting that result.

I also sent invalid requests to the sanitizer-enabled server:

- An invalid calendar date.
- An occurrence count above 365.
- A blank title.

Each request returned `INVALID`. The server then responded to `PING` and shut down through `STOP` without sanitizer reports.

These results apply to the paths that were tested. They do not prove that every possible error has been handled or that the program is completely free of memory leaks.

## Demo Video Link

[Watch my capstone demonstration on Kaltura MediaSpace](https://mediaspace.umn.edu/media/t/1_f3k5u133)
