/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <gnome.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "main-window.h"
#include "message-window.h"
#include "sendmsg-window.h"

#include "toolbar-prefs.h"
#include "toolbar-factory.h"

/* Structures for holding various details */

/* one per type of toolbar */
struct BalsaToolbarModel_ {
    GSList *legal;
    GSList *standard;
    GSList **current;
};

/* one per instance of a toolbar: */
typedef struct BalsaToolbarData_ BalsaToolbarData;
struct BalsaToolbarData_ {
    BalsaToolbarModel *model;   /* the model for toolbars of this type */
    GHashTable *table;          /* hash table of icons:
                                   id -> BalsaToolbarIcon */
};

/* one per icon per instance of a toolbar: */
typedef struct BalsaToolbarIcon_ BalsaToolbarIcon;
struct BalsaToolbarIcon_ {
    GtkWidget *widget;
    GCallback callback;
    gpointer user_data;
    gboolean sensitive;
    gboolean active;
};

/* callbacks */
static void bt_button_toggled(GtkWidget * button, BalsaToolbarIcon * bti);
static void bt_destroy(GtkWidget * toolbar, BalsaToolbarData * btd);

/* forward references */
static BalsaToolbarData *bt_data_new(BalsaToolbarModel * model);
static BalsaToolbarIcon *bt_get_icon(GtkWidget * toolbar,
                                     const gchar * icon);

static GSList *toolbar_list;

/* The descriptions must be SHORT */
button_data toolbar_buttons[]={
    {"", N_("Separator"), "", 0},
    {BALSA_PIXMAP_RECEIVE, N_("Check"),
     N_("Check for new email"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_COMPOSE, N_("Compose"),
     N_("Compose message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_CONTINUE, N_("Continue"),
     N_("Continue message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_REPLY, N_("Reply"),
     N_("Reply to the current message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_REPLY_ALL, N_("Reply\nto all"),
     N_("Reply to all recipients"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_REPLY_GROUP, N_("Reply\nto group"),
     N_("Reply to mailing list"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_FORWARD, N_("Forward"),
     N_("Forward the current message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_PREVIOUS, N_("Previous"),
     N_("Open previous"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_NEXT, N_("Next"),
     N_("Open next"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_NEXT_UNREAD, N_("Next\nunread"),
     N_("Open next unread message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_NEXT_FLAGGED, N_("Next\nflagged"),
     N_("Open next flagged message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_PREVIOUS_PART, N_("Previous\npart"),
     N_("View previous part of message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_NEXT_PART, N_("Next\npart"),
     N_("View next part of message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {GTK_STOCK_DELETE, N_("Trash /\nDelete"),
     N_("Move the current message to trash"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_POSTPONE, N_("Postpone"),
     N_("Postpone current message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {GTK_STOCK_PRINT, N_("Print"),
     N_("Print current message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_SEND, N_("Send"),
     N_("Send this message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_SEND_RECEIVE, N_("Exchange"),
     N_("Send and Receive messages"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_ATTACHMENT, N_("Attach"),
     N_("Add attachments to this message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {GTK_STOCK_SAVE, N_("Save"),
     N_("Save the current item"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_IDENTITY, N_("Identity"),
     N_("Set identity to use for this message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {GTK_STOCK_SPELL_CHECK, N_("Spelling"),
     N_("Run a spell check"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {GTK_STOCK_CLOSE, N_("Close"), 
     N_("Close the compose window"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_MARKED_NEW, N_("Toggle\nnew"),
     N_("Toggle new message flag"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_MARK_ALL, N_("Mark all"),
     N_("Mark all messages in current mailbox"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_SHOW_HEADERS, N_("All\nheaders"),
     N_("Show all headers"), TOOLBAR_BUTTON_TYPE_TOGGLE},
    {BALSA_PIXMAP_TRASH_EMPTY, N_("Empty Trash"),
     N_("Delete messages from the Trash mailbox"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {GTK_STOCK_CANCEL, N_("Close"),
     N_("Close current mailbox"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_SHOW_PREVIEW, N_("Msg Preview"),
     N_("Show preview pane"), TOOLBAR_BUTTON_TYPE_TOGGLE},
#ifdef HAVE_GPGME
    {BALSA_PIXMAP_GPG_SIGN, N_("Sign"),
     N_("Sign message using GPG"), TOOLBAR_BUTTON_TYPE_TOGGLE},
    {BALSA_PIXMAP_GPG_ENCRYPT, N_("Encrypt"),
     N_("Encrypt message using GPG"), TOOLBAR_BUTTON_TYPE_TOGGLE},
#endif
    {GTK_STOCK_UNDO, N_("Undo"),
    N_("Undo most recent change"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {GTK_STOCK_REDO, N_("Redo"),
    N_("Redo most recent change"), TOOLBAR_BUTTON_TYPE_BUTTON},
};

const int toolbar_button_count =
    sizeof(toolbar_buttons) / sizeof(button_data);

/* Public methods. */

/* this should go to GTK because it modifies its internal structures. */
void
balsa_toolbar_remove_all(GtkWidget * widget)
{
    GList *child, *children;
    
    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (child = children; child; child = child->next)
        gtk_widget_destroy(child->data);
    g_list_free(children);
}

/* update_all_toolbars:
   Update all toolbars in all windows displaying a toolbar

   Called from toolbar-prefs.c after a change has been mada to a toolbar
   layout.
*/
void
update_all_toolbars(void)
{
    GSList *list;

    for (list = toolbar_list; list; list = g_slist_next(list))
        balsa_toolbar_refresh(GTK_WIDGET(list->data));
}

#define BALSA_KEY_TOOLBAR_DATA "balsa-toolbar-data"

/* Create a BalsaToolbarModel structure.
 */
BalsaToolbarModel *
balsa_toolbar_model_new(GSList * legal, GSList * standard,
                        GSList ** current)
{
    BalsaToolbarModel *model = g_new(BalsaToolbarModel, 1);
    model->legal = legal;
    model->standard = standard;
    model->current = current;
    return model;
}

/* Return the legal icons.
 */
GSList *
balsa_toolbar_model_get_legal(BalsaToolbarModel * model)
{
    return model->legal;
}

/* Return the current icons.
 */
GSList *
balsa_toolbar_model_get_current(BalsaToolbarModel * model)
{
    return *model->current ? *model->current : model->standard;
}

/* Add an icon to the list of current icons in a BalsaToolbarModel.
 */
void
balsa_toolbar_model_insert_icon(BalsaToolbarModel * model, gchar * icon,
                                gint position)
{
    if (get_toolbar_button_index(icon) >= 0)
        *model->current =
            g_slist_insert(*model->current, g_strdup(icon), position);
    else
        g_warning(_("Unknown toolbar icon \"%s\""), icon);
}

/* Remove all icons from the BalsaToolbarModel.
 */
void 
balsa_toolbar_model_clear(BalsaToolbarModel * model)
{
    g_slist_foreach(*model->current, (GFunc) g_free, NULL);
    g_slist_free(*model->current);
    *model->current = NULL;
}

/* Create a new instance of a toolbar with the given BalsaToolbarModel
 * set as object data.
 */
GtkWidget *
balsa_toolbar_new(BalsaToolbarModel * model)
{
    GtkWidget *toolbar = gtk_toolbar_new();
    BalsaToolbarData *btd = bt_data_new(model);
    
    g_object_set_data(G_OBJECT(toolbar), BALSA_KEY_TOOLBAR_DATA, btd);
    g_signal_connect(G_OBJECT(toolbar), "destroy",
                     G_CALLBACK(bt_destroy), btd);
    toolbar_list = g_slist_prepend(toolbar_list, toolbar);
    balsa_toolbar_refresh(toolbar);

    return toolbar;
}

/* Refresh (or initially populate) a toolbar.
 */
void
balsa_toolbar_refresh(GtkWidget * toolbar)
{
    BalsaToolbarData *btd =
        g_object_get_data(G_OBJECT(toolbar), BALSA_KEY_TOOLBAR_DATA);
    BalsaToolbarModel *model = btd->model;
    GSList *list;
    gchar *second_line;
    GtkWidget *parent;

    balsa_toolbar_remove_all(toolbar);

    /* Find out whether any button has 2 lines of text. */
    for (list = balsa_toolbar_model_get_current(model), second_line = NULL;
         list && !second_line; list = g_slist_next(list)) {
        const gchar *icon = list->data;
        gint button = get_toolbar_button_index(icon);

        if (button < 0) {
            g_warning("button '%s' not found. ABORT!\n", icon);
            continue;
        }

        second_line = strchr(_(toolbar_buttons[button].button_text), '\n');
    }

    for (list = balsa_toolbar_model_get_current(model); list;
         list = g_slist_next(list)) {
        const gchar *icon = list->data;
        BalsaToolbarIcon *bti;
        gint button;
        gchar *text, *tmp;
	GtkToolItem *tool_item;

        if (!*icon) {
            gtk_toolbar_insert(GTK_TOOLBAR(toolbar),
                               gtk_separator_tool_item_new(), -1);
            continue;
        }

        bti = bt_get_icon(toolbar, icon);
        button = get_toolbar_button_index(icon);

        if (button < 0)
            continue;

        tmp = _(toolbar_buttons[button].button_text);
        if (balsa_app.toolbar_wrap_button_text) {
            /* Make sure all buttons have the same number of lines of
             * text (1 or 2), to keep icons aligned */
            text = second_line && !strchr(tmp, '\n') ?
                g_strconcat(tmp, "\n", NULL) : g_strdup(tmp);
        } else {
            text = tmp = g_strdup(tmp);
            while ((tmp = strchr(tmp, '\n')))
                *tmp++ = ' ';
        }

        switch (toolbar_buttons[button].type) {
        case TOOLBAR_BUTTON_TYPE_TOGGLE:
	    tool_item = gtk_toggle_tool_button_new_from_stock(icon);
            gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON
                                              (tool_item), bti->active);
	    g_signal_connect(G_OBJECT(tool_item), "toggled",
                             G_CALLBACK(bt_button_toggled), bti);
	    if (bti->callback)
		g_signal_connect(G_OBJECT(tool_item), "toggled",
                                 G_CALLBACK(bti->callback), bti->user_data);
            break;
        case TOOLBAR_BUTTON_TYPE_BUTTON:
        default:
	    tool_item = gtk_tool_button_new_from_stock(icon);
	    if (bti->callback)
		g_signal_connect(G_OBJECT(tool_item), "clicked",
                                 G_CALLBACK(bti->callback), bti->user_data);
            break;
        }
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(tool_item), text);
        gtk_tool_item_set_tooltip(tool_item,
                                  GTK_TOOLBAR(toolbar)->tooltips,
                                  _(toolbar_buttons[button].help_text),
                                  _(toolbar_buttons[button].help_text));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), tool_item, -1);
	bti->widget = GTK_WIDGET(tool_item);
	g_object_add_weak_pointer(G_OBJECT(bti->widget),
				  (gpointer) &bti->widget);
        GTK_WIDGET_UNSET_FLAGS(GTK_WIDGET(bti->widget), GTK_CAN_FOCUS);
        g_free(text);
        gtk_widget_set_sensitive(bti->widget, bti->sensitive);
    }

    gtk_widget_show_all(toolbar);
    parent = gtk_widget_get_parent(toolbar);
    if (parent)
        gtk_widget_queue_resize(parent);
}

/* Get the GtkToolbar from a GnomeApp (for convenience: not really a
 * BalsaToolbar method).
 */
GtkWidget *
balsa_toolbar_get_from_gnome_app(GnomeApp * app)
{
    BonoboDockItem *item =
        gnome_app_get_dock_item_by_name(app, GNOME_APP_TOOLBAR_NAME);
    return bonobo_dock_item_get_child(item);
}

/* Set up a callback for the toolbar button with the given id.
 *
 * Saves the callback and user_data in a BalsaToolbarIcon structure, and
 * if the corresponding widget has been created, connects to its
 * "clicked" signal.
 */
guint
balsa_toolbar_set_callback(GtkWidget * toolbar, const gchar * icon,
                           GCallback callback, gpointer user_data)
{
    BalsaToolbarIcon *bti = bt_get_icon(toolbar, icon);
    guint handler = 0;

    bti->callback = callback;
    bti->user_data = user_data;
    if (bti->widget)
        handler = g_signal_connect(G_OBJECT(bti->widget), "clicked",
                                   callback, user_data);

    return handler;
}

/* Set a button's sensitivity, and save the state in the appropriate
 * BalsaToolbarIcon structure.
 */
void
balsa_toolbar_set_button_sensitive(GtkWidget * toolbar, const gchar * icon,
                                   gboolean sensitive)
{
    BalsaToolbarIcon *bti = bt_get_icon(toolbar, icon);

    bti->sensitive = sensitive;
    if (bti->widget)
        gtk_widget_set_sensitive(bti->widget, sensitive);
}

/* Get the active status of a toggle-button from the BalsaToolbarIcon
 * structure, which is kept current in the signal handler.
 */
gboolean
balsa_toolbar_get_button_active(GtkWidget * toolbar, const gchar * icon)
{
    BalsaToolbarIcon *bti = bt_get_icon(toolbar, icon);

    return bti->active;
}

/* Set the active status of a toggle-button if it exists, and save the
 * status in the BalsaToolbarIcon structure.
 */
void
balsa_toolbar_set_button_active(GtkWidget * toolbar, const gchar * icon,
                                gboolean active)
{
    BalsaToolbarIcon *bti = bt_get_icon(toolbar, icon);

    bti->active = active;
    if (bti->widget)
        gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON
                                          (bti->widget), active);
}

/* Signal handlers. */

/* Handler for the "toggled" signal. */
static void
bt_button_toggled(GtkWidget * button, BalsaToolbarIcon * bti)
{
    bti->active =
        gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(button));
}

/* Handler for the "destroy" signal. */
static void
bt_destroy(GtkWidget * toolbar, BalsaToolbarData * btd)
{
    g_hash_table_destroy(btd->table);
    g_free(btd);
    toolbar_list = g_slist_remove(toolbar_list, toolbar);
}

/* Helpers. */

/* Create and initialize a BalsaToolbarData structure with the given
 * BalsaToolbarModel.
 */
static void
bt_free_icon(gpointer data)
{
    BalsaToolbarIcon *bti = data;
    if (bti->widget)
	g_object_remove_weak_pointer(G_OBJECT(bti->widget),
				     (gpointer) &bti->widget);
    g_free(bti);
}

static BalsaToolbarData *
bt_data_new(BalsaToolbarModel * model)
{
    BalsaToolbarData *btd = g_new(BalsaToolbarData, 1);

    btd->model = model;
    btd->table =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, bt_free_icon);

    return btd;
}

/* Find or create the BalsaToolbarIcon structure for the given icon in
 * the given toolbar.
 */
static BalsaToolbarIcon *
bt_get_icon(GtkWidget * toolbar, const gchar * icon)
{
    BalsaToolbarData *btd =
        g_object_get_data(G_OBJECT(toolbar), BALSA_KEY_TOOLBAR_DATA);
    BalsaToolbarIcon *bti = g_hash_table_lookup(btd->table, icon);
    if (!bti) {
        bti = g_new0(BalsaToolbarIcon, 1);
        bti->sensitive = TRUE;
        g_hash_table_insert(btd->table, g_strdup(icon), bti);
    }
    return bti;
}
