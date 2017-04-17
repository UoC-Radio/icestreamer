CC=gcc
CFLAGS=-Wall `pkg-config --cflags --libs gstreamer-1.0`

debug:clean
	$(CC) $(CFLAGS) -g -o icestreamer main.c
stable:clean
	$(CC) $(CFLAGS) -o icestreamer main.c
clean:
	rm -vfr *~ icestreamer
