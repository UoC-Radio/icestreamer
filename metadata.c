/*
 * IceStreamer - A simple live audio streamer
 *
 * Copyright (C) 2017 George Kiagiadakis <gkiagia@tolabaki.gr>
 * Copyright (C) 2019 Nick Kossifidis <mickflemm@gmail.com>
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
#include "icestreamer.h"

static void
icstr_update_metadata_callback (GFileMonitor * monitor,
    GFile * file,
    GFile * other_file, GFileMonitorEvent event_type, gpointer data)
{
  IceStreamer *self = data;
  GBytes *file_bytes = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar **metadata = NULL;
  g_autofree gchar *artist = NULL;
  g_autofree gchar *title = NULL;
  g_autofree gchar *file_contents = NULL;
  GstEvent *tag_event = NULL;
  GList *curr = NULL;
  gsize file_len = 0;

  if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
      event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
    g_error ("Something weird happened to the metadata file (%u),\n",
        event_type);
    g_error ("disabling metadata monitor.\n");
    g_file_monitor_cancel (monitor);
    g_object_unref (monitor);
    g_object_unref (file);
    return;
  }

  file_bytes = g_file_load_bytes (file, NULL, NULL, &error);
  if (error) {
    g_error ("Couldn't read metadata file: %s,\n", error->message);
    g_error ("disabling metadata monitor.\n");
    g_file_monitor_cancel (monitor);
    g_object_unref (monitor);
    g_object_unref (file);
    return;
  }

  file_contents = (gchar *) g_bytes_unref_to_data (file_bytes, &file_len);

  if (!strnlen (file_contents, file_len)) {
    g_warning ("Got malformed metadata\n");
    return;
  }

  if (!g_utf8_validate_len (file_contents, file_len, NULL)) {
    g_warning ("Got malformed metadata\n");
    return;
  }

  file_contents = g_strstrip (file_contents);

  if (!g_strrstr (file_contents, "\n")) {
    g_warning ("Got malformed metadata\n");
    return;
  }

  metadata = g_strsplit (file_contents, "\n", 3);
  if (g_strv_length (metadata) != 2) {
    g_warning ("Got malformed metadata\n");
    return;
  }

  artist = g_str_to_ascii (metadata[0], NULL);
  title = g_str_to_ascii (metadata[1], NULL);

  GST_DEBUG ("Got metadata: a: %s t: %s\n", artist, title);

  if (!self->tags)
    self->tags = gst_tag_list_new_empty ();

  gst_tag_list_add (self->tags, GST_TAG_MERGE_REPLACE, GST_TAG_ARTIST, artist,
      NULL);
  gst_tag_list_add (self->tags, GST_TAG_MERGE_REPLACE, GST_TAG_TITLE, title,
      NULL);
  gst_tag_list_set_scope (self->tags, GST_TAG_SCOPE_GLOBAL);

  tag_event = gst_event_new_tag (self->tags);
  if (!gst_element_send_event (self->pipeline, tag_event))
    g_warning ("Failed to send tag event\n");

  return;
}

gboolean
icstr_setup_metadata_handler (IceStreamer * self, GKeyFile * keyfile,
    GError ** error)
{
  g_autofree gchar *filename = NULL;
  GCancellable *cancellable = NULL;
  g_autoptr (GError) internal_error = NULL;
  GFile *mtdat_file = NULL;
  GFileMonitor *mtdat_file_monitor = NULL;
  guint ret = 0;

  filename =
      g_key_file_get_string (keyfile, "metadata", "file", &internal_error);
  if (internal_error) {
    g_set_error (error, 0, 0, "No metadata file provided: %s\n",
        internal_error->message);
    return FALSE;
  }

  mtdat_file = g_file_new_for_path (filename);
  if (!g_file_query_exists (mtdat_file, NULL)) {
    g_set_error (error, 0, 0, "Provided metadata file doesn't exist\n");
    return FALSE;
  }

  cancellable = g_cancellable_new ();
  mtdat_file_monitor = g_file_monitor_file (mtdat_file, G_FILE_MONITOR_NONE,
      cancellable, &internal_error);
  if (internal_error) {
    g_set_error (error, 0, 0,
        "Could not initialize metadata file monitor: %s\n",
        internal_error->message);
    g_object_unref (mtdat_file);
    return FALSE;
  }

  ret = g_signal_connect (mtdat_file_monitor, "changed",
      G_CALLBACK (icstr_update_metadata_callback), self);
  if (ret <= 0) {
    g_set_error (error, 0, 0, "Could not connect to metadata file monitor\n");
    g_file_monitor_cancel (mtdat_file_monitor);
    g_object_unref (mtdat_file_monitor);
    g_object_unref (mtdat_file);
    return FALSE;
  }

  self->mtdat_file = mtdat_file;
  self->mtdat_file_monitor = mtdat_file_monitor;

  /* force an update */
  icstr_update_metadata_callback (mtdat_file_monitor, mtdat_file,
      NULL, G_FILE_MONITOR_EVENT_CHANGED, self);

  return TRUE;
}
