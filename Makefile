#Makefile for HW5
CFLAGS = -Wall -Werror
BINS = clean server client chat

all: $(BINS)

server:
	gcc $(CFLAGS) server.c -pthread -lssl -lcrypto -g -o $@

client:
	gcc $(CFLAGS) client.c -g -o $@

chat:
	gcc $(CFLAGS) chat.c -g -o $@

clean:
	rm -f $(BINS)

clean-s:
	rm -f server

clean-c:
	rm -f client

clean-chat:
	rm -f chat
