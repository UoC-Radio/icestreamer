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
#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <gst/gst.h>

/* ammount of seconds to wait before attempting to reconnect a stream */
#define RECONNECT_TIMEOUT 5

GST_DEBUG_CATEGORY_STATIC (icestreamer_debug);
#define GST_CAT_DEFAULT icestreamer_debug

typedef struct _IceStreamer IceStreamer;
struct _IceStreamer
{
  GstElement *pipeline;
  GstElement *tee;    /* owned by the pipeline */
  GMainLoop  *loop;   /* weak pointer, not owned by us */
  GList      *discncted_streams;
  guint      timeout_source;
};

static void
ice_streamer_free (IceStreamer *streamer)
{
  g_clear_object (&streamer->pipeline);
  g_free (streamer);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (IceStreamer, ice_streamer_free);


static gchar *
keyfile_get_string_with_fallback (GKeyFile *keyfile,
    const gchar *group,
    const gchar *key,
    const gchar *fallback)
{
  GError *error = NULL;
  gchar *value = NULL;

  value = g_key_file_get_string (keyfile, group, key, &error);
  if (error) {
    value = g_strdup (fallback);
    g_clear_error (&error);
  }

  return value;
}

static gboolean
object_set_properties_from_keyfile (gpointer object,
    GKeyFile *keyfile,
    const gchar *group,
    GError **error)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (object);
  g_autofree GParamSpec **params;
  guint n_properties = 0, i;

  params = g_object_class_list_properties (klass, &n_properties);
  for (i = 0; i < n_properties; i++) {
    GParamSpec *param = params[i];
    g_autofree gchar *value = NULL;
    g_autoptr (GError) e = NULL;

    if (!(param->flags & G_PARAM_WRITABLE) ||
        (param->flags & G_PARAM_CONSTRUCT_ONLY))
      continue;

    value = g_key_file_get_value (keyfile, group, g_param_spec_get_name (param), &e);

    if (e && e->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND)
      continue;

    if (e) {
      g_propagate_error (error, g_steal_pointer (&e));
      return FALSE;
    }

    GST_LOG ("Setting property %s on object %s to the value '%s'", g_param_spec_get_name (param),
      GST_OBJECT_NAME (object), value);

    gst_util_set_object_arg (G_OBJECT (object), g_param_spec_get_name (param), value);
  }

  return TRUE;
}

static GstElement *
element_factory_make_with_group_name (const gchar *factory,
    const gchar *group)
{
  g_autofree gchar *name;
  GstElement *element;

  name = g_strdup_printf ("%s-%s", factory, group);
  element = gst_element_factory_make (factory, name);

  /* clear floating reference for use with g_autoptr */
  return gst_object_ref_sink (element);
}

static GstElement *
construct_source (IceStreamer *self,
    GKeyFile *keyfile,
    GError **error)
{
  g_autoptr (GstElement) element = NULL;
  g_autofree gchar *value = NULL;
  const gchar *element_factory = NULL;
  g_autoptr (GError) internal_error = NULL;

  /* find out which element to construct and construct it */
  value = keyfile_get_string_with_fallback (keyfile, "input", "system", "auto");
  if (g_str_equal (value, "auto"))
    element_factory = "autoaudiosrc";
  else if (g_str_equal (value, "jack"))
    element_factory = "jackaudiosrc";
  else if (g_str_equal (value, "alsa"))
    element_factory = "alsasrc";
  else if (g_str_equal (value, "pulse"))
    element_factory = "pulsesrc";

  GST_DEBUG ("Attempting to construct source element %s for input system %s", element_factory,
      value);

  if (!element_factory || !(element = gst_element_factory_make (element_factory, NULL))) {
    g_set_error (error, 0, 0, "Failed to construct source element (system = %s, factory = %s)\n",
        value, element_factory);
    return NULL;
  }

  /* claim ownership */
  gst_object_ref_sink (element);

  /* set its properties */
  if (!object_set_properties_from_keyfile (element, keyfile, "input", &internal_error)) {
    /* group not found is ok - for anything else, bail out */
    if (internal_error->code != G_KEY_FILE_ERROR_GROUP_NOT_FOUND) {
      g_propagate_prefixed_error (error, g_steal_pointer (&internal_error),
          "Failed to read input properties:");
      return NULL;
    }
  }

  /* make sure it works */
  if (gst_element_set_state (element, GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS) {
    g_set_error (error, 0, 0, "Failed to activate input element");
    return NULL;
  }

  /* bring back to NULL state, for the case where we have to dispose before going to PLAYING */
  gst_element_set_state (element, GST_STATE_NULL);

  return g_steal_pointer (&element);
}

static GstElement *
construct_stream (IceStreamer *self,
    GKeyFile *keyfile,
    const gchar *group,
    GError **error)
{
  g_autoptr (GstElement) bin = NULL;
  g_autoptr (GstElement) queue = NULL;
  g_autoptr (GstElement) convert = NULL;
  g_autoptr (GstElement) resample = NULL;
  g_autoptr (GstElement) encoder = NULL;
  g_autoptr (GstElement) mux = NULL;
  g_autoptr (GstElement) shout2send = NULL;
  g_autoptr (GError) internal_error = NULL;
  g_autofree gchar *value = NULL;
  const gchar *encoder_factory = NULL;
  const gchar *mux_factory = NULL;
  gboolean mux_required = TRUE;

  /* find out which encoder & mux to construct */
  value = keyfile_get_string_with_fallback (keyfile, group, "encoder", "vorbis");
  if (g_str_equal (value, "vorbis")) {
    encoder_factory = "vorbisenc";
  } else if (g_str_equal (value, "opus")) {
    encoder_factory = "opusenc";
  } else if (g_str_equal (value, "mp3")) {
    encoder_factory = "lamemp3enc";
    mux_required = FALSE;
  }

  if (mux_required) {
    g_free (value);
    value = keyfile_get_string_with_fallback (keyfile, group, "container", "ogg");
    if (g_str_equal (value, "ogg")) {
      mux_factory = "oggmux";
    } else if (g_str_equal (value, "webm")) {
      mux_factory = "webmmux";
    }
  }

  GST_DEBUG ("Attempting to construct encoder element %s for stream %s", encoder_factory, group);

  /* construct encoder */
  if (!encoder_factory ||
      !(encoder = element_factory_make_with_group_name (encoder_factory, group))) {
    g_set_error (error, 0, 0, "Failed to construct encoder element (%s) for stream '%s'\n",
        encoder_factory, group);
    return NULL;
  }

  /* set encoder properties */
  if (!object_set_properties_from_keyfile (encoder, keyfile, group, &internal_error)) {
    g_propagate_prefixed_error (error, g_steal_pointer (&internal_error),
        "Failed to read shout2send properties for stream '%s':", group);
    return NULL;
  }

  if (mux_required) {
    GST_DEBUG ("Attempting to construct mux element %s for stream %s", mux_factory, group);

    /* construct mux */
    if (!mux_factory ||
        !(mux = element_factory_make_with_group_name (mux_factory, group))) {
      g_set_error (error, 0, 0, "Failed to construct mux element (%s) for stream '%s'\n",
          mux_factory, group);
      return NULL;
    }
  }

  if (mux && g_str_equal (mux_factory, "webmmux"))
    g_object_set (mux, "streamable", TRUE, NULL);

  /* construct shout2send */
  if (!(shout2send = element_factory_make_with_group_name ("shout2send", group))) {
    g_set_error (error, 0, 0, "Failed to construct shout2send element "
        "- verify your GStreamer installation\n");
    return NULL;
  }
  g_object_set (shout2send, "streamname", group, NULL);

  /* set its properties */
  if (!object_set_properties_from_keyfile (shout2send, keyfile, group, &internal_error)) {
    g_propagate_prefixed_error (error, g_steal_pointer (&internal_error),
        "Failed to read shout2send properties for stream '%s':", group);
    return NULL;
  }

  /* construct the rest of the pipeline for this stream */
  bin = element_factory_make_with_group_name ("bin", group);
  queue = element_factory_make_with_group_name ("queue", group);
  convert = element_factory_make_with_group_name ("audioconvert", group);
  resample = element_factory_make_with_group_name ("audioresample", group);

  /* allow the bin to go to PLAYING independently of the pipeline or other bins */
  g_object_set (bin, "async-handling", TRUE, NULL);

  /* allow dropping old buffers if transmission is taking too long */
  g_object_set (queue, "leaky", 2, NULL);

  gst_bin_add_many (GST_BIN (bin), queue, convert, resample, encoder, shout2send, NULL);
  if (mux)
    gst_bin_add (GST_BIN (bin), mux);

  {
    gboolean link_res;

    if (mux)
      link_res = gst_element_link_many (queue, convert, resample, encoder, mux, shout2send, NULL);
    else
      link_res = gst_element_link_many (queue, convert, resample, encoder, shout2send, NULL);

    if (!link_res) {
      g_set_error (error, 0, 0, "Failed to link pipeline for stream '%s'\n", group);
      return NULL;
    }
  }

  {
    g_autoptr (GstPad) target = gst_element_get_static_pad (queue, "sink");
    gst_element_add_pad (bin, gst_ghost_pad_new ("sink", target));
  }

  return g_steal_pointer (&bin);
}

static gboolean
icestreamer_load (IceStreamer *self, const gchar *conf_file)
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
    g_print ("Failed to load configuration file '%s': %s\n", conf_file, error->message);
    return FALSE;
  }

  source = construct_source (self, keyfile, &error);
  if (!source) {
    g_print ("%s\n", error->message);
    return FALSE;
  }

  self->pipeline = gst_pipeline_new (NULL);
  self->tee = gst_element_factory_make ("tee", NULL);
  g_object_set (self->tee, "allow-not-linked", TRUE, NULL);

  gst_bin_add_many (GST_BIN (self->pipeline), source, self->tee, NULL);

  if (!gst_element_link (source, self->tee)) {
    g_print ("Failed to link source with tee\n");
    return FALSE;
  }

  /* parse all remaining groups as streams */

  groups = g_key_file_get_groups (keyfile, NULL);
  for (group = groups; *group; group++) {
    GstElement *stream;

    /* skip the input group, this is parsed by construct_source() */
    if (g_str_equal (*group, "input"))
      continue;

    GST_DEBUG ("Constructing stream '%s'", *group);

    stream = construct_stream (self, keyfile, *group, &error);

    if (error) {
      g_print ("Failed to construct stream: %s\n", error->message);
      g_clear_error (&error);
      continue;
    }

    gst_bin_add (GST_BIN (self->pipeline), stream);
    gst_element_link_pads (self->tee, "src_%u", stream, "sink");
    streams_linked++;
  }

  g_strfreev (groups);

  if (streams_linked == 0) {
    g_print ("No streams specified in the configuration file\n");
    return FALSE;
  }

  return TRUE;
}

static gboolean
reconnect_timeout_callback (gpointer data)
{
  IceStreamer *self = data;
  GList *l;

  for (l = self->discncted_streams; l != NULL; l = g_list_next (l)) {
    GstElement *stream = l->data;
    GST_INFO ("Reconnecting %s", GST_OBJECT_NAME (stream));
    gst_element_set_state (stream, GST_STATE_PLAYING);
    gst_element_link_pads (self->tee, "src_%u", stream, "sink");
  }

  self->timeout_source = 0;
  return G_SOURCE_REMOVE;
}

static gboolean
bus_callback (GstBus *bus, GstMessage *msg, gpointer data)
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

      stream_bin = GST_ELEMENT (gst_object_get_parent (GST_MESSAGE_SRC (msg)));
      bin_sinkpad = gst_element_get_static_pad (stream_bin, "sink");
      tee_srcpad = gst_pad_get_peer (bin_sinkpad);
      gst_pad_unlink (tee_srcpad, bin_sinkpad);
      gst_element_release_request_pad (self->tee, tee_srcpad);
      gst_element_set_state (stream_bin, GST_STATE_NULL);
      self->discncted_streams = g_list_prepend (self->discncted_streams, stream_bin);

      if (self->timeout_source == 0) {
        GST_INFO ("Starting reconnection timer");
        self->timeout_source = g_timeout_add_seconds (RECONNECT_TIMEOUT,
            reconnect_timeout_callback, self);
      }
    } else {
      /*
       * Any other error is fatal - report & exit
       */
      g_print ("GStreamer reported a fatal error: %s (%s)\n", error->message, debug);
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
exit_handler (gpointer loop)
{
  g_main_loop_quit ((GMainLoop *) loop);
  return G_SOURCE_REMOVE;
}

static void
icestreamer_run (IceStreamer *self)
{
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GstBus) bus = NULL;

  self->loop = loop;

  g_unix_signal_add (SIGINT, exit_handler, loop);
  g_unix_signal_add (SIGHUP, exit_handler, loop);
  g_unix_signal_add (SIGTERM, exit_handler, loop);

  bus = gst_pipeline_get_bus (GST_PIPELINE (self->pipeline));
  gst_bus_add_watch (bus, bus_callback, self);

  gst_element_set_state (self->pipeline, GST_STATE_PLAYING);

  GST_INFO ("Entering main loop");
  g_main_loop_run (loop);
  GST_INFO ("Exiting...");

  gst_element_set_state (self->pipeline, GST_STATE_NULL);
  self->loop = NULL;
}

gint
main (gint argc, gchar **argv)
{
  g_autoptr (GOptionContext) context;
  g_autoptr (IceStreamer) self = NULL;
  g_autoptr (GError) error = NULL;

  gchar *conf_file = "icestreamer.conf";
  const GOptionEntry entries[] =
  {
    { "config", 'c', 0, G_OPTION_ARG_FILENAME, &conf_file,
      "Configuration file", "icestreamer.conf" },
    { NULL }
  };

  GST_DEBUG_CATEGORY_INIT (icestreamer_debug, "icestreamer", 0, "IceStreamer");

  /* cmd line option parsing */

  context = g_option_context_new (NULL);
  g_option_context_set_summary (context, "stream audio to an icecast server");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());

  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_print ("option parsing failed: %s\n", error->message);
    return 1;
  }

  g_clear_pointer (&context, g_option_context_free);

  /* initialization */
  self = g_new0 (IceStreamer, 1);
  if (!icestreamer_load (self, conf_file))
    return 1;

  /* enter main loop */
  icestreamer_run (self);

  return 0;
}
