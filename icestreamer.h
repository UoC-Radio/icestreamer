/*
 * IceStreamer - A simple live audio streamer
 *
 * Copyright (C) 2017 George Kiagiadakis <gkiagia@tolabaki.gr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <glib-unix.h>
#include <gst/gst.h>

/* ammount of seconds to wait before attempting to reconnect a stream */
#define RECONNECT_TIMEOUT 5

typedef struct _IceStreamer IceStreamer;
struct _IceStreamer
{
  GstElement *pipeline;
  GstElement *tee;    /* owned by the pipeline */
  GMainLoop  *loop;   /* weak pointer, not owned by us */
  GList      *streams;
  GList      *disconnected_streams;
  guint      timeout_source;
};

