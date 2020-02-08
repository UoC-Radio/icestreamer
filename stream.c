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

GstElement *
icstr_construct_stream (IceStreamer *self,
    GKeyFile *keyfile, const gchar *group, GError **error)
{
  g_autoptr (GstElement) bin = NULL;
  g_autoptr (GstElement) queue = NULL;
  g_autoptr (GstElement) convert = NULL;
  g_autoptr (GstElement) resample = NULL;
  g_autoptr (GstElement) encoder = NULL;
  g_autoptr (GstElement) mux = NULL;
  g_autoptr (GstElement) shout2send = NULL;
  g_autoptr (GError) internal_error = NULL;
  g_autoptr (GstPad) target = NULL;
  g_autofree gchar *value = NULL;
  const gchar *encoder_factory = NULL;
  const gchar *mux_factory = NULL;
  GstTagSetter *tagsetter = NULL;
  gboolean mux_required = TRUE;
  gboolean link_res = FALSE;

  /* find out which encoder & mux to construct */
  value = icstr_keyfile_get_string_with_fallback (keyfile, group, "encoder",
                                                  "vorbis");
  if (g_str_equal (value, "vorbis")) {
    encoder_factory = "vorbisenc";
  } else if (g_str_equal (value, "opus")) {
    encoder_factory = "opusenc";
  } else if (g_str_equal (value, "mp3")) {
    encoder_factory = "lamemp3enc";
    mux_required = FALSE;
  }

  if (!encoder_factory) {
    g_set_error (error, ICSTR_ERROR, 0, "Unknown encoder: %s", value);
    return NULL;
  }

  if (mux_required) {
    g_free (value);
    value = icstr_keyfile_get_string_with_fallback (keyfile, group, "container",
                                                    "ogg");
    if (g_str_equal (value, "ogg")) {
      mux_factory = "oggmux";
    } else if (g_str_equal (value, "webm")) {
      mux_factory = "webmmux";
    }

    if (!mux_factory) {
      g_set_error (error, ICSTR_ERROR, 0, "Unknown container: %s", value);
      return NULL;
    }

  }

  GST_DEBUG ("Attempting to construct encoder element %s for stream %s",
             encoder_factory, group);

  /* construct encoder */
  encoder = icstr_element_factory_make_with_group_name (encoder_factory,
                                                        group);
  if (!encoder) {
    g_set_error (error, ICSTR_ERROR, 0,
                 "Failed to construct encoder element (%s) for stream '%s'",
                 encoder_factory, group);
    return NULL;
  }

  /* set encoder properties */
  if (!icstr_object_set_properties_from_keyfile (encoder, keyfile, group,
                                                 &internal_error)) {
    g_propagate_prefixed_error (error, g_steal_pointer (&internal_error),
        "Failed to read shout2send properties for stream '%s':", group);
    return NULL;
  }

  if (mux_required) {

    GST_DEBUG ("Attempting to construct mux element %s for stream %s",
               mux_factory, group);

    /* construct mux */
    mux = icstr_element_factory_make_with_group_name (mux_factory, group);
    if (!mux) {
      g_set_error (error, ICSTR_ERROR, 0,
          "Failed to construct mux element (%s) for stream '%s'", mux_factory,
          group);
      return NULL;
    }
  }

  if (mux && g_str_equal (mux_factory, "webmmux"))
    g_object_set (mux, "streamable", TRUE, NULL);

  /* construct shout2send */
  shout2send = icstr_element_factory_make_with_group_name ("shout2send", group);
  if (!shout2send) {
    g_set_error (error, ICSTR_ERROR, 0,
        "Failed to construct shout2send element "
        "- verify your GStreamer installation");
    return NULL;
  }
  g_object_set (shout2send, "streamname", group, NULL);

  /* set its properties */
  if (!icstr_object_set_properties_from_keyfile (shout2send, keyfile, group,
                                                 &internal_error)) {
    g_propagate_prefixed_error (error, g_steal_pointer (&internal_error),
        "Failed to read shout2send properties for stream '%s':", group);
    return NULL;
  }

  /* construct the rest of the pipeline for this stream */
  bin = icstr_element_factory_make_with_group_name ("bin", group);
  queue = icstr_element_factory_make_with_group_name ("queue", group);
  convert = icstr_element_factory_make_with_group_name ("audioconvert", group);
  resample = icstr_element_factory_make_with_group_name ("audioresample", group);

  /* allow the bin to go to PLAYING independently of the pipeline or other bins */
  g_object_set (bin, "async-handling", TRUE, NULL);

  /* allow dropping old buffers if transmission is taking too long */
  g_object_set (queue, "leaky", 2, NULL);

  gst_bin_add_many (GST_BIN (bin), queue, convert, resample, encoder,
                    shout2send, NULL);
  if (mux)
    gst_bin_add (GST_BIN (bin), mux);

  if (mux)
    link_res = gst_element_link_many (queue, convert, resample, encoder, mux,
                                      shout2send, NULL);
  else
    link_res = gst_element_link_many (queue, convert, resample, encoder,
                                      shout2send, NULL);
  if (!link_res) {
    g_set_error (error, ICSTR_ERROR, 0,
        "Failed to link pipeline for stream '%s'", group);
    return NULL;
  }

  target = gst_element_get_static_pad (queue, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink", target));

  tagsetter = GST_TAG_SETTER (shout2send);

  gst_tag_setter_set_tag_merge_mode (tagsetter, GST_TAG_MERGE_REPLACE);

  return g_steal_pointer (&bin);
}
