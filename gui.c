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

const char css_style[] = ".time_label {"\
				"background: #669999;"\
				"min-height: 50px;"\
				"color: black;"\
				"font-weight: bold;"\
				"font-family: monospace;"\
				"text-shadow: 1px 1px 5px black;"\
				"box-shadow: inset 0px 0px 5px black;"\
				"border: 1px solid black;"\
			"}";

struct status_widget_map {
	GtkWidget *stream_box;
	GstElement *shout2send;
};

static void
_icstr_gui_destroy (GtkWidget *widget, gpointer data)
{
	IceStreamer *self = data;
	g_main_loop_quit (self->loop);
}

void
icstr_gui_destroy (IceStreamer *self)
{
	struct icsr_gui *gui = &self->gui;
	if (gui->window)
		gtk_widget_destroy(GTK_WIDGET(gui->window));
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
					"description", &description,
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
	geninfo_markup = g_markup_printf_escaped ("\n\t<b>Stream name:</b>\t\t%s\t\n"
				"\t<b>Description:</b>\t\t%s\t\n"
				"\t<b>Genre:</b>\t\t\t\t%s\t\n"
				"\t<b>Public:</b>\t\t\t\t%s\t\n"
				"\t<b>URL:</b>\t\t\t\t%s\t\n\n",
				stream_name,
				description,
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
	config_markup = g_markup_printf_escaped ("\n\t<b>Username</b>:\t:%s\t\n"
						  "\t<b>IP:</b>\t\t\t\t%s\t\n"
						  "\t<b>Port:</b>\t\t\t%u\t\n"
						  "\t<b>Protocol:</b>\t\t%s\t\n"
						  "\t<b>Mountpoint:</b>\t%s\t\n\n",
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
icstr_gui_realize_streambox(GtkWidget *stream_box, gpointer data)
{
	struct icsr_gui *gui = data;
	GtkRequisition size_req = {0};
	gtk_widget_get_preferred_size (stream_box, &size_req, NULL);
	if (gui->max_width < size_req.width)
		gui->max_width = size_req.width;
	if (!gui->height_inc)
		gui->height_inc = size_req.height + 24;
	gui->max_height += size_req.height + 24;
}

static void
icstr_gui_add_stream(IceStreamer *self, GstElement *stream_bin)
{
	struct icsr_gui *gui = &self->gui;
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

	if (gui->stream_counter) {
		separator = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
		if (!separator)
			goto fail;
		gtk_box_pack_start (GTK_BOX(gui->streams_box), separator, TRUE, FALSE, 5);
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

	/* Label to be hold the stream's name */
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
	g_signal_connect(stream_box, "realize", G_CALLBACK(icstr_gui_realize_streambox), gui);
	gtk_box_pack_start (GTK_BOX(gui->streams_box), stream_box, TRUE, FALSE, 3);

	g_timeout_add (1000, icstr_gui_update_stream_status, wmap);

	gui->stream_counter++;

	return;
 fail:
	g_printerr ("Couldn't add stream to gui\n");
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
}

void
icstr_gui_update_time_label(IceStreamer *self, GstClockTime tstamp)
{
	struct icsr_gui *gui = &self->gui;
	g_autofree gchar *tl_markup = NULL;

	tl_markup = g_markup_printf_escaped ("<tt>%"GST_TIME_FORMAT"</tt>",
					     GST_TIME_ARGS(tstamp));

	gtk_label_set_markup (GTK_LABEL(gui->time_label), tl_markup);
}

void
icstr_gui_update_levels(IceStreamer *self, double rms_l, double rms_r)
{
	struct icsr_gui *gui = &self->gui;
	double rms_l_normalized = 0.0L;
	double rms_r_normalized = 0.0L;

	rms_l_normalized = pow(10, rms_l / 20);
	rms_r_normalized = pow(10, rms_r / 20);

	gtk_level_bar_set_value (GTK_LEVEL_BAR(gui->level_l),
                         rms_l_normalized);
	gtk_level_bar_set_value (GTK_LEVEL_BAR(gui->level_r),
                         rms_r_normalized);
}

static void
icstr_gui_realize_sourcestats(GtkWidget *source_frame, gpointer data)
{
	struct icsr_gui *gui = data;
	GtkRequisition size_req = {0};
	gtk_widget_get_preferred_size (source_frame, &size_req, NULL);
	if (gui->max_width < size_req.width)
		gui->max_width = size_req.width + (size_req.width >> 1);
	gui->base_height = size_req.height + 71;
}

static void
icstr_main_window_set_geometry(GtkWindow *window, GtkStateFlags flags, gpointer data)
{
	struct icsr_gui *gui = data;
	GdkGeometry hints = {0};
	static int done = 0;

	if(done)
		return;

	/* Create window geometry */
	gui->max_width += 42;
	hints.min_width = gui->max_width;
	hints.max_width = gui->max_width;
	hints.base_width = -1;
	hints.min_height = gui->base_height;
	hints.base_height = gui->base_height;
	hints.height_inc = gui->height_inc;
	gui->max_height += gui->base_height;
	hints.max_height = gui->max_height;

	gtk_window_set_geometry_hints(GTK_WINDOW(window), NULL, &hints,
			(GdkWindowHints)(GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE |
					 GDK_HINT_BASE_SIZE | GDK_HINT_RESIZE_INC));

	gtk_window_resize(GTK_WINDOW(window), hints.max_width, hints.max_height);

	done = 1;
}

void
icstr_init_gui (IceStreamer *self)
{
	struct icsr_gui *gui = &self->gui;
	g_autoptr (GtkCssProvider) provider = NULL;
	GdkScreen *screen = NULL;
	GtkStyleContext *context = NULL;

	/* Create top level window */
	gui->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (!gui->window)
		goto cleanup;
	gtk_window_set_title(GTK_WINDOW(gui->window), "Icestreamer");
	/* Add event handler for closing the window */
	g_signal_connect(gui->window, "destroy", G_CALLBACK(_icstr_gui_destroy),
			 self);

	/* CSS Stuff */
	provider = gtk_css_provider_new();
	if(!provider)
		goto cleanup;
	screen = gtk_widget_get_screen(gui->window);
	gtk_style_context_add_provider_for_screen(screen,
					GTK_STYLE_PROVIDER(provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	gtk_css_provider_load_from_data(provider, css_style, -1, NULL);

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
	g_signal_connect(gui->source_frame, "realize",
			G_CALLBACK(icstr_gui_realize_sourcestats), gui);

	gui->source_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
	if(!gui->source_box)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(gui->source_frame), gui->source_box);

	gui->time_label = gtk_label_new("Pending...");
	if(!gui->time_label)
		goto cleanup;
	gtk_box_pack_start (GTK_BOX(gui->source_box), gui->time_label, TRUE, FALSE, 3);
	context = gtk_widget_get_style_context(gui->time_label);
	gtk_style_context_add_class(context,"time_label");

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

	/* Add signal handler for setting window geometry after all inner
	 * widgets have been realized. I used state-flags-changed because
	 * it does the trick and doesn't get triggered all the time. We want
	 * to do this once. */
	g_signal_connect(gui->window, "state-flags-changed",
			 G_CALLBACK(icstr_main_window_set_geometry), gui);

	gtk_widget_show_all(gui->window);

 cleanup:
	return;
}
