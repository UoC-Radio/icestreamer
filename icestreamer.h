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
#include <gio/gio.h>

/* ammount of seconds to wait before attempting to reconnect a stream */
#define RECONNECT_TIMEOUT 5

typedef struct _IceStreamer IceStreamer;
struct _IceStreamer
{
  GstElement *pipeline;
  GstElement *tee;              /* owned by the pipeline */
  GMainLoop *loop;              /* weak pointer, not owned by us */
  GList *streams;
  GList *disconnected_streams;
  guint timeout_source;
  GFile *mtdat_file;
  GFileMonitor *mtdat_file_monitor;
  GstTagList *tags;
};

/* Prototypes */

/* config.c */
gchar *icstr_keyfile_get_string_with_fallback (GKeyFile * keyfile,
    const gchar * group, const gchar * key, const gchar * fallback);
gboolean
icstr_object_set_properties_from_keyfile (gpointer object,
    GKeyFile * keyfile, const gchar * group, GError ** error);
GstElement *icstr_element_factory_make_with_group_name (const gchar * factory,
    const gchar * group);

/* source.c */
GstElement *icstr_construct_source (IceStreamer * self,
    GKeyFile * keyfile, GError ** error);

/* stream.c */
GstElement *icstr_construct_stream (IceStreamer * self,
    GKeyFile * keyfile, const gchar * group, GError ** error);

/* metadata.c */
gboolean
icstr_setup_metadata_handler (IceStreamer * self, GKeyFile * keyfile,
    GError ** error);
