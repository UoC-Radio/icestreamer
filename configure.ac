#Prelude
AC_INIT([icestreamer],[0.5],[radio-list@culture.uoc.gr])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign -Wall -Werror dist-bzip2])

# Check for programs
AC_PROG_CC

# Check for libraries
PKG_CHECK_MODULES(GStreamer,
		   [
			gstreamer-1.0 >= 1.0.0
			gstreamer-base-1.0 >= 1.0.0
        	   ],
		   [
			AC_SUBST(GStreamer_CFLAGS)
			AC_SUBST(GStreamer_LIBS)
        	   ],
		   [
			AC_MSG_ERROR([Could not find GStreamer 1.0 libraries])
		   ])

PKG_CHECK_MODULES(GLib,
		   [
			gio-2.0
		   ],
		   [
			AC_SUBST(GLib_CFLAGS)
                        AC_SUBST(GLib_LIBS)
		   ],
		   [
			AC_MSG_ERROR([Could not find GLib 2.0 libraries])
		   ])

# Output files
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

