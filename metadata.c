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
icstr_update_metadata_callback (GFileMonitor *monitor,
    GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer data)
{
  IceStreamer *self = data;
  g_autoptr (GError) error = NULL;
  GBytes *file_bytes = NULL;
  g_autofree gchar *file_contents = NULL;
  gchar *sanitized_contents = NULL;
  g_auto(GStrv) metadata = NULL;
  g_autofree gchar *artist = NULL;
  g_autofree gchar *title = NULL;
  GstEvent *tag_event = NULL;
  gsize file_len = 0;

  if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
      event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
    GST_WARNING ("Something weird happened to the metadata file (%u),",
             event_type);
    GST_WARNING ("  disabling metadata monitor.");
    g_file_monitor_cancel (monitor);
    return;
  }

  file_bytes = g_file_load_bytes (file, NULL, NULL, &error);
  if (error) {
    GST_WARNING ("Couldn't read metadata file: %s,", error->message);
    GST_WARNING ("  disabling metadata monitor.");
    g_file_monitor_cancel (monitor);
    return;
  }

  file_contents = (gchar *) g_bytes_unref_to_data (file_bytes, &file_len);

  if (!strnlen (file_contents, file_len)) {
    GST_WARNING ("Got malformed metadata");
    return;
  }

  if (!g_utf8_validate_len (file_contents, file_len, NULL)) {
    GST_WARNING ("Got malformed metadata");
    return;
  }

  sanitized_contents = g_strstrip (file_contents);

  if (!g_strrstr (sanitized_contents, "\n")) {
    GST_WARNING ("Got malformed metadata");
    return;
  }

  metadata = g_strsplit (sanitized_contents, "\n", 3);
  if (g_strv_length (metadata) != 2) {
    GST_WARNING ("Got malformed metadata");
    return;
  }

  artist = g_str_to_ascii (metadata[0], NULL);
  title = g_str_to_ascii (metadata[1], NULL);

  GST_DEBUG ("Got metadata: a: %s t: %s", artist, title);

  if (!self->tags)
    self->tags = gst_tag_list_new_empty ();

  gst_tag_list_add (self->tags, GST_TAG_MERGE_REPLACE, GST_TAG_ARTIST, artist,
                    NULL);
  gst_tag_list_add (self->tags, GST_TAG_MERGE_REPLACE, GST_TAG_TITLE, title,
                    NULL);
  gst_tag_list_set_scope (self->tags, GST_TAG_SCOPE_GLOBAL);

  tag_event = gst_event_new_tag (self->tags);
  if (!gst_element_send_event (self->pipeline, tag_event))
    GST_WARNING ("Failed to send tag event");

  return;
}

gboolean
icstr_setup_metadata_handler (IceStreamer * self, GKeyFile * keyfile,
    GError ** error)
{
  g_autofree gchar *filename = NULL;
  g_autoptr (GError) internal_error = NULL;
  g_autoptr (GFile) mtdat_file = NULL;
  g_autoptr (GFileMonitor) mtdat_file_monitor = NULL;
  guint ret = 0;

  filename =
      g_key_file_get_string (keyfile, "metadata", "file", &internal_error);
  if (!filename) {
    g_propagate_prefixed_error (error, internal_error,
        "No metadata file provided:");
    return FALSE;
  }

  mtdat_file = g_file_new_for_path (filename);
  if (!g_file_query_exists (mtdat_file, NULL)) {
    g_set_error (error, ICSTR_ERROR, 0, "Provided metadata file doesn't exist");
    return FALSE;
  }

  mtdat_file_monitor = g_file_monitor_file (mtdat_file, G_FILE_MONITOR_NONE,
                                            NULL, &internal_error);
  if (!mtdat_file_monitor) {
    g_propagate_prefixed_error (error, internal_error,
        "Could not initialize metadata file monitor:");
    return FALSE;
  }

  ret = g_signal_connect (mtdat_file_monitor, "changed",
                          G_CALLBACK (icstr_update_metadata_callback), self);
  if (ret <= 0) {
    g_set_error (error, ICSTR_ERROR, 0,
        "Could not connect to metadata file monitor");
    return FALSE;
  }

  self->mtdat_file = g_steal_pointer (&mtdat_file);
  self->mtdat_file_monitor = g_steal_pointer (&mtdat_file_monitor);

  /* force an update */
  icstr_update_metadata_callback (self->mtdat_file_monitor, self->mtdat_file,
                                  NULL, G_FILE_MONITOR_EVENT_CHANGED, self);

  return TRUE;
}
