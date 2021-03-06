/*
 * Purple - XMPP debugging tool
 *
 * Copyright (C) 2002-2003, Sean Egan
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 *
 */

#include "internal.h"
#include "gtkplugin.h"
#include "version.h"
#include "protocol.h"
#include "xmlnode.h"

#include "gtkutils.h"

#include <gdk/gdkkeysyms.h>

#include "gtk3compat.h"

#define PLUGIN_ID      "gtk-xmpp"
#define PLUGIN_DOMAIN  (g_quark_from_static_string(PLUGIN_ID))

typedef struct {
	PurpleConnection *gc;
	GtkWidget *window;
	GtkWidget *hbox;
	GtkWidget *dropdown;
	GtkWidget *view;
	GtkTextBuffer *buffer;
	struct {
		GtkTextTag *info;
		GtkTextTag *incoming;
		GtkTextTag *outgoing;
		GtkTextTag *bracket;
		GtkTextTag *tag;
		GtkTextTag *attr;
		GtkTextTag *value;
		GtkTextTag *xmlns;
	} tags;
	GtkWidget *entry;
	GtkTextBuffer *entry_buffer;
	GtkWidget *sw;
	int count;
	GList *accounts;
} XmppConsole;

XmppConsole *console = NULL;
static void *xmpp_console_handle = NULL;

static const gchar *xmpp_prpls[] = {
	"prpl-jabber", "prpl-gtalk", NULL
};

#define EMPTY_HTML \
"<html><head><style type='text/css'>" \
	"body { word-wrap: break-word; margin: 0; }" \
	"div.tab { padding-left: 1em; }" \
	"div.info { color: #777777; }" \
	"div.incoming { background-color: #ffcece; }" \
	"div.outgoing { background-color: #dcecc4; }" \
	"span.bracket { color: #940f8c; }" \
	"span.tag { color: #8b1dab; font-weight: bold; }" \
	"span.attr { color: #a02961; font-weight: bold; }" \
	"span.value { color: #324aa4; }" \
	"span.xmlns { color: #2cb12f; font-weight: bold;}" \
"</style></head></html>"

static gboolean
xmppconsole_is_xmpp_account(PurpleAccount *account)
{
	const gchar *prpl_name;
	int i;

	prpl_name = purple_account_get_protocol_id(account);

	i = 0;
	while (xmpp_prpls[i] != NULL) {
		if (purple_strequal(xmpp_prpls[i], prpl_name))
			return TRUE;
		i++;
	}

	return FALSE;
}

static void
purple_xmlnode_append_to_buffer(PurpleXmlNode *node, gint indent_level, GtkTextIter *iter, GtkTextTag *tag)
{
	PurpleXmlNode *c;
	gboolean need_end = FALSE, pretty = TRUE;
	gint i;

	g_return_if_fail(node != NULL);

	for (i = 0; i < indent_level; i++) {
		gtk_text_buffer_insert_with_tags(console->buffer, iter, "\t", 1, tag, NULL);
	}

	gtk_text_buffer_insert_with_tags(console->buffer, iter, "<", 1,
	                                 tag, console->tags.bracket, NULL);
	gtk_text_buffer_insert_with_tags(console->buffer, iter, node->name, -1,
	                                 tag, console->tags.tag, NULL);

	if (node->xmlns) {
		if ((!node->parent ||
		     !node->parent->xmlns ||
		     !purple_strequal(node->xmlns, node->parent->xmlns)) &&
		    !purple_strequal(node->xmlns, "jabber:client"))
		{
			gtk_text_buffer_insert_with_tags(console->buffer, iter, " ", 1,
			                                 tag, NULL);
			gtk_text_buffer_insert_with_tags(console->buffer, iter, "xmlns", 5,
			                                 tag, console->tags.attr, NULL);
			gtk_text_buffer_insert_with_tags(console->buffer, iter, "='", 2,
			                                 tag, NULL);
			gtk_text_buffer_insert_with_tags(console->buffer, iter, node->xmlns, -1,
			                                 tag, console->tags.xmlns, NULL);
			gtk_text_buffer_insert_with_tags(console->buffer, iter, "'", 1,
			                                 tag, NULL);
		}
	}
	for (c = node->child; c; c = c->next)
	{
		if (c->type == PURPLE_XMLNODE_TYPE_ATTRIB) {
			gtk_text_buffer_insert_with_tags(console->buffer, iter, " ", 1,
			                                 tag, NULL);
			gtk_text_buffer_insert_with_tags(console->buffer, iter, c->name, -1,
			                                 tag, console->tags.attr, NULL);
			gtk_text_buffer_insert_with_tags(console->buffer, iter, "='", 2,
			                                 tag, NULL);
			gtk_text_buffer_insert_with_tags(console->buffer, iter, c->data, -1,
			                                 tag, console->tags.value, NULL);
			gtk_text_buffer_insert_with_tags(console->buffer, iter, "'", 1,
			                                 tag, NULL);
		} else if (c->type == PURPLE_XMLNODE_TYPE_TAG || c->type == PURPLE_XMLNODE_TYPE_DATA) {
			if (c->type == PURPLE_XMLNODE_TYPE_DATA)
				pretty = FALSE;
			need_end = TRUE;
		}
	}

	if (need_end) {
		gtk_text_buffer_insert_with_tags(console->buffer, iter, ">", 1,
		                                 tag, console->tags.bracket, NULL);
		if (pretty) {
			gtk_text_buffer_insert_with_tags(console->buffer, iter, "\n", 1,
			                                 tag, NULL);
		}

		for (c = node->child; c; c = c->next)
		{
			if (c->type == PURPLE_XMLNODE_TYPE_TAG) {
				purple_xmlnode_append_to_buffer(c, indent_level + 1, iter, tag);
			} else if (c->type == PURPLE_XMLNODE_TYPE_DATA && c->data_sz > 0) {
				gtk_text_buffer_insert_with_tags(console->buffer, iter, c->data, c->data_sz,
				                                 tag, NULL);
			}
		}

		if (pretty) {
			for (i = 0; i < indent_level; i++) {
				gtk_text_buffer_insert_with_tags(console->buffer, iter, "\t", 1, tag, NULL);
			}
		}
		gtk_text_buffer_insert_with_tags(console->buffer, iter, "<", 1,
		                                 tag, console->tags.bracket, NULL);
		gtk_text_buffer_insert_with_tags(console->buffer, iter, "/", 1,
		                                 tag, NULL);
		gtk_text_buffer_insert_with_tags(console->buffer, iter, node->name, -1,
		                                 tag, console->tags.tag, NULL);
		gtk_text_buffer_insert_with_tags(console->buffer, iter, ">", 1,
		                                 tag, console->tags.bracket, NULL);
		gtk_text_buffer_insert_with_tags(console->buffer, iter, "\n", 1,
		                                 tag, NULL);
	} else {
		gtk_text_buffer_insert_with_tags(console->buffer, iter, "/", 1,
		                                 tag, NULL);
		gtk_text_buffer_insert_with_tags(console->buffer, iter, ">", 1,
		                                 tag, console->tags.bracket, NULL);
		gtk_text_buffer_insert_with_tags(console->buffer, iter, "\n", 1,
		                                 tag, NULL);
	}
}

static void
purple_xmlnode_received_cb(PurpleConnection *gc, PurpleXmlNode **packet, gpointer null)
{
	GtkTextIter iter;

	if (!console || console->gc != gc)
		return;

	gtk_text_buffer_get_end_iter(console->buffer, &iter);
	purple_xmlnode_append_to_buffer(*packet, 0, &iter, console->tags.incoming);
}

static void
purple_xmlnode_sent_cb(PurpleConnection *gc, char **packet, gpointer null)
{
	GtkTextIter iter;
	PurpleXmlNode *node;

	if (!console || console->gc != gc)
		return;
	node = purple_xmlnode_from_str(*packet, -1);

	if (!node)
		return;

	gtk_text_buffer_get_end_iter(console->buffer, &iter);
	purple_xmlnode_append_to_buffer(node, 0, &iter, console->tags.outgoing);
	purple_xmlnode_free(node);
}

static gboolean
message_send_cb(GtkWidget *widget, GdkEventKey *event, gpointer p)
{
	PurpleProtocol *protocol = NULL;
	PurpleConnection *gc;
	gchar *text;
	GtkTextIter start, end;

	if (event->keyval != GDK_KEY_KP_Enter && event->keyval != GDK_KEY_Return)
		return FALSE;

	gc = console->gc;

	if (gc)
		protocol = purple_connection_get_protocol(gc);

	gtk_text_buffer_get_bounds(console->entry_buffer, &start, &end);
	text = gtk_text_buffer_get_text(console->entry_buffer, &start, &end, FALSE);

	if (protocol)
		purple_protocol_server_iface_send_raw(protocol, gc, text, strlen(text));

	g_free(text);
	gtk_text_buffer_set_text(console->entry_buffer, "", 0);

	return TRUE;
}

static void
entry_changed_cb(GtkTextBuffer *buffer, void *data)
{
	GtkTextIter start, end;
	char *xmlstr, *str;
#if 0
	int wrapped_lines;
	int lines;
	GdkRectangle oneline;
	int height;
	int pad_top, pad_inside, pad_bottom;
#endif
	PurpleXmlNode *node;
	GtkStyleContext *style;

#if 0
	/* TODO WebKit: Do entry auto-sizing... */
	wrapped_lines = 1;
	gtk_text_buffer_get_start_iter(buffer, &iter);
	gtk_text_view_get_iter_location(GTK_TEXT_VIEW(console->entry), &iter, &oneline);
	while (gtk_text_view_forward_display_line(GTK_TEXT_VIEW(console->entry), &iter))
		wrapped_lines++;

	lines = gtk_text_buffer_get_line_count(buffer);

	/* Show a maximum of 64 lines */
	lines = MIN(lines, 6);
	wrapped_lines = MIN(wrapped_lines, 6);

	pad_top = gtk_text_view_get_pixels_above_lines(GTK_TEXT_VIEW(console->entry));
	pad_bottom = gtk_text_view_get_pixels_below_lines(GTK_TEXT_VIEW(console->entry));
	pad_inside = gtk_text_view_get_pixels_inside_wrap(GTK_TEXT_VIEW(console->entry));

	height = (oneline.height + pad_top + pad_bottom) * lines;
	height += (oneline.height + pad_inside) * (wrapped_lines - lines);

	gtk_widget_set_size_request(console->sw, -1, height + 6);
#endif

	gtk_text_buffer_get_bounds(buffer, &start, &end);
	str = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
	if (!str)
		return;
	xmlstr = g_strdup_printf("<xml>%s</xml>", str);
	node = purple_xmlnode_from_str(xmlstr, -1);
	style = gtk_widget_get_style_context(console->entry);
	if (node) {
		gtk_style_context_remove_class(style, GTK_STYLE_CLASS_ERROR);
	} else {
		gtk_style_context_add_class(style, GTK_STYLE_CLASS_ERROR);
	}
	g_free(str);
	g_free(xmlstr);
	if (node)
		purple_xmlnode_free(node);
}

static void
load_text_and_set_caret(const gchar *pre_text, const gchar *post_text)
{
	GtkTextIter where;
	GtkTextMark *mark;

	gtk_text_buffer_begin_user_action(console->entry_buffer);

	gtk_text_buffer_set_text(console->entry_buffer, pre_text, -1);

	gtk_text_buffer_get_end_iter(console->entry_buffer, &where);
	mark = gtk_text_buffer_create_mark(console->entry_buffer, NULL, &where, TRUE);

	gtk_text_buffer_insert(console->entry_buffer, &where, post_text, -1);

	gtk_text_buffer_get_iter_at_mark(console->entry_buffer, &where, mark);
	gtk_text_buffer_place_cursor(console->entry_buffer, &where);
	gtk_text_buffer_delete_mark(console->entry_buffer, mark);

	gtk_text_buffer_end_user_action(console->entry_buffer);
}

static void iq_clicked_cb(GtkWidget *w, gpointer nul)
{
	GtkWidget *vbox, *hbox, *to_entry, *label, *type_combo;
	GtkSizeGroup *sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	const gchar *to;
	int result;
	char *stanza;

	GtkWidget *dialog = gtk_dialog_new_with_buttons("<iq/>",
							GTK_WINDOW(console->window),
							GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							_("_Cancel"),
							GTK_RESPONSE_REJECT,
							_("_OK"),
							GTK_RESPONSE_ACCEPT,
							NULL);
	gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
	vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new("To:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	to_entry = gtk_entry_new();
	gtk_entry_set_activates_default (GTK_ENTRY (to_entry), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), to_entry, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	label = gtk_label_new("Type:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);

	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	type_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "get");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "set");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "result");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "error");
	gtk_combo_box_set_active(GTK_COMBO_BOX(type_combo), 0);
	gtk_box_pack_start(GTK_BOX(hbox), type_combo, FALSE, FALSE, 0);

	gtk_widget_show_all(vbox);

	result = gtk_dialog_run(GTK_DIALOG(dialog));
	if (result != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}

	to = gtk_entry_get_text(GTK_ENTRY(to_entry));
	stanza = g_strdup_printf("<iq %s%s%s id='console%x' type='%s'>",
				 to && *to ? "to='" : "",
				 to && *to ? to : "",
				 to && *to ? "'" : "",
				 g_random_int(),
				 gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(type_combo)));
	load_text_and_set_caret(stanza, "</iq>");
	gtk_widget_grab_focus(console->entry);
	g_free(stanza);

	gtk_widget_destroy(dialog);
	g_object_unref(sg);
}

static void presence_clicked_cb(GtkWidget *w, gpointer nul)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *to_entry;
	GtkWidget *status_entry;
	GtkWidget *priority_entry;
	GtkWidget *label;
	GtkWidget *show_combo;
	GtkWidget *type_combo;
	GtkSizeGroup *sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	const gchar *to, *status, *priority;
	gchar *type, *show;
	int result;
	char *stanza;

	GtkWidget *dialog = gtk_dialog_new_with_buttons("<presence/>",
							GTK_WINDOW(console->window),
							GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							_("_Cancel"),
							GTK_RESPONSE_REJECT,
							_("_OK"),
							GTK_RESPONSE_ACCEPT,
							NULL);
	gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
	vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new("To:");
	gtk_size_group_add_widget(sg, label);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	to_entry = gtk_entry_new();
	gtk_entry_set_activates_default (GTK_ENTRY (to_entry), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), to_entry, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	label = gtk_label_new("Type:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	type_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "default");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "unavailable");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "subscribe");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "unsubscribe");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "subscribed");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "unsubscribed");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "probe");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "error");
	gtk_combo_box_set_active(GTK_COMBO_BOX(type_combo), 0);
	gtk_box_pack_start(GTK_BOX(hbox), type_combo, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	label = gtk_label_new("Show:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	show_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(show_combo), "default");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(show_combo), "away");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(show_combo), "dnd");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(show_combo), "xa");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(show_combo), "chat");

	gtk_combo_box_set_active(GTK_COMBO_BOX(show_combo), 0);
	gtk_box_pack_start(GTK_BOX(hbox), show_combo, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new("Status:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	status_entry = gtk_entry_new();
	gtk_entry_set_activates_default (GTK_ENTRY (status_entry), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), status_entry, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new("Priority:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	priority_entry = gtk_spin_button_new_with_range(-128, 127, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(priority_entry), 0);
	gtk_box_pack_start(GTK_BOX(hbox), priority_entry, FALSE, FALSE, 0);

	gtk_widget_show_all(vbox);

	result = gtk_dialog_run(GTK_DIALOG(dialog));
	if (result != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}

	to = gtk_entry_get_text(GTK_ENTRY(to_entry));
	type = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(type_combo));
	if (purple_strequal(type, "default")) {
		g_free(type);
		type = g_strdup("");
	}
	show = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(show_combo));
	if (purple_strequal(show, "default")) {
		g_free(show);
		show = g_strdup("");
	}
	status = gtk_entry_get_text(GTK_ENTRY(status_entry));
	priority = gtk_entry_get_text(GTK_ENTRY(priority_entry));
	if (purple_strequal(priority, "0"))
		priority = "";

	stanza = g_strdup_printf("<presence %s%s%s id='console%x' %s%s%s>"
	                         "%s%s%s%s%s%s%s%s%s",
	                         *to ? "to='" : "",
	                         *to ? to : "",
	                         *to ? "'" : "",
	                         g_random_int(),

	                         *type ? "type='" : "",
	                         *type ? type : "",
	                         *type ? "'" : "",

	                         *show ? "<show>" : "",
	                         *show ? show : "",
	                         *show ? "</show>" : "",

	                         *status ? "<status>" : "",
	                         *status ? status : "",
	                         *status ? "</status>" : "",

	                         *priority ? "<priority>" : "",
	                         *priority ? priority : "",
	                         *priority ? "</priority>" : "");

	load_text_and_set_caret(stanza, "</presence>");
	gtk_widget_grab_focus(console->entry);
	g_free(stanza);
	g_free(type);
	g_free(show);

	gtk_widget_destroy(dialog);
	g_object_unref(sg);
}

static void message_clicked_cb(GtkWidget *w, gpointer nul)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *to_entry;
	GtkWidget *body_entry;
	GtkWidget *thread_entry;
	GtkWidget *subject_entry;
	GtkWidget *label;
	GtkWidget *type_combo;
	GtkSizeGroup *sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	const gchar *to, *body, *thread, *subject;
	char *stanza;
	int result;

	GtkWidget *dialog = gtk_dialog_new_with_buttons("<message/>",
							GTK_WINDOW(console->window),
							GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							_("_Cancel"),
							GTK_RESPONSE_REJECT,
							_("_OK"),
							GTK_RESPONSE_ACCEPT,
							NULL);
	gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
	vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new("To:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	to_entry = gtk_entry_new();
	gtk_entry_set_activates_default (GTK_ENTRY (to_entry), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), to_entry, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	label = gtk_label_new("Type:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	type_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "chat");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "headline");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "groupchat");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "normal");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "error");
	gtk_combo_box_set_active(GTK_COMBO_BOX(type_combo), 0);
	gtk_box_pack_start(GTK_BOX(hbox), type_combo, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new("Body:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	body_entry = gtk_entry_new();
	gtk_entry_set_activates_default (GTK_ENTRY (body_entry), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), body_entry, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new("Subject:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	subject_entry = gtk_entry_new();
	gtk_entry_set_activates_default (GTK_ENTRY (subject_entry), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), subject_entry, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new("Thread:");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_size_group_add_widget(sg, label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	thread_entry = gtk_entry_new();
	gtk_entry_set_activates_default (GTK_ENTRY (thread_entry), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), thread_entry, FALSE, FALSE, 0);

	gtk_widget_show_all(vbox);

	result = gtk_dialog_run(GTK_DIALOG(dialog));
	if (result != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}

	to = gtk_entry_get_text(GTK_ENTRY(to_entry));
	body = gtk_entry_get_text(GTK_ENTRY(body_entry));
	thread = gtk_entry_get_text(GTK_ENTRY(thread_entry));
	subject = gtk_entry_get_text(GTK_ENTRY(subject_entry));

	stanza = g_strdup_printf("<message %s%s%s id='console%x' type='%s'>"
	                         "%s%s%s%s%s%s%s%s%s",

	                         *to ? "to='" : "",
	                         *to ? to : "",
	                         *to ? "'" : "",
	                         g_random_int(),
	                         gtk_combo_box_text_get_active_text(
                               GTK_COMBO_BOX_TEXT(type_combo)),

	                         *body ? "<body>" : "",
	                         *body ? body : "",
	                         *body ? "</body>" : "",

	                         *subject ? "<subject>" : "",
	                         *subject ? subject : "",
	                         *subject ? "</subject>" : "",

	                         *thread ? "<thread>" : "",
	                         *thread ? thread : "",
	                         *thread ? "</thread>" : "");

	load_text_and_set_caret(stanza, "</message>");
	gtk_widget_grab_focus(console->entry);
	g_free(stanza);

	gtk_widget_destroy(dialog);
	g_object_unref(sg);
}

static void
signing_on_cb(PurpleConnection *gc)
{
	PurpleAccount *account;

	if (!console)
		return;

	account = purple_connection_get_account(gc);
	if (!xmppconsole_is_xmpp_account(account))
		return;

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(console->dropdown),
		purple_account_get_username(account));
	console->accounts = g_list_append(console->accounts, gc);
	console->count++;

	if (console->count == 1) {
		console->gc = gc;
		gtk_text_buffer_set_text(console->buffer, "", 0);
		gtk_combo_box_set_active(GTK_COMBO_BOX(console->dropdown), 0);
	} else
		gtk_widget_show_all(console->hbox);
}

static void
signed_off_cb(PurpleConnection *gc)
{
	int i = 0;
	GList *l;

	if (!console)
		return;

	l = console->accounts;
	while (l) {
		PurpleConnection *g = l->data;
		if (gc == g)
			break;
		i++;
		l = l->next;
	}

	if (l == NULL)
		return;

	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(console->dropdown), i);
	console->accounts = g_list_remove(console->accounts, gc);
	console->count--;

	if (gc == console->gc) {
		GtkTextIter end;
		gtk_text_buffer_get_end_iter(console->buffer, &end);
		gtk_text_buffer_insert_with_tags(console->buffer, &end, _("Logged out."), -1,
		                                 console->tags.info, NULL);
		console->gc = NULL;
	}
}

static void
console_destroy(GtkWidget *window, gpointer nul)
{
	g_list_free(console->accounts);
	g_free(console);
	console = NULL;
}

static void
dropdown_changed_cb(GtkComboBox *widget, gpointer nul)
{
	if (!console)
		return;

	console->gc = g_list_nth_data(console->accounts, gtk_combo_box_get_active(GTK_COMBO_BOX(console->dropdown)));
	gtk_text_buffer_set_text(console->buffer, "", 0);
}

static void
create_console(PurplePluginAction *action)
{
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *label;
	GtkWidget *toolbar;
	GList *connections;
	GtkToolItem *button;
	GtkCssProvider *entry_css;
	GtkStyleContext *context;

	if (console) {
		gtk_window_present(GTK_WINDOW(console->window));
		return;
	}

	console = g_new0(XmppConsole, 1);

	console->window = pidgin_create_window(_("XMPP Console"), PIDGIN_HIG_BORDER, NULL, TRUE);
	g_signal_connect(G_OBJECT(console->window), "destroy", G_CALLBACK(console_destroy), NULL);
	gtk_window_set_default_size(GTK_WINDOW(console->window), 580, 400);
	gtk_container_add(GTK_CONTAINER(console->window), vbox);

	console->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_box_pack_start(GTK_BOX(vbox), console->hbox, FALSE, FALSE, 0);
	label = gtk_label_new(_("Account: "));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_box_pack_start(GTK_BOX(console->hbox), label, FALSE, FALSE, 0);
	console->dropdown = gtk_combo_box_text_new();
	for (connections = purple_connections_get_all(); connections; connections = connections->next) {
		PurpleConnection *gc = connections->data;
		if (xmppconsole_is_xmpp_account(purple_connection_get_account(gc))) {
			console->count++;
			console->accounts = g_list_append(console->accounts, gc);
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(console->dropdown),
						  purple_account_get_username(purple_connection_get_account(gc)));
			if (!console->gc)
				console->gc = gc;
		}
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(console->dropdown), 0);
	gtk_box_pack_start(GTK_BOX(console->hbox), console->dropdown, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT(console->dropdown), "changed", G_CALLBACK(dropdown_changed_cb), NULL);

	console->view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(console->view), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(console->view), GTK_WRAP_WORD);

	console->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console->view));
	console->tags.info = gtk_text_buffer_create_tag(console->buffer, "info",
	                                                "foreground", "#777777", NULL);
	console->tags.incoming = gtk_text_buffer_create_tag(console->buffer, "incoming",
	                                                    "paragraph-background", "#ffcece", NULL);
	console->tags.outgoing = gtk_text_buffer_create_tag(console->buffer, "outgoing",
	                                                    "paragraph-background", "#dcecc4", NULL);
	console->tags.bracket = gtk_text_buffer_create_tag(console->buffer, "bracket",
	                                                   "foreground", "#940f8c", NULL);
	console->tags.tag = gtk_text_buffer_create_tag(console->buffer, "tag",
	                                               "foreground", "#8b1dab",
	                                               "weight", PANGO_WEIGHT_BOLD, NULL);
	console->tags.attr = gtk_text_buffer_create_tag(console->buffer, "attr",
	                                                "foreground", "#a02961",
	                                                "weight", PANGO_WEIGHT_BOLD, NULL);
	console->tags.value = gtk_text_buffer_create_tag(console->buffer, "value",
	                                                 "foreground", "#324aa4", NULL);
	console->tags.xmlns = gtk_text_buffer_create_tag(console->buffer, "xmlns",
	                                                 "foreground", "#2cb12f",
	                                                 "weight", PANGO_WEIGHT_BOLD, NULL);

	if (console->count == 0) {
		GtkTextIter start, end;
		gtk_text_buffer_set_text(console->buffer, _("Not connected to XMPP"), -1);
		gtk_text_buffer_get_bounds(console->buffer, &start, &end);
		gtk_text_buffer_apply_tag(console->buffer, console->tags.info, &start, &end);
	}
	gtk_box_pack_start(GTK_BOX(vbox),
		pidgin_make_scrollable(console->view, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC, GTK_SHADOW_ETCHED_IN, -1, -1),
		TRUE, TRUE, 0);

	toolbar = gtk_toolbar_new();
	button = gtk_tool_button_new(NULL, "<iq/>");
	gtk_tool_item_set_is_important(button, TRUE);
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(iq_clicked_cb), NULL);
	gtk_container_add(GTK_CONTAINER(toolbar), GTK_WIDGET(button));

	button = gtk_tool_button_new(NULL, "<presence/>");
	gtk_tool_item_set_is_important(button, TRUE);
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(presence_clicked_cb), NULL);
	gtk_container_add(GTK_CONTAINER(toolbar), GTK_WIDGET(button));

	button = gtk_tool_button_new(NULL, "<message/>");
	gtk_tool_item_set_is_important(button, TRUE);
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(message_clicked_cb), NULL);
	gtk_container_add(GTK_CONTAINER(toolbar), GTK_WIDGET(button));

	gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

	console->entry = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(console->entry), GTK_WRAP_WORD);
	entry_css = gtk_css_provider_new();
	gtk_css_provider_load_from_data(entry_css,
	                                "textview." GTK_STYLE_CLASS_ERROR " text {background-color:#ffcece;}",
	                                -1, NULL);
	context = gtk_widget_get_style_context(console->entry);
	gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(entry_css),
	                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	console->entry_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console->entry));
	g_signal_connect(G_OBJECT(console->entry),"key-press-event", G_CALLBACK(message_send_cb), console);

	console->sw = pidgin_make_scrollable(console->entry, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC, GTK_SHADOW_ETCHED_IN, -1, -1);
	gtk_box_pack_start(GTK_BOX(vbox), console->sw, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(console->entry_buffer), "changed", G_CALLBACK(entry_changed_cb), NULL);

	entry_changed_cb(console->entry_buffer, NULL);

	gtk_widget_show_all(console->window);
	if (console->count < 2)
		gtk_widget_hide(console->hbox);
}

static GList *
actions(PurplePlugin *plugin)
{
	GList *l = NULL;
	PurplePluginAction *act = NULL;

	act = purple_plugin_action_new(_("XMPP Console"), create_console);
	l = g_list_append(l, act);

	return l;
}

static PidginPluginInfo *
plugin_query(GError **error)
{
	const gchar * const authors[] = {
		"Sean Egan <seanegan@gmail.com>",
		NULL
	};

	return pidgin_plugin_info_new(
		"id",           PLUGIN_ID,
		"name",         N_("XMPP Console"),
		"version",      DISPLAY_VERSION,
		"category",     N_("Protocol utility"),
		"summary",      N_("Send and receive raw XMPP stanzas."),
		"description",  N_("This plugin is useful for debugging XMPP servers "
		                   "or clients."),
		"authors",      authors,
		"website",      PURPLE_WEBSITE,
		"abi-version",  PURPLE_ABI_VERSION,
		"actions-cb",   actions,
		NULL
	);
}

static gboolean
plugin_load(PurplePlugin *plugin, GError **error)
{
	int i;
	gboolean any_registered = FALSE;

	xmpp_console_handle = plugin;

	i = 0;
	while (xmpp_prpls[i] != NULL) {
		PurpleProtocol *xmpp;

		xmpp = purple_protocols_find(xmpp_prpls[i]);
		i++;

		if (!xmpp)
			continue;
		any_registered = TRUE;

		purple_signal_connect(xmpp, "jabber-receiving-xmlnode",
			xmpp_console_handle,
			PURPLE_CALLBACK(purple_xmlnode_received_cb), NULL);
		purple_signal_connect(xmpp, "jabber-sending-text",
			xmpp_console_handle,
			PURPLE_CALLBACK(purple_xmlnode_sent_cb), NULL);
	}

	if (!any_registered) {
		g_set_error(error, PLUGIN_DOMAIN, 0, _("No XMPP protocol is loaded."));
		return FALSE;
	}

	purple_signal_connect(purple_connections_get_handle(), "signing-on",
		plugin, PURPLE_CALLBACK(signing_on_cb), NULL);
	purple_signal_connect(purple_connections_get_handle(), "signed-off",
		plugin, PURPLE_CALLBACK(signed_off_cb), NULL);

	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin, GError **error)
{
	if (console)
		gtk_widget_destroy(console->window);
	return TRUE;
}

PURPLE_PLUGIN_INIT(xmppconsole, plugin_query, plugin_load, plugin_unload);
