.PHONY: all clean

CFLAGS=-Wall -O2
SOURCES=main.c async_util.c async_util.h net_listener.c\
	net_listener.h service_handler.c service_handler.h constants.h
all: $(SOURCES)
	$(CC) $^ $(CFLAGS) -o server

clean:
	rm ./server
