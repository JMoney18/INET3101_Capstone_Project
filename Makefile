CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g

COMMON_OBJECTS = event_store.o event.o

.PHONY: all clean test ping stop

all: calendar calendar_server

calendar: main.o network.o $(COMMON_OBJECTS)
	$(CC) $(CFLAGS) -o $@ main.o network.o $(COMMON_OBJECTS)

test_calendar: test_calendar.o $(COMMON_OBJECTS)
	$(CC) $(CFLAGS) -o $@ test_calendar.o $(COMMON_OBJECTS)

test: test_calendar test_capacity
	TZ=America/Chicago ./test_calendar
	TZ=America/Chicago ./test_capacity

main.o: main.c network.h event_store.h event.h
	$(CC) $(CFLAGS) -c main.c

test_calendar.o: test_calendar.c event_store.h event.h
	$(CC) $(CFLAGS) -c test_calendar.c

event_store.o: event_store.c event_store.h event.h
	$(CC) $(CFLAGS) -c event_store.c

event.o: event.c event.h
	$(CC) $(CFLAGS) -c event.c

calendar_server: server_main.o network.o $(COMMON_OBJECTS)
	$(CC) $(CFLAGS) -o $@ server_main.o network.o $(COMMON_OBJECTS)

server_main.o: server_main.c network.h event_store.h event.h
	$(CC) $(CFLAGS) -c server_main.c

network.o: network.c network.h event_store.h event.h
	$(CC) $(CFLAGS) -c network.c

test_capacity: test_capacity.c event_store.c event.c event_store.h event.h
	$(CC) $(CFLAGS) -DMAX_EVENTS=3 -o $@ test_capacity.c event_store.c event.c

clean:
	rm -f calendar calendar_server test_calendar test_capacity main.o server_main.o network.o test_calendar.o $(COMMON_OBJECTS)

ping:
	printf 'PING\n' | nc -w 5 127.0.0.1 5050

stop:
	printf 'STOP\n' | nc -w 5 127.0.0.1 5050