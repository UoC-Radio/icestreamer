bin_PROGRAMS = icestreamer

icestreamer_SOURCES = main.c
icestreamer_LDADD = $(GStreamer_LIBS) $(GLib_LIBS)
icestreamer_CFLAGS = ${CFLAGS} ${GStreamer_CFLAGS} $(GLib_CFLAGS)

#Also clean up after autoconf
distclean-local:
	-rm -rf autom4te.cache
	-rm -rf build-aux
	-rm aclocal.m4
	-rm configure
	-rm *.in
	-rm *~

#Do nothing
test:
