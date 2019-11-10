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
icstr_construct_source (IceStreamer * self, GKeyFile * keyfile, GError ** error)
{
  g_autoptr (GstElement) element = NULL;
  g_autofree gchar *value = NULL;
  const gchar *element_factory = NULL;
  g_autoptr (GError) internal_error = NULL;

  /* find out which element to construct and construct it */
  value =
      icstr_keyfile_get_string_with_fallback (keyfile, "input", "source",
      "auto");
  if (g_str_equal (value, "auto"))
    element_factory = "autoaudiosrc";
  else if (g_str_equal (value, "jack"))
    element_factory = "jackaudiosrc";
  else if (g_str_equal (value, "alsa"))
    element_factory = "alsasrc";
  else if (g_str_equal (value, "pulse"))
    element_factory = "pulsesrc";
  else if (g_str_equal (value, "test"))
    element_factory = "audiotestsrc";

  GST_DEBUG ("Attempting to construct source element %s for input source %s",
      element_factory, value);

  if (!element_factory
      || !(element = gst_element_factory_make (element_factory, NULL))) {
    g_set_error (error, 0, 0,
        "Failed to construct source element (source = %s, factory = %s)\n",
        value, element_factory);
    return NULL;
  }

  /* claim ownership */
  gst_object_ref_sink (element);

  /* set its properties */
  if (!icstr_object_set_properties_from_keyfile (element, keyfile, "input",
          &internal_error)) {
    /* group not found is ok - for anything else, bail out */
    if (internal_error->code != G_KEY_FILE_ERROR_GROUP_NOT_FOUND) {
      g_propagate_prefixed_error (error, g_steal_pointer (&internal_error),
          "Failed to read input properties:");
      return NULL;
    }
  }

  /* force audiotestsrc to behave like a live source */
  if (g_str_equal (element_factory, "audiotestsrc"))
    g_object_set (element, "is-live", TRUE, NULL);

  /* make sure it works */
  if (gst_element_set_state (element,
          GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS) {
    g_set_error (error, 0, 0, "Failed to activate input element");
    return NULL;
  }

  /* bring back to NULL state, for the case where we have to dispose before going to PLAYING */
  gst_element_set_state (element, GST_STATE_NULL);

  return g_steal_pointer (&element);
}
