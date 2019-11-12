/*
 * IceStreamer - A simple live audio streamer
 *
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
#include "math.h"

struct status_widget_map {
	GtkWidget *stream_box;
	GstElement *shout2send;
};

void
icstr_gui_destroy(IceStreamer *self)
{
	gtk_main_quit();
}

static gboolean
icstr_gui_open_infobox(GtkToggleButton *togglebutton, gpointer data)
{
	GstElement *shout2send = data;
	GtkWidget *infobox = NULL;
	GtkWidget *ib_content_area = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *geninfo_frame = NULL;
	GtkWidget *geninfo_label = NULL;
	g_autofree gchar *geninfo_markup = NULL;
	GtkWidget *config_frame = NULL;
	GtkWidget *config_label = NULL;
	g_autofree gchar *config_markup = NULL;
	const gchar *stream_name = NULL;
	const gchar *description = NULL;
	const gchar *genre = NULL;
	gboolean public = FALSE;
	const gchar *url = NULL;
	const gchar *username = NULL;
	const gchar *ip = NULL;
	gint  port = 0;
	gint  protocol = 0;
	const gchar *mount = NULL;
	gboolean is_active = gtk_toggle_button_get_active (togglebutton);

	if (!is_active)
		return TRUE;

	infobox = gtk_dialog_new_with_buttons ("Stream information",
                                       NULL,
                                       GTK_DIALOG_MODAL,
                                       "OK",
                                       GTK_RESPONSE_NONE,
                                       NULL);

	g_object_set (G_OBJECT(infobox), "skip-taskbar-hint",TRUE, NULL);

	g_signal_connect_swapped (infobox, "response",
                          G_CALLBACK (gtk_widget_destroy),
                          infobox);

	g_object_get (G_OBJECT(shout2send), "streamname", &stream_name,
					"genre", &genre,
					"public", &public,
					"url", &url,
					"username", &username,
					"ip", &ip,
					"port", &port,
					"protocol",&protocol,
					"mount", &mount, NULL);

	ib_content_area = gtk_dialog_get_content_area (GTK_DIALOG (infobox));

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
	if(!hbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(ib_content_area), hbox);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
	if(!vbox)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(hbox), vbox, TRUE, TRUE, 3);

	/* General infos */
	geninfo_frame = gtk_frame_new ("General");
	if(!geninfo_frame)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(vbox), geninfo_frame, TRUE, FALSE, 3);

	geninfo_label = gtk_label_new (NULL);
	geninfo_markup = g_markup_printf_escaped ("<b>Stream name:</b>\t\t%s\n"
				"<b>Genre:</b>\t\t\t\t%s\n"
				"<b>Public:</b>\t\t\t\t%s\n"
				"<b>URL:</b>\t\t\t\t%s\n",
				stream_name,
				genre,
				(public) ? "Yes" : "No",
				url);

	gtk_label_set_markup (GTK_LABEL(geninfo_label), geninfo_markup);
	gtk_container_add(GTK_CONTAINER(geninfo_frame), geninfo_label);

	/* Configuration */
	config_frame = gtk_frame_new ("Configuration");
	if(!config_frame)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(vbox), config_frame, TRUE, FALSE, 3);

	config_label = gtk_label_new (NULL);
	config_markup = g_markup_printf_escaped ("<b>Username</b>:\t:%s\n"
						  "<b>IP:</b>\t\t\t\t%s\n"
						  "<b>Port:</b>\t\t\t%u\n"
						  "<b>Protocol:</b>\t\t%s\n"
						  "<b>Mountpoint:</b>\t%s\n",
						  username,
						  ip,
						  port,
						  (protocol == 1) ?
						  "Xaudiocast (icecast 1.3.x)" :
						  (protocol == 2) ?
						  "Icy (ShoutCast)" :
						  "Http (icecast 2.x)",
						  mount);

	gtk_label_set_markup (GTK_LABEL(config_label), config_markup);
	gtk_container_add(GTK_CONTAINER(config_frame), config_label);

	gtk_window_set_resizable (GTK_WINDOW(infobox), FALSE);

	gtk_widget_show_all (infobox);

	gtk_dialog_run (GTK_DIALOG (infobox));
	gtk_toggle_button_set_active (togglebutton, FALSE);

 cleanup:
	return TRUE;
}

static gboolean
icstr_gui_update_stream_status(gpointer data)
{
	struct status_widget_map *wmap = data;
	GstState state = GST_STATE(wmap->shout2send);
	GtkWidget *status_widget = NULL;
	GtkWidget *new_status_widget = NULL;
	g_autoptr (GList) children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER(wmap->stream_box));
	if (!children)
		return FALSE;

	/* status widget is first on the list so it's on the first node */
	status_widget = children->data;
	if (!status_widget)
		return FALSE;

	if (state == GST_STATE_PLAYING &&
	    GTK_IS_SPINNER(status_widget)) {
		new_status_widget =
			gtk_image_new_from_icon_name ("network-transmit",
						      GTK_ICON_SIZE_MENU);
		if (!new_status_widget)
			return FALSE;

		gtk_container_remove (GTK_CONTAINER(wmap->stream_box),
                      		      status_widget);
		gtk_box_pack_start (GTK_BOX(wmap->stream_box), new_status_widget,
					  FALSE, FALSE, 3);
		gtk_box_reorder_child (GTK_BOX(wmap->stream_box), new_status_widget, 0);
		gtk_widget_show (new_status_widget);
	} else if (state != GST_STATE_PLAYING &&
		   GTK_IS_IMAGE(status_widget)) {
		new_status_widget = gtk_spinner_new();
		if (!new_status_widget)
			return FALSE;
		gtk_container_remove (GTK_CONTAINER(wmap->stream_box),
                      		      status_widget);
		gtk_box_pack_start (GTK_BOX(wmap->stream_box), new_status_widget,
					  FALSE, FALSE, 3);
		gtk_box_reorder_child (GTK_BOX(wmap->stream_box), new_status_widget, 0);
		gtk_widget_show (new_status_widget);
	}
	return TRUE;
}

static void
icstr_gui_add_stream(IceStreamer *self, GstElement *stream_bin)
{
	struct icsr_gui *gui = &self->gui;
	static guint counter = 0;
	GtkWidget* separator = NULL;
	GtkWidget* stream_box = NULL;
	GtkWidget* status_widget = NULL;
	GtkWidget* stream_label = NULL;
	GtkWidget* stream_info_button = NULL;
	GtkWidget* info_button_image = NULL;
	g_autofree gchar *bin_name = NULL;
	gchar **bin_name_parts = NULL;
	gchar shout2send_name[32] = {0};
	GstElement* shout2send = NULL;
	const gchar *stream_name_str = NULL;
	struct status_widget_map *wmap = NULL;

	stream_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
	if (!stream_box)
		goto fail;

	if (counter) {
		separator = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
		if (!separator)
			goto fail;
		gtk_box_pack_start (GTK_BOX(gui->streams_box), separator, TRUE, FALSE, 3);
	}

	/* Spinner is displayed unless the stream is active */
	status_widget = gtk_spinner_new();
	if (!status_widget)
		goto fail;
	gtk_box_pack_start (GTK_BOX(stream_box), status_widget, FALSE, FALSE, 3);
	gtk_spinner_start (GTK_SPINNER(status_widget));

	bin_name = gst_object_get_name(GST_OBJECT(stream_bin));
	bin_name_parts = g_strsplit (bin_name,"-", 2);
	g_snprintf (shout2send_name, 32, "shout2send-%s", bin_name_parts[1]);

	shout2send = gst_bin_get_by_name (GST_BIN(stream_bin), shout2send_name);
	if (!shout2send)
		goto fail;

	g_object_get (G_OBJECT(shout2send), "streamname", &stream_name_str, NULL);
	if (!stream_name_str)
		goto fail;

	/* Label to be replaced by the stream's name */
	stream_label = gtk_label_new(stream_name_str);
	if (!stream_label)
		goto fail;
	gtk_box_pack_start (GTK_BOX(stream_box), stream_label, TRUE, TRUE, 3);

	stream_info_button = gtk_toggle_button_new ();
	if (!stream_info_button)
		goto fail;

	info_button_image = gtk_image_new_from_icon_name ("dialog-information",
							  GTK_ICON_SIZE_MENU);
	if (!info_button_image)
		goto fail;
	gtk_button_set_image (GTK_BUTTON(stream_info_button), info_button_image);

	g_signal_connect(stream_info_button, "toggled", G_CALLBACK(icstr_gui_open_infobox), shout2send);
	gtk_box_pack_start (GTK_BOX(stream_box), stream_info_button, FALSE, FALSE, 3);

	wmap = g_malloc(sizeof(struct status_widget_map));
	if (!wmap)
		goto fail;

	wmap->stream_box = stream_box;
	wmap->shout2send = shout2send;
	g_signal_connect(stream_box, "delete-event", G_CALLBACK(g_free), wmap);
	gtk_box_pack_start (GTK_BOX(gui->streams_box), stream_box, TRUE, FALSE, 3);

	g_timeout_add (1000, icstr_gui_update_stream_status, wmap);

	counter++;

	return;
 fail:
	g_error ("Couldn't add stream to gui\n");
	return;
}

static void
icstr_gui_add_streams(IceStreamer *self)
{
	GList *curr = NULL;
	GstElement *stream_bin = NULL;

	for (curr = self->streams; curr != NULL; curr = g_list_next (curr)) {
		stream_bin = curr->data;
		icstr_gui_add_stream(self, stream_bin);
	}

	return;
}

void
icstr_gui_update_time_label(IceStreamer *self, GstClockTime tstamp)
{
	struct icsr_gui *gui = &self->gui;
	g_autofree gchar *tl_markup = NULL;

	tl_markup = g_markup_printf_escaped ("<tt>%"GST_TIME_FORMAT"</tt>",
					     GST_TIME_ARGS(tstamp));

	gtk_label_set_markup (GTK_LABEL(gui->time_label), tl_markup);
	return;
}

void
icstr_gui_update_levels(IceStreamer *self, double rms_l, double rms_r)
{
	struct icsr_gui *gui = &self->gui;
	double rms_l_normalized = pow(10, rms_l / 20);
	double rms_r_normalized = pow(10, rms_r / 20);
	gtk_level_bar_set_value (GTK_LEVEL_BAR(gui->level_l),
                         rms_l_normalized);
	gtk_level_bar_set_value (GTK_LEVEL_BAR(gui->level_r),
                         rms_r_normalized);
	return;
}

gpointer
_icstr_init_gui(gpointer data)
{
	IceStreamer *self = data;
	struct icsr_gui *gui = &self->gui;

	/* Create top level window */
	gui->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (!gui->window)
		goto cleanup;
	gtk_window_set_title(GTK_WINDOW(gui->window), "Icestreamer");
	gtk_widget_set_size_request (gui->window, 280, 280);
	/* Add event handler for closing the window */
	g_signal_connect(gui->window, "delete-event", G_CALLBACK(icstr_gui_destroy),
			 NULL);

	/* Top containers, one hbox and one vbox to create a grid */
	gui->container = gtk_scrolled_window_new (NULL, NULL);
	if(!gui->container)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(gui->window), gui->container);

	gui->top_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
	if(!gui->top_hbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(gui->container), gui->top_hbox);

	gui->top_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
	if(!gui->top_vbox)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(gui->top_hbox), gui->top_vbox, TRUE, TRUE, 3);

	/* Audio source information */
	gui->source_frame = gtk_frame_new ("Audio source status");
	if(!gui->source_frame)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(gui->top_vbox), gui->source_frame, TRUE, FALSE, 3);

	gui->source_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
	if(!gui->source_box)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(gui->source_frame), gui->source_box);

	gui->time_label = gtk_label_new("Pending...");
	if(!gui->time_label)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(gui->source_box), gui->time_label, TRUE, FALSE, 6);

	gui->levels_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
	if(!gui->levels_box)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(gui->source_box), gui->levels_box, TRUE, FALSE, 3);

	gui->level_l = gtk_level_bar_new();
	if(!gui->level_l)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(gui->levels_box), gui->level_l, TRUE, TRUE, 0);

	gui->level_r = gtk_level_bar_new();
	if(!gui->level_r)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(gui->levels_box), gui->level_r, TRUE, TRUE, 0);

	/* Streams information */
	gui->streams_frame = gtk_frame_new ("Streams status");
	if(!gui->streams_frame)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(gui->top_vbox), gui->streams_frame, TRUE, FALSE, 3);

	gui->streams_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
	if(!gui->streams_box)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(gui->streams_frame), gui->streams_box);

	icstr_gui_add_streams(self);

	gtk_widget_show_all(gui->window);

        gtk_main();

 cleanup:
	return data;
}

gboolean
icstr_init_gui(IceStreamer *self, guint *argc, gchar ***argv)
{
	gtk_init(argc, argv);
	self->gui_thread = g_thread_new ("icstr_gui", _icstr_init_gui, self);
	return TRUE;
}
