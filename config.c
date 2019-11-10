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

gchar *
icstr_keyfile_get_string_with_fallback (GKeyFile *keyfile,
    const gchar *group, const gchar *key, const gchar *fallback)
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

gboolean
icstr_object_set_properties_from_keyfile (gpointer object,
    GKeyFile *keyfile, const gchar *group, GError **error)
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

    value =
        g_key_file_get_value (keyfile, group, g_param_spec_get_name (param),
        &e);

    if (e && e->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND)
      continue;

    if (e) {
      g_propagate_error (error, g_steal_pointer (&e));
      return FALSE;
    }

    GST_LOG ("Setting property %s on object %s to the value '%s'",
             g_param_spec_get_name (param), GST_OBJECT_NAME (object), value);

    gst_util_set_object_arg (G_OBJECT (object), g_param_spec_get_name (param),
                             value);
  }

  return TRUE;
}

GstElement *
icstr_element_factory_make_with_group_name (const gchar *factory,
    const gchar *group)
{
  g_autofree gchar *name;
  GstElement *element;

  name = g_strdup_printf ("%s-%s", factory, group);
  element = gst_element_factory_make (factory, name);

  /* clear floating reference for use with g_autoptr */
  return gst_object_ref_sink (element);
}
