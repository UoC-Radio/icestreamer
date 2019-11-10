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
#include "icestreamer.h"

GST_DEBUG_CATEGORY_STATIC (icestreamer_debug);
#define GST_CAT_DEFAULT icestreamer_debug

static void
ice_streamer_free (IceStreamer * streamer)
{
  g_clear_object (&streamer->pipeline);
  g_free (streamer);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (IceStreamer, ice_streamer_free);


static gboolean
icstr_load (IceStreamer * self, const gchar * conf_file)
{
  g_autoptr (GKeyFile) keyfile = NULL;
  g_autoptr (GstElement) source = NULL;
  g_autoptr (GError) error = NULL;
  gchar **groups;
  gchar **group;
  guint streams_linked = 0;

  GST_INFO ("Loading IceStreamer using configuration file: %s", conf_file);

  keyfile = g_key_file_new ();
  if (!g_key_file_load_from_file (keyfile, conf_file, G_KEY_FILE_NONE, &error)) {
    g_error ("Failed to load configuration file '%s': %s\n", conf_file,
        error->message);
    return FALSE;
  }

  source = icstr_construct_source (self, keyfile, &error);
  if (!source) {
    g_error ("%s\n", error->message);
    return FALSE;
  }

  self->pipeline = gst_pipeline_new (NULL);
  self->tee = gst_element_factory_make ("tee", NULL);
  g_object_set (self->tee, "allow-not-linked", TRUE, NULL);

  gst_bin_add_many (GST_BIN (self->pipeline), source, self->tee, NULL);

  if (!gst_element_link (source, self->tee)) {
    g_error ("Failed to link source with tee\n");
    return FALSE;
  }

  /* parse all remaining groups as streams */

  groups = g_key_file_get_groups (keyfile, NULL);
  for (group = groups; *group; group++) {
    GstElement *stream;

    /* skip the input group, this is parsed by icstr_construct_source() */
    if (g_str_equal (*group, "input"))
      continue;

    /* skip the metadata group, this is parsed by icstr_setup_metadata_handler() */
    if (g_str_equal (*group, "metadata"))
      continue;

    GST_DEBUG ("Constructing stream '%s'", *group);

    stream = icstr_construct_stream (self, keyfile, *group, &error);

    if (error) {
      g_error ("Failed to construct stream: %s\n", error->message);
      g_clear_error (&error);
      continue;
    }

    gst_bin_add (GST_BIN (self->pipeline), stream);
    gst_element_link_pads (self->tee, "src_%u", stream, "sink");
    self->streams = g_list_prepend (self->streams, stream);
    streams_linked++;
  }

  self->streams = g_list_reverse (self->streams);
  g_strfreev (groups);

  if (streams_linked == 0) {
    g_error ("No streams specified in the configuration file\n");
    return FALSE;
  }

  icstr_setup_metadata_handler (self, keyfile, &error);
  if (error)
    g_error ("%s\n", error->message);

  return TRUE;
}

static gboolean
icstr_reconnect_timeout_callback (gpointer data)
{
  IceStreamer *self = data;
  GList *curr = NULL;

  for (curr = self->disconnected_streams; curr != NULL;
      curr = g_list_next (curr)) {
    GstElement *stream = curr->data;
    g_message ("Reconnecting %s", GST_OBJECT_NAME (stream));
    gst_element_set_state (stream, GST_STATE_PLAYING);
    gst_element_link_pads (self->tee, "src_%u", stream, "sink");
  }

  for (curr = self->streams; curr != NULL; curr = g_list_next (curr)) {
    GstElement *stream = curr->data;
    self->disconnected_streams =
        g_list_remove (self->disconnected_streams, curr->data);
  }

  self->timeout_source = 0;
  return G_SOURCE_REMOVE;
}

static gboolean
icstr_bus_callback (GstBus * bus, GstMessage * msg, gpointer data)
{
  IceStreamer *self = data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_WARNING:
    {
      g_autoptr (GError) error = NULL;
      g_autofree gchar *debug = NULL;

      gst_message_parse_warning (msg, &error, &debug);
      GST_WARNING ("GStreamer warning: %s (%s)\n", error->message, debug);

      break;
    }
    case GST_MESSAGE_ERROR:
    {
      g_autoptr (GError) error = NULL;
      g_autofree gchar *debug = NULL;

      gst_message_parse_error (msg, &error, &debug);

      if (g_str_has_prefix (GST_MESSAGE_SRC_NAME (msg), "shout2send")
          && error->domain == GST_RESOURCE_ERROR) {
        /*
         * Network error - disconnect stream bin from the pipeline and reconnect it later
         */
        g_autoptr (GstElement) stream_bin = NULL;
        g_autoptr (GstPad) bin_sinkpad = NULL, tee_srcpad = NULL;

        GST_WARNING ("Encountered a fatal network send error (%s)", debug);
        g_warning ("Network error for %s: %s\n", GST_MESSAGE_SRC_NAME (msg),
            error->message);

        stream_bin =
            GST_ELEMENT (gst_object_get_parent (GST_MESSAGE_SRC (msg)));
        bin_sinkpad = gst_element_get_static_pad (stream_bin, "sink");
        tee_srcpad = gst_pad_get_peer (bin_sinkpad);
        gst_pad_unlink (tee_srcpad, bin_sinkpad);
        gst_element_release_request_pad (self->tee, tee_srcpad);
        gst_element_set_state (stream_bin, GST_STATE_NULL);
        self->disconnected_streams =
            g_list_prepend (self->disconnected_streams, stream_bin);

        if (self->timeout_source == 0) {
          GST_INFO ("Starting reconnection timer");
          self->timeout_source = g_timeout_add_seconds (RECONNECT_TIMEOUT,
              icstr_reconnect_timeout_callback, self);
        }
      } else {
        /*
         * Any other error is fatal - report & exit
         */
        g_error ("GStreamer reported a fatal error: %s (%s)\n", error->message,
            debug);
        g_main_loop_quit (self->loop);
      }

      break;
    }
    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

static gboolean
icstr_exit_handler (gpointer data)
{
  IceStreamer *self = data;
  g_file_monitor_cancel (self->mtdat_file_monitor);
  g_object_unref (self->mtdat_file_monitor);
  g_object_unref (self->mtdat_file);
  gst_tag_list_unref (self->tags);
  g_main_loop_quit (self->loop);
  return G_SOURCE_REMOVE;
}

static void
icstr_run (IceStreamer * self)
{
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GstBus) bus = NULL;

  self->loop = loop;

  g_unix_signal_add (SIGINT, icstr_exit_handler, self);
  g_unix_signal_add (SIGHUP, icstr_exit_handler, self);
  g_unix_signal_add (SIGTERM, icstr_exit_handler, self);

  bus = gst_pipeline_get_bus (GST_PIPELINE (self->pipeline));
  gst_bus_add_watch (bus, icstr_bus_callback, self);

  gst_element_set_state (self->pipeline, GST_STATE_PLAYING);

  GST_INFO ("Entering main loop");
  g_main_loop_run (loop);
  GST_INFO ("Exiting...");

  gst_element_set_state (self->pipeline, GST_STATE_NULL);
  self->loop = NULL;
}

gint
main (gint argc, gchar ** argv)
{
  g_autoptr (GOptionContext) context;
  g_autoptr (IceStreamer) self = NULL;
  g_autoptr (GError) error = NULL;

  gchar *conf_file = "/etc/icestreamer.conf";
  const GOptionEntry entries[] = {
    {"config", 'c', 0, G_OPTION_ARG_FILENAME, &conf_file,
        "Configuration file", "icestreamer.conf"},
    {NULL}
  };

  GST_DEBUG_CATEGORY_INIT (icestreamer_debug, "icestreamer", 0, "IceStreamer");

  /* cmd line option parsing */

  context = g_option_context_new (NULL);
  g_option_context_set_summary (context, "stream audio to an icecast server");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());

  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_error ("Option parsing failed: %s\n", error->message);
    return 1;
  }

  /* verify provided files exist */
  if (!g_file_test (conf_file, G_FILE_TEST_IS_REGULAR)) {
    g_error ("No configuration file provided\n");
    g_print ("\n%s", g_option_context_get_help (context, TRUE, NULL));
    return 1;
  }

  g_clear_pointer (&context, g_option_context_free);

  /* initialization */
  self = g_new0 (IceStreamer, 1);
  if (!icstr_load (self, conf_file))
    return 1;

  /* enter main loop */
  icstr_run (self);

  return 0;
}
