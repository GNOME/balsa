/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-message.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/utsname.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "mime.h"
#include "misc.h"
#include "html.h"
#include <glib/gi18n.h>
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"
#include "balsa-mime-widget-message.h"
#include "balsa-mime-widget-image.h"
#include "balsa-mime-widget-crypto.h"

#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "send.h"
#include "quote-color.h"
#include "sendmsg-window.h"
#include "libbalsa-vfs.h"

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

#ifdef HAVE_GPGME
#  include "gmime-part-rfc2440.h"
#endif

#  define FORWARD_SEARCH(iter, text, match_begin, match_end)            \
    gtk_text_iter_forward_search((iter), (text),                        \
    GTK_TEXT_SEARCH_CASE_INSENSITIVE, (match_begin), (match_end), NULL)
#  define BACKWARD_SEARCH(iter, text, match_begin, match_end)           \
    gtk_text_iter_backward_search((iter), (text),                       \
    GTK_TEXT_SEARCH_CASE_INSENSITIVE, (match_begin), (match_end), NULL)

enum {
    SELECT_PART,
    LAST_SIGNAL,
};

enum {
    PART_INFO_COLUMN = 0,
    PART_NUM_COLUMN,
    MIME_ICON_COLUMN,
    MIME_TYPE_COLUMN,
    NUM_COLUMNS
};

typedef struct _BalsaPartInfoClass BalsaPartInfoClass;

struct _BalsaPartInfo {
    GObject parent_object;

    LibBalsaMessageBody *body;

    /* MIME widget */
    BalsaMimeWidget *mime_widget;

    /* The contect menu; referenced */
    GtkWidget *popup_menu;

    /* the path in the tree view */
    GtkTreePath *path;
};

struct _BalsaPartInfoClass {
    GObjectClass parent_class;
};

static GType balsa_part_info_get_type();

#define TYPE_BALSA_PART_INFO          \
        (balsa_part_info_get_type ())
#define BALSA_PART_INFO(obj)          \
        (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_BALSA_PART_INFO, BalsaPartInfo))
#define IS_BALSA_PART_INFO(obj)       \
        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_BALSA_PART_INFO))

static gint balsa_message_signals[LAST_SIGNAL];

/* widget */
static void balsa_message_class_init(BalsaMessageClass * klass);
static void balsa_message_init(BalsaMessage * bm);

static void balsa_message_destroy(GObject * object);

static void display_headers(BalsaMessage * bm);
static void display_content(BalsaMessage * bm);

static LibBalsaMessageBody *add_part(BalsaMessage *bm, BalsaPartInfo *info,
                                     GtkWidget * container);
static LibBalsaMessageBody *add_multipart(BalsaMessage * bm,
                                          LibBalsaMessageBody * parent,
                                          GtkWidget * container);
static void select_part(BalsaMessage * bm, BalsaPartInfo *info);
static void tree_activate_row_cb(GtkTreeView *treeview, GtkTreePath *arg1,
                                 GtkTreeViewColumn *arg2, gpointer user_data);
static gboolean tree_menu_popup_key_cb(GtkWidget *widget, gpointer user_data);
static void bm_gesture_pressed_cb(GtkGestureMultiPress *multi_press,
                                  gint                  n_press,
                                  gdouble               x,
                                  gdouble               y,
                                  gpointer              user_data);

static void part_info_init(BalsaMessage * bm, BalsaPartInfo * info);
static void part_context_save_all_cb(GtkWidget * menu_item, GList * info_list);
static void part_context_dump_all_cb(GtkWidget * menu_item, GList * info_list);
static void part_create_menu (BalsaPartInfo* info);

static GtkNotebookClass *parent_class = NULL;

/* stuff needed for sending Message Disposition Notifications */
static void handle_mdn_request(GtkWindow *parent, LibBalsaMessage *message);
static LibBalsaMessage *create_mdn_reply (const LibBalsaIdentity *mdn_ident,
                                          LibBalsaMessage *for_msg,
                                          gboolean manual);
static GtkWidget* create_mdn_dialog (GtkWindow *parent, gchar *sender,
				     gchar *mdn_to_address,
                                     LibBalsaMessage *send_msg,
                                     LibBalsaIdentity *mdn_ident);
static void mdn_dialog_response(GtkWidget * dialog, gint response,
                                gpointer user_data);

static void balsa_part_info_init(GObject *object, gpointer data);
static BalsaPartInfo* balsa_part_info_new(LibBalsaMessageBody* body);
static void balsa_part_info_free(GObject * object);


#ifdef HAVE_GPGME
static LibBalsaMsgProtectState balsa_message_scan_signatures(LibBalsaMessageBody *body,
							     LibBalsaMessage * message);
static GdkPixbuf * get_crypto_content_icon(LibBalsaMessageBody * body,
					   const gchar * content_type,
					   gchar ** icon_title);
static void message_recheck_crypto_cb(GtkWidget * button, BalsaMessage * bm);
#endif /* HAVE_GPGME */


static void
balsa_part_info_class_init(BalsaPartInfoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = balsa_part_info_free;
}

static GType
balsa_part_info_get_type()
{
    static GType balsa_part_info_type = 0 ;

    if (!balsa_part_info_type) {
        static const GTypeInfo balsa_part_info_info =
            {
                sizeof (BalsaPartInfoClass),
                (GBaseInitFunc) NULL,
                (GBaseFinalizeFunc) NULL,
                (GClassInitFunc) balsa_part_info_class_init,
                (GClassFinalizeFunc) NULL,
                NULL,
                sizeof(BalsaPartInfo),
                0,
                (GInstanceInitFunc) balsa_part_info_init
            };
        balsa_part_info_type =
           g_type_register_static (G_TYPE_OBJECT, "BalsaPartInfo",
                                   &balsa_part_info_info, 0);
    }
    return balsa_part_info_type;
}

GType
balsa_message_get_type()
{
    static GType balsa_message_type = 0;

    if (!balsa_message_type) {
        static const GTypeInfo balsa_message_info = {
            sizeof(BalsaMessageClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) balsa_message_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(BalsaMessage),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) balsa_message_init
        };

        balsa_message_type =
        	g_type_register_static(GTK_TYPE_BOX, "BalsaMessage", &balsa_message_info, 0);
    }

    return balsa_message_type;
}

static void
balsa_message_class_init(BalsaMessageClass * klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    balsa_message_signals[SELECT_PART] =
        g_signal_new("select-part",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(BalsaMessageClass, select_part),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0);

    object_class->dispose = balsa_message_destroy;

    parent_class = g_type_class_peek_parent(klass);

    klass->select_part = NULL;

}

/* Helpers for balsa_message_init. */
#define BALSA_MESSAGE_ATTACH_BTN "balsa-message-attach-btn"
#define bm_header_widget_att_button(balsa_message) \
    g_object_get_data(G_OBJECT(balsa_message), BALSA_MESSAGE_ATTACH_BTN)

static void
balsa_headers_attachments_popup(GtkButton * button, BalsaMessage * bm)
{
    if (bm->parts_popup) {
        gtk_menu_popup_at_widget(GTK_MENU(bm->parts_popup),
                                 GTK_WIDGET(bm),
                                 GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                                 NULL);
    }
}


static GtkWidget **
bm_header_tl_buttons(BalsaMessage * bm)
{
    GPtrArray *array;
    GtkWidget *button;

    array = g_ptr_array_new();

#ifdef HAVE_GPGME
    button = gtk_button_new_from_icon_name(balsa_icon_id(BALSA_PIXMAP_GPG_RECHECK));
    gtk_widget_set_tooltip_text(button,
			        _("Check cryptographic signature"));
    g_signal_connect(G_OBJECT(button), "focus_in_event",
		     G_CALLBACK(balsa_mime_widget_limit_focus),
		     (gpointer) bm);
    g_signal_connect(G_OBJECT(button), "focus_out_event",
		     G_CALLBACK(balsa_mime_widget_unlimit_focus),
		     (gpointer) bm);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    g_signal_connect(button, "clicked",
		     G_CALLBACK(message_recheck_crypto_cb), bm);
    g_ptr_array_add(array, button);
#endif

    button = gtk_button_new_from_icon_name(balsa_icon_id(BALSA_PIXMAP_ATTACHMENT));
    gtk_widget_set_tooltip_text(button,
			        _("Select message part to display"));
    g_signal_connect(G_OBJECT(button), "focus_in_event",
		     G_CALLBACK(balsa_mime_widget_limit_focus),
		     (gpointer) bm);
    g_signal_connect(G_OBJECT(button), "focus_out_event",
		     G_CALLBACK(balsa_mime_widget_unlimit_focus),
		     (gpointer) bm);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    g_signal_connect(button, "clicked",
		     G_CALLBACK(balsa_headers_attachments_popup), bm);
    g_signal_connect(button, "key_press_event",
		     G_CALLBACK(balsa_mime_widget_key_press_event), bm);
    g_object_set_data(G_OBJECT(bm), BALSA_MESSAGE_ATTACH_BTN, button);
    g_ptr_array_add(array, button);

    g_ptr_array_add(array, NULL);

    return (GtkWidget **) g_ptr_array_free(array, FALSE);
}

static void
on_content_size_alloc(GtkWidget * widget, GtkAllocation * allocation,
                      gint baseline, GtkAllocation * clip,
		      gpointer user_data)
{
    gtk_container_foreach (GTK_CONTAINER(widget), balsa_mime_widget_image_resize_all, NULL);
}

/*
 * Callbacks and helpers for the find bar.
 */

typedef enum {
    BM_FIND_STATUS_INIT,
    BM_FIND_STATUS_FOUND,
    BM_FIND_STATUS_WRAPPED,
    BM_FIND_STATUS_NOT_FOUND
} BalsaMessageFindStatus;

static void
bm_find_set_status(BalsaMessage * bm, BalsaMessageFindStatus status)
{
    const gchar *text = "";
    gboolean sensitive = FALSE;

    switch (status) {
        default:
        case BM_FIND_STATUS_INIT:
            break;
        case BM_FIND_STATUS_FOUND:
            /* The widget returned "found"; if it really found a string,
             * we sensitize the "next" and "previous" buttons, but if
             * the find-entry was empty, we desensitize them. */
            if (gtk_entry_get_text(GTK_ENTRY(bm->find_entry))[0])
                sensitive = TRUE;
            break;
        case BM_FIND_STATUS_WRAPPED:
            text = _("Wrapped");
            sensitive = TRUE;
            break;
        case BM_FIND_STATUS_NOT_FOUND:
            text = _("Not found");
            break;
    }

    gtk_label_set_text(GTK_LABEL(bm->find_label), text);
    gtk_separator_tool_item_set_draw(GTK_SEPARATOR_TOOL_ITEM
                                     (bm->find_sep), text[0] != '\0');
    gtk_widget_set_sensitive(bm->find_prev, sensitive);
    gtk_widget_set_sensitive(bm->find_next, sensitive);
}

static void
bm_find_scroll_to_rectangle(BalsaMessage * bm,
                            GtkWidget    * widget,
                            GdkRectangle * rectangle)
{
    gint x, y;
    GtkAdjustment *hadj, *vadj;

    gtk_widget_translate_coordinates(widget, bm->bm_widget->widget,
                                     rectangle->x, rectangle->y,
                                     &x, &y);

    g_object_get(G_OBJECT(bm->scroll), "hadjustment", &hadj,
                                       "vadjustment", &vadj, NULL);
    gtk_adjustment_clamp_page(hadj, x, x + rectangle->width);
    gtk_adjustment_clamp_page(vadj, y, y + rectangle->height);
}

static void
bm_find_scroll_to_selection(BalsaMessage * bm,
                            GtkTextView  * text_view,
                            GtkTextIter  * begin_iter,
                            GtkTextIter  * end_iter)
{
    GdkRectangle begin_location, end_location;

    gtk_text_view_get_iter_location(text_view, begin_iter,
                                    &begin_location);
    gtk_text_view_get_iter_location(text_view, end_iter,
                                    &end_location);
    end_location.width = 0;
    gdk_rectangle_union(&begin_location, &end_location, &begin_location);
    gtk_text_view_buffer_to_window_coords(text_view,
                                          GTK_TEXT_WINDOW_WIDGET,
                                          begin_location.x,
                                          begin_location.y,
                                          &begin_location.x,
                                          &begin_location.y);

    bm_find_scroll_to_rectangle(bm, GTK_WIDGET(text_view), &begin_location);
}

#ifdef HAVE_HTML_WIDGET
typedef struct {
    BalsaMessage *bm;
    GtkWidget    *widget;
    gboolean      continuing;
    gboolean      wrapping;
} BalsaMessageFindInfo;
#define BALSA_MESSAGE_FIND_INFO "BalsaMessageFindInfo"

static void
bm_find_cb(const gchar * text, gboolean found, gpointer data)
{
    BalsaMessageFindInfo *info = data;

    if (!found && info->continuing) {
        info->wrapping = TRUE;
        libbalsa_html_search(info->widget, text, info->bm->find_forward,
                             TRUE, bm_find_cb, info);
        return;
    }

    if (found && *text) {
        GdkRectangle selection_bounds;
        if (libbalsa_html_get_selection_bounds(info->widget,
                                               &selection_bounds))
            bm_find_scroll_to_rectangle(info->bm, info->widget,
                                        &selection_bounds);
    }

    if (info->wrapping) {
        info->wrapping = FALSE;
        bm_find_set_status(info->bm, BM_FIND_STATUS_WRAPPED);
    } else
        bm_find_set_status(info->bm, found ? BM_FIND_STATUS_FOUND :
                                             BM_FIND_STATUS_NOT_FOUND);
}
#endif                          /* HAVE_HTML_WIDGET */

static void
bm_find_entry_changed_cb(GtkEditable * editable, gpointer data)
{
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(editable));
    BalsaMessage *bm = data;
    GtkWidget *widget = bm->current_part->mime_widget->widget;
    gboolean found = FALSE;

    if (GTK_IS_TEXT_VIEW(widget)) {
        GtkTextView *text_view = GTK_TEXT_VIEW(widget);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
        GtkTextIter match_begin, match_end;

        if (bm->find_forward) {
            found = FORWARD_SEARCH(&bm->find_iter, text,
                                   &match_begin, &match_end);
            if (!found) {
                /* Silently wrap to the top. */
                gtk_text_buffer_get_start_iter(buffer, &bm->find_iter);
                found = FORWARD_SEARCH(&bm->find_iter, text,
                                       &match_begin, &match_end);
            }
        } else {
            found = BACKWARD_SEARCH(&bm->find_iter, text,
                                    &match_begin, &match_end);
            if (!found) {
                /* Silently wrap to the bottom. */
                gtk_text_buffer_get_end_iter(buffer, &bm->find_iter);
                found = BACKWARD_SEARCH(&bm->find_iter, text,
                                        &match_begin, &match_end);
            }
        }

        if (found) {
            gtk_text_buffer_select_range(buffer, &match_begin, &match_end);
            bm_find_scroll_to_selection(bm, text_view,
                                        &match_begin, &match_end);
            bm->find_iter = match_begin;
        }

        bm_find_set_status(bm, found ? BM_FIND_STATUS_FOUND :
                                       BM_FIND_STATUS_NOT_FOUND);
#ifdef HAVE_HTML_WIDGET
    } else if (libbalsa_html_can_search(widget)) {
        BalsaMessageFindInfo *info;

        if (!(info = bm->html_find_info)) {
            bm->html_find_info = info = g_new(BalsaMessageFindInfo, 1);
            info->bm = bm;
        }
        info->widget = widget;
        info->continuing = FALSE;
        info->wrapping = FALSE;

        libbalsa_html_search(widget, text, bm->find_forward, TRUE,
                             bm_find_cb, info);
#endif                          /* HAVE_HTML_WIDGET */
    } else
        g_assert_not_reached();
}

static void
bm_find_again(BalsaMessage * bm, gboolean find_forward)
{
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(bm->find_entry));
    GtkWidget *widget = bm->current_part->mime_widget->widget;
    gboolean found;

    bm->find_forward = find_forward;

    if (GTK_IS_TEXT_VIEW(widget)) {
        GtkTextView *text_view = GTK_TEXT_VIEW(widget);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
        GtkTextIter match_begin, match_end;

        if (find_forward) {
            gtk_text_iter_forward_char(&bm->find_iter);
            found = FORWARD_SEARCH(&bm->find_iter, text,
                                   &match_begin, &match_end);
            if (!found) {
                gtk_text_buffer_get_start_iter(buffer, &bm->find_iter);
                FORWARD_SEARCH(&bm->find_iter, text, &match_begin,
                               &match_end);
            }
        } else {
            gtk_text_iter_backward_char(&bm->find_iter);
            found = BACKWARD_SEARCH(&bm->find_iter, text,
                                    &match_begin, &match_end);
            if (!found) {
                gtk_text_buffer_get_end_iter(buffer, &bm->find_iter);
                BACKWARD_SEARCH(&bm->find_iter, text, &match_begin,
                                &match_end);
            }
        }

        gtk_text_buffer_select_range(buffer, &match_begin, &match_end);
        bm_find_scroll_to_selection(bm, text_view,
                                    &match_begin, &match_end);
        bm->find_iter = match_begin;

        bm_find_set_status(bm, found ?
                           BM_FIND_STATUS_FOUND : BM_FIND_STATUS_WRAPPED);
#ifdef HAVE_HTML_WIDGET
    } else if (libbalsa_html_can_search(widget)) {
        BalsaMessageFindInfo *info = bm->html_find_info;

        info->continuing = TRUE;
        libbalsa_html_search(widget, text, find_forward, FALSE,
                             bm_find_cb, info);
#endif                          /* HAVE_HTML_WIDGET */
    } else
        g_assert_not_reached();
}

static void
bm_find_prev_cb(GtkToolButton * prev_button, gpointer data)
{
    bm_find_again((BalsaMessage *) data, FALSE);
}

static void
bm_find_next_cb(GtkToolButton * prev_button, gpointer data)
{
    bm_find_again((BalsaMessage *) data, TRUE);
}

static GtkWidget *
bm_find_bar_new(BalsaMessage * bm)
{
    GtkWidget *toolbar;
    GtkWidget *hbox;
    GtkToolItem *tool_item;
    GtkWidget *image;

    toolbar = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_BOTH_HORIZ);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("Find:")));
    bm->find_entry = gtk_entry_new();
    g_signal_connect(bm->find_entry, "changed",
                     G_CALLBACK(bm_find_entry_changed_cb), bm);
    gtk_box_pack_start(GTK_BOX(hbox), bm->find_entry);

    tool_item = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(tool_item), hbox);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), tool_item, -1);

    image = gtk_image_new_from_icon_name("pan-up-symbolic");
    tool_item = gtk_tool_button_new(image, _("Previous"));
    bm->find_prev = GTK_WIDGET(tool_item);
    gtk_tool_item_set_is_important(tool_item, TRUE);
    g_signal_connect(tool_item, "clicked", G_CALLBACK(bm_find_prev_cb), bm);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), tool_item, -1);

    image = gtk_image_new_from_icon_name("pan-down-symbolic");
    tool_item = gtk_tool_button_new(image, _("Next"));
    bm->find_next = GTK_WIDGET(tool_item);
    gtk_tool_item_set_is_important(tool_item, TRUE);
    g_signal_connect(tool_item, "clicked", G_CALLBACK(bm_find_next_cb), bm);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), tool_item, -1);

    bm->find_sep = GTK_WIDGET(gtk_separator_tool_item_new());
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(bm->find_sep), -1);

    bm->find_label = gtk_label_new("");
    tool_item = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(tool_item), bm->find_label);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), tool_item, -1);

    return toolbar;
}

static void bm_disable_find_entry(BalsaMessage * bm);

static gboolean
bm_find_pass_to_entry(BalsaMessage * bm, GdkEvent * event)
{
    gboolean res = TRUE;
    guint keyval;
    GdkModifierType state;

    if (!gdk_event_get_keyval(event, &keyval) ||
        !gdk_event_get_state(event, &state))
        return FALSE;

    switch (keyval) {
    case GDK_KEY_Escape:
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        bm_disable_find_entry(bm);
        return res;
    case GDK_KEY_g:
        if ((state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) == GDK_CONTROL_MASK &&
            gtk_widget_get_sensitive(bm->find_next)) {
            bm_find_again(bm, bm->find_forward);
            return res;
        }
    default:
        break;
    }

    res = FALSE;
    if (gtk_widget_has_focus(bm->find_entry))
        g_signal_emit_by_name(bm->find_entry, "key-press-event", event,
                              &res, NULL);

    return res;
}

static void
bm_disable_find_entry(BalsaMessage * bm)
{
    g_signal_handlers_disconnect_by_func
        (gtk_widget_get_toplevel(GTK_WIDGET(bm)),
         G_CALLBACK(bm_find_pass_to_entry), bm);
    gtk_widget_hide(bm->find_bar);
}

/*
 * End of callbacks and helpers for the find bar.
 */

static void
balsa_message_init(BalsaMessage * bm)
{
    GtkStack  *stack;
    GtkWidget *vbox;
    GtkWidget *scroll;
    GtkWidget **buttons;
    GtkTreeStore *model;
    GtkGesture *gesture;
    GtkCellRenderer *renderer;
    GtkTreeSelection *selection;

    bm->switcher = gtk_stack_switcher_new();
    gtk_box_pack_start(GTK_BOX(bm), bm->switcher);

    bm->stack = gtk_stack_new();
    stack = GTK_STACK(bm->stack);
    gtk_stack_set_transition_type(stack,
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_UP_DOWN);
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(bm->switcher), stack);
    gtk_widget_set_vexpand(bm->stack, TRUE);
    gtk_box_pack_start(GTK_BOX(bm), bm->stack);

    /* Box to hold the scrolled window and the find bar */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_stack_add_titled(stack, vbox, "content", _("Content"));

    /* scrolled window for the contents */
    bm->scroll = scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    g_signal_connect(scroll, "key_press_event",
		     G_CALLBACK(balsa_mime_widget_key_press_event), bm);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), scroll);
    g_signal_connect(bm->scroll, "size-allocate",
		     G_CALLBACK(on_content_size_alloc), NULL);

    /* Widget to hold headers */
    buttons = bm_header_tl_buttons(bm);
    bm->bm_widget = balsa_mime_widget_new_message_tl(bm, buttons);
    g_free(buttons);

    /* Widget to hold message */
    g_signal_connect(G_OBJECT(bm->bm_widget->widget), "focus_in_event",
                     G_CALLBACK(balsa_mime_widget_limit_focus),
                     (gpointer) bm);
    g_signal_connect(G_OBJECT(bm->bm_widget->widget), "focus_out_event",
                     G_CALLBACK(balsa_mime_widget_unlimit_focus),
		     (gpointer) bm);
    gtk_container_add(GTK_CONTAINER(bm->scroll), bm->bm_widget->widget);

    /* structure view */
    model = gtk_tree_store_new (NUM_COLUMNS,
                                TYPE_BALSA_PART_INFO,
				G_TYPE_STRING,
                                GDK_TYPE_PIXBUF,
                                G_TYPE_STRING);
    bm->treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL(model));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (bm->treeview));
    g_signal_connect(bm->treeview, "row-activated",
                     G_CALLBACK(tree_activate_row_cb), bm);

    gesture = gtk_gesture_multi_press_new(GTK_WIDGET(bm->treeview));
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
    g_object_set_data_full(G_OBJECT(bm->treeview), "bm-gesture", gesture, g_object_unref);
    g_signal_connect(gesture, "pressed",
                     G_CALLBACK(bm_gesture_pressed_cb), NULL);

    g_signal_connect(bm->treeview, "popup-menu",
                     G_CALLBACK(tree_menu_popup_key_cb), bm);
    g_object_unref (G_OBJECT (model));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (bm->treeview), FALSE);

    /* column for the part number */
    renderer = gtk_cell_renderer_text_new ();
    g_object_set (G_OBJECT (renderer), "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (bm->treeview),
                                                 -1, NULL,
                                                 renderer, "text",
                                                 PART_NUM_COLUMN,
                                                 NULL);

    /* column for type icon */
    renderer = gtk_cell_renderer_pixbuf_new ();
    g_object_set (G_OBJECT (renderer), "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (bm->treeview),
                                                 -1, NULL,
                                                 renderer, "pixbuf",
                                                 MIME_ICON_COLUMN,
                                                 NULL);

    /* column for mime type */
    renderer = gtk_cell_renderer_text_new ();
    g_object_set (G_OBJECT (renderer), "xalign", 0.0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (bm->treeview),
                                                 -1, NULL,
                                                 renderer, "text",
                                                 MIME_TYPE_COLUMN,
                                                 NULL);

    /* scrolled window for the tree view */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);

    gtk_tree_view_set_expander_column
	(GTK_TREE_VIEW (bm->treeview), gtk_tree_view_get_column
	 (GTK_TREE_VIEW (bm->treeview), MIME_ICON_COLUMN - 1));

    gtk_stack_add_titled(stack, scroll, "parts", _("Message parts"));
    gtk_container_add(GTK_CONTAINER(scroll), bm->treeview);

    bm->current_part = NULL;
    bm->message = NULL;
    bm->info_count = 0;
    bm->save_all_list = NULL;
    bm->save_all_popup = NULL;

    bm->wrap_text = balsa_app.browse_wrap;
    bm->shown_headers = balsa_app.shown_headers;

    gtk_widget_show(GTK_WIDGET(bm));

    /* Find-in-message toolbar that is hidden by default. */
    bm->find_bar = bm_find_bar_new(bm);
    gtk_box_pack_start(GTK_BOX(vbox), bm->find_bar);
}

static void
balsa_message_destroy(GObject * object)
{
    BalsaMessage* bm = BALSA_MESSAGE(object);

    if (bm->treeview) {
        balsa_message_set(bm, NULL, 0);
        gtk_widget_destroy(bm->treeview);
        bm->treeview = NULL;
    }

    g_list_free(bm->save_all_list);
    bm->save_all_list = NULL;

    if (bm->save_all_popup) {
        g_object_unref(bm->save_all_popup);
	bm->save_all_popup = NULL;
    }

    if (bm->parts_popup) {
	g_object_unref(bm->parts_popup);
	bm->parts_popup = NULL;
    }

#ifdef HAVE_HTML_WIDGET
    if (bm->html_find_info) {
        g_free(bm->html_find_info);
        bm->html_find_info = NULL;
    }
#endif                          /* HAVE_HTML_WIDGET */

    if (G_OBJECT_CLASS(parent_class)->dispose)
        (*G_OBJECT_CLASS(parent_class)->dispose) (object);
}

GtkWidget *
balsa_message_new(void)
{
    BalsaMessage *bm;

    bm = g_object_new(BALSA_TYPE_MESSAGE,
                      "orientation", GTK_ORIENTATION_VERTICAL,
                      NULL);

    return GTK_WIDGET(bm);
}

/* Returns a BalsaPartInfo with a reference (g_object_unref when done). */
static BalsaPartInfo *
tree_next_valid_part_info(GtkTreeModel * model, GtkTreeIter * iter)
{
    BalsaPartInfo *info = NULL;

    do {
        GtkTreeIter child;

        /* check if there is a valid info */
        gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
        if (info)
            return info;

        /* if there are children, check the childs */
        if (gtk_tree_model_iter_children (model, &child, iter))
            if ((info = tree_next_valid_part_info(model, &child)))
                return info;

        /* switch to the next iter on the same level */
        if (!gtk_tree_model_iter_next(model, iter))
            return NULL;
    } while (1);
    /* never reached */
    return NULL;
}

static void
tree_activate_row_cb(GtkTreeView *treeview, GtkTreePath *arg1,
                     GtkTreeViewColumn *arg2, gpointer user_data)
{
    BalsaMessage * bm = (BalsaMessage *)user_data;
    GtkTreeModel * model = gtk_tree_view_get_model(treeview);
    GtkTreeIter sel_iter;
    BalsaPartInfo *info = NULL;

    g_return_if_fail(bm);

    /* get the info of the activated part */
    if (!gtk_tree_model_get_iter(model, &sel_iter, arg1))
        return;
    gtk_tree_model_get(model, &sel_iter, PART_INFO_COLUMN, &info, -1);

    /* if it's not displayable (== no info), get the next one... */
    if (!info) {
        info = tree_next_valid_part_info(model, &sel_iter);

        if (!info) {
            gtk_tree_model_get_iter_first(model, &sel_iter);
            gtk_tree_model_get(model, &sel_iter, PART_INFO_COLUMN, &info, -1);
            if (!info)
                info = tree_next_valid_part_info(model, &sel_iter);
        }
    }

    gtk_stack_set_visible_child_name(GTK_STACK(bm->stack), "content");
    select_part(bm, info);
    if (info)
        g_object_unref(info);
}

static void
collect_selected_info(GtkTreeModel * model, GtkTreePath * path,
                      GtkTreeIter * iter, gpointer data)
{
    GList **info_list = (GList **)data;
    BalsaPartInfo *info;

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
        g_object_unref(info);
        *info_list = g_list_append(*info_list, info);
    }
}

static void
tree_mult_selection_popup(BalsaMessage * bm, const GdkEvent * event,
                          GtkTreeSelection * selection)
{
    gint selected;

    /* destroy left-over select list and popup... */
    g_list_free(bm->save_all_list);
    bm->save_all_list = NULL;
    if (bm->save_all_popup) {
        g_object_unref(bm->save_all_popup);
        bm->save_all_popup = NULL;
    }

    /* collect all selected info blocks */
    gtk_tree_selection_selected_foreach(selection,
                                        collect_selected_info,
                                        &bm->save_all_list);

    /* For a single part, display it's popup, for multiple parts a "save all"
     * popup. If nothing with an info block is selected, do nothing */
    selected = g_list_length(bm->save_all_list);
    if (selected == 1) {
        BalsaPartInfo *info = BALSA_PART_INFO(bm->save_all_list->data);
        if (info->popup_menu) {
            if (event != NULL) {
                gtk_menu_popup_at_pointer(GTK_MENU(info->popup_menu), event);
            } else {
                gtk_menu_popup_at_widget(GTK_MENU(info->popup_menu),
                                         GTK_WIDGET(bm),
                                         GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                                         NULL);
            }
        }
        g_list_free(bm->save_all_list);
        bm->save_all_list = NULL;
    } else if (selected > 1) {
        GtkWidget *menu_item;

        bm->save_all_popup = gtk_menu_new ();
        g_object_ref_sink(bm->save_all_popup);
        menu_item =
            gtk_menu_item_new_with_label (_("Save selected as…"));
        gtk_widget_show(menu_item);
        g_signal_connect (G_OBJECT (menu_item), "activate",
                          G_CALLBACK (part_context_save_all_cb),
                          (gpointer) bm->save_all_list);
        gtk_menu_shell_append (GTK_MENU_SHELL (bm->save_all_popup), menu_item);
        menu_item =
            gtk_menu_item_new_with_label (_("Save selected to folder…"));
        gtk_widget_show(menu_item);
        g_signal_connect (G_OBJECT (menu_item), "activate",
                          G_CALLBACK (part_context_dump_all_cb),
                          (gpointer) bm->save_all_list);
        gtk_menu_shell_append (GTK_MENU_SHELL (bm->save_all_popup), menu_item);
        if (event)
            gtk_menu_popup_at_pointer(GTK_MENU(bm->save_all_popup),
                                      (GdkEvent *) event);
        else
            gtk_menu_popup_at_widget(GTK_MENU(bm->save_all_popup),
                                     GTK_WIDGET(bm),
                                     GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                                     NULL);
    }
}

static gboolean
tree_menu_popup_key_cb(GtkWidget *widget, gpointer user_data)
{
    BalsaMessage * bm = (BalsaMessage *)user_data;

    g_return_val_if_fail(bm, FALSE);
    tree_mult_selection_popup(bm, NULL,
                              gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));
    return TRUE;
}

static void
bm_gesture_pressed_cb(GtkGestureMultiPress *multi_press,
                      gint                  n_press,
                      gdouble               x,
                      gdouble               y,
                      gpointer              user_data)
{
    GtkGesture *gesture;
    const GdkEvent *event;
    GtkTreeView *tree_view;
    BalsaMessage * bm = (BalsaMessage *) user_data;
    GtkTreePath *path;

    gesture = GTK_GESTURE(multi_press);
    event = gtk_gesture_get_last_event(gesture, gtk_gesture_get_last_updated_sequence(gesture));
    g_return_if_fail(event != NULL);
    if (!gdk_event_triggers_context_menu(event))
        return;

    tree_view = GTK_TREE_VIEW(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));

    /* If the part which received the click is already selected, don't change
     * the selection and check if more than one part is selected. Pop up the
     * "save all" menu in this case and the "normal" popup otherwise.
     * If the receiving part is not selected, select (only) this part and pop
     * up its menu.
     */
    if (gtk_tree_view_get_path_at_pos(tree_view, (gint) x, (gint) y,
                                      &path, NULL, NULL, NULL)) {
        GtkTreeIter iter;
        GtkTreeSelection * selection =
            gtk_tree_view_get_selection(tree_view);
        GtkTreeModel * model = gtk_tree_view_get_model(tree_view);

        if (!gtk_tree_selection_path_is_selected(selection, path)) {
            BalsaPartInfo *info = NULL;

            gtk_tree_selection_unselect_all(selection);
            gtk_tree_selection_select_path(selection, path);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree_view), path, NULL,
                                     FALSE);
            if (gtk_tree_model_get_iter (model, &iter, path)) {
                gtk_tree_model_get(model, &iter, PART_INFO_COLUMN, &info, -1);
                if (info != NULL) {
                    if (info->popup_menu != NULL) {
                        gtk_menu_popup_at_pointer(GTK_MENU(info->popup_menu), event);
                    }
                    g_object_unref(info);
                }
            }
        } else
            tree_mult_selection_popup(bm, event, selection);
        gtk_tree_path_free(path);
    }
}

/* balsa_message_set:
   returns TRUE on success, FALSE on failure (message content could not be
   accessed).

   if msgno == 0, clears the display and returns TRUE
*/

/* Helpers:
 */

gchar *
balsa_message_sender_to_gchar(InternetAddressList * list, gint which)
{
    InternetAddress *ia;

    if (!list)
	return g_strdup(_("(No sender)"));
    if (which < 0)
	return internet_address_list_to_string(list, FALSE);
    ia = internet_address_list_get_address (list, which);
    return internet_address_to_string(ia, FALSE);
}

static void
balsa_message_clear_tree(BalsaMessage * bm)
{
    GtkTreeModel *model;

    g_return_if_fail(bm != NULL);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview));
    gtk_tree_store_clear(GTK_TREE_STORE(model));
    bm->info_count = 0;
}

gboolean
balsa_message_set(BalsaMessage * bm, LibBalsaMailbox * mailbox, guint msgno)
{
    gboolean is_new;
    GtkTreeIter iter;
    BalsaPartInfo *info;
    gboolean has_focus = bm->focus_state != BALSA_MESSAGE_FOCUS_STATE_NO;
    LibBalsaMessage *message;

    g_return_val_if_fail(bm != NULL, FALSE);

    bm_disable_find_entry(bm);
    balsa_message_clear_tree(bm);
    select_part(bm, NULL);
    if (bm->message != NULL) {
        libbalsa_message_body_unref(bm->message);
        g_object_unref(bm->message);
        bm->message = NULL;
    }

    if (mailbox == NULL || msgno == 0) {
        gtk_widget_hide(bm->switcher);
        gtk_stack_set_visible_child_name(GTK_STACK(bm->stack), "content");
        return TRUE;
    }

    bm->message = message = libbalsa_mailbox_get_message(mailbox, msgno);
    /* We must not use msgno from now on: an asynchronous expunge may
       arrive (in particular between the body_ref() and set_flags()
       actions) and change the message numbering. Asynchronous
       expunges will update the LibBalsaMailbox::message data but no
       message numbers stored in random integer variables. */
    if (!message) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not access message %u "
                            "in mailbox “%s”."),
			  msgno, mailbox->name);
        return FALSE;
    }

    is_new = LIBBALSA_MESSAGE_IS_UNREAD(message);
    if(!libbalsa_message_body_ref(message, TRUE, TRUE)) {
	libbalsa_mailbox_check(mailbox);
        g_object_unref(bm->message);
        bm->message = NULL;
	balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not access message %u "
                            "in mailbox “%s”."),
			  (unsigned int) message->msgno, mailbox->name);
        return FALSE;
    }

#ifdef HAVE_GPGME
    balsa_message_perform_crypto(message,
				 libbalsa_mailbox_get_crypto_mode(mailbox),
				 FALSE, 1);
    /* calculate the signature summary state if not set earlier */
    if(message->prot_state == LIBBALSA_MSG_PROTECT_NONE) {
        LibBalsaMsgProtectState prot_state =
            balsa_message_scan_signatures(message->body_list, message);
        /* update the icon if necessary */
        if (message->prot_state != prot_state)
            message->prot_state = prot_state;
    }
#endif

    /* may update the icon */
    libbalsa_mailbox_msgno_update_attach(mailbox, message->msgno, message);

    display_headers(bm);
    display_content(bm);
    gtk_widget_show(GTK_WIDGET(bm));

    if (bm->info_count > 1)
        gtk_widget_show(bm->switcher);
    else
        gtk_widget_hide(bm->switcher);
    gtk_stack_set_visible_child_name(GTK_STACK(bm->stack), "content");

    /*
     * At this point we check if (a) a message was new (its not new
     * any longer) and (b) a Disposition-Notification-To header line is
     * present.
     *
     */
    if (is_new && message->headers->dispnotify_to)
        handle_mdn_request (balsa_get_parent_window(GTK_WIDGET(bm)), message);

    if (!gtk_tree_model_get_iter_first (gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview)),
                                        &iter))
        /* Not possible? */
        return TRUE;

    info =
        tree_next_valid_part_info(gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview)),
                                  &iter);
    select_part(bm, info);
    if (info)
        g_object_unref(info);
    /*
     * emit read message
     */
    if (is_new && !mailbox->readonly)
        libbalsa_mailbox_msgno_change_flags(mailbox, message->msgno, 0,
                                            LIBBALSA_MESSAGE_FLAG_NEW);

    /* restore keyboard focus to the content, if it was there before */
    if (has_focus)
        balsa_message_grab_focus(bm);

    return TRUE;
}

void
balsa_message_save_current_part(BalsaMessage * bm)
{
    g_return_if_fail(bm != NULL);

    if (bm->current_part)
	balsa_mime_widget_ctx_menu_save(GTK_WIDGET(bm), bm->current_part->body);
}

static gboolean
balsa_message_set_embedded_hdr(GtkTreeModel * model, GtkTreePath * path,
                               GtkTreeIter *iter, gpointer data)
{
    BalsaPartInfo *info = NULL;
    BalsaMessage * bm = BALSA_MESSAGE(data);

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
 	if (info->body && info->body->embhdrs && info->mime_widget)
 	    balsa_mime_widget_message_set_headers_d(bm, info->mime_widget,
                                                    info->body->embhdrs,
                                                    info->body->parts,
                                                    info->body->embhdrs->subject);
	g_object_unref(G_OBJECT(info));
    }

    return FALSE;
}

void
balsa_message_set_displayed_headers(BalsaMessage * bmessage,
                                    ShownHeaders sh)
{
    g_return_if_fail(bmessage != NULL);
    g_return_if_fail(sh >= HEADERS_NONE && sh <= HEADERS_ALL);

    if (bmessage->shown_headers == sh)
        return;

    bmessage->shown_headers = sh;

    if (bmessage->message) {
        if(sh == HEADERS_ALL)
            libbalsa_mailbox_set_msg_headers(bmessage->message->mailbox,
                                             bmessage->message);
        display_headers(bmessage);
        gtk_tree_model_foreach
            (gtk_tree_view_get_model(GTK_TREE_VIEW(bmessage->treeview)),
             balsa_message_set_embedded_hdr, bmessage);
	if (bm_header_widget_att_button(bmessage)) {
	    if (bmessage->info_count > 1)
		gtk_widget_show
		    (GTK_WIDGET(bm_header_widget_att_button(bmessage)));
	    else
		gtk_widget_hide
		    (GTK_WIDGET(bm_header_widget_att_button(bmessage)));
	}
    }
}

void
balsa_message_set_wrap(BalsaMessage * bm, gboolean wrap)
{
    g_return_if_fail(bm != NULL);

    bm->wrap_text = wrap;

    /* This is easier than reformating all the widgets... */
    if (bm->message) {
        LibBalsaMessage *msg = bm->message;
        balsa_message_set(bm, msg->mailbox, msg->msgno);
    }
}


static void
display_headers(BalsaMessage * bm)
{
    balsa_mime_widget_message_set_headers_d(bm, bm->bm_widget,
                                            bm->message->headers,
                                            bm->message->body_list,
                                            LIBBALSA_MESSAGE_GET_SUBJECT(bm->message));
}


static void
part_info_init(BalsaMessage * bm, BalsaPartInfo * info)
{
    g_return_if_fail(bm != NULL);
    g_return_if_fail(info != NULL);
    g_return_if_fail(info->body != NULL);

    info->mime_widget = balsa_mime_widget_new(bm, info->body, info->popup_menu);
}


static inline gchar *
mpart_content_name(const gchar *content_type)
{
    if (g_ascii_strcasecmp(content_type, "multipart/mixed") == 0)
        return g_strdup(_("mixed parts"));
    else if (g_ascii_strcasecmp(content_type, "multipart/alternative") == 0)
        return g_strdup(_("alternative parts"));
    else if (g_ascii_strcasecmp(content_type, "multipart/signed") == 0)
        return g_strdup(_("signed parts"));
    else if (g_ascii_strcasecmp(content_type, "multipart/encrypted") == 0)
        return g_strdup(_("encrypted parts"));
    else if (g_ascii_strcasecmp(content_type, "message/rfc822") == 0)
        return g_strdup(_("RFC822 message"));
    else
        return g_strdup_printf(_("“%s” parts"),
                               strchr(content_type, '/') + 1);
}

static void
atattchments_menu_cb(GtkWidget * widget, BalsaPartInfo *info)
{
    BalsaMessage * bm = g_object_get_data(G_OBJECT(widget), "balsa-message");

    g_return_if_fail(bm);
    g_return_if_fail(info);

    gtk_stack_set_visible_child_name(GTK_STACK(bm->stack), "content");
    select_part(bm, info);
}

static void
add_to_attachments_popup(GtkMenuShell * menu, const gchar * item,
			 BalsaMessage * bm, BalsaPartInfo *info)
{
    GtkWidget * menuitem = gtk_menu_item_new_with_label (item);

    g_object_set_data(G_OBJECT(menuitem), "balsa-message", bm);
    g_signal_connect(G_OBJECT (menuitem), "activate",
		     G_CALLBACK (atattchments_menu_cb),
		     (gpointer) info);
    gtk_menu_shell_append(menu, menuitem);
}

static void
toggle_all_inline_cb(GtkCheckMenuItem * item, BalsaPartInfo *info)
{
    BalsaMessage * bm = g_object_get_data(G_OBJECT(item), "balsa-message");

    g_return_if_fail(bm);
    g_return_if_fail(info);

    bm->force_inline = gtk_check_menu_item_get_active(item);

    gtk_stack_set_visible_child_name(GTK_STACK(bm->stack), "content");
    select_part(bm, info);
}

static void
add_toggle_inline_menu_item(GtkMenuShell * menu, BalsaMessage * bm,
			    BalsaPartInfo *info)
{
    GtkWidget * menuitem =
	gtk_check_menu_item_new_with_label (_("force inline for all parts"));

    g_object_set_data(G_OBJECT(menuitem), "balsa-message", bm);
    g_signal_connect(G_OBJECT (menuitem), "activate",
		     G_CALLBACK (toggle_all_inline_cb),
		     (gpointer) info);
    gtk_menu_shell_append(menu, menuitem);

    /* Clear force-inline to be consistent with initial FALSE state of
     * check-menu-item. */
    bm->force_inline = FALSE;
}

static void
display_part(BalsaMessage * bm, LibBalsaMessageBody * body,
             GtkTreeModel * model, GtkTreeIter * iter, gchar * part_id)
{
    BalsaPartInfo *info = NULL;
    gchar *content_type = libbalsa_message_body_get_mime_type(body);
    gchar *icon_title = NULL;
    gboolean is_multipart=libbalsa_message_body_is_multipart(body);
    GdkPixbuf *content_icon;
    gchar *content_desc;

    content_desc = libbalsa_vfs_content_description(content_type);

    if(!is_multipart ||
       g_ascii_strcasecmp(content_type, "message/rfc822")==0 ||
       g_ascii_strcasecmp(content_type, "multipart/signed")==0 ||
       g_ascii_strcasecmp(content_type, "multipart/encrypted")==0 ||
       g_ascii_strcasecmp(content_type, "multipart/mixed")==0 ||
       g_ascii_strcasecmp(content_type, "multipart/alternative")==0) {

        info = balsa_part_info_new(body);
        bm->info_count++;

        if (g_ascii_strcasecmp(content_type, "message/rfc822") == 0 &&
            body->embhdrs) {
            gchar *from = balsa_message_sender_to_gchar(body->embhdrs->from, 0);
            gchar *subj = g_strdup(body->embhdrs->subject);
            libbalsa_utf8_sanitize(&from, balsa_app.convert_unknown_8bit, NULL);
            libbalsa_utf8_sanitize(&subj, balsa_app.convert_unknown_8bit, NULL);
            icon_title =
                g_strdup_printf(_("RFC822 message (from %s, subject “%s”)"),
                                from, subj);
            g_free(from);
            g_free(subj);
        } else if (is_multipart) {
            icon_title = mpart_content_name(content_type);
	    if (!strcmp(part_id, "1")) {
		add_toggle_inline_menu_item(GTK_MENU_SHELL(bm->parts_popup),
					    bm, info);
		gtk_menu_shell_append(GTK_MENU_SHELL(bm->parts_popup),
				      gtk_separator_menu_item_new ());
		add_to_attachments_popup(GTK_MENU_SHELL(bm->parts_popup),
					 _("complete message"),
					 bm, info);
		gtk_menu_shell_append(GTK_MENU_SHELL(bm->parts_popup),
				      gtk_separator_menu_item_new ());
	    }
        } else if (body->filename) {
            gchar * filename = g_strdup(body->filename);
	    gchar * menu_label;

            libbalsa_utf8_sanitize(&filename, balsa_app.convert_unknown_8bit,
                                   NULL);
            icon_title =
                g_strdup_printf("%s (%s)", filename, content_desc);

	    /* this should neither be a message nor multipart, so add it to the
	       attachments popup */
	    menu_label =
		g_strdup_printf(_("part %s: %s (file %s)"), part_id,
				content_desc, filename);
	    add_to_attachments_popup(GTK_MENU_SHELL(bm->parts_popup),
				     menu_label, bm, info);
	    g_free(menu_label);
            g_free(filename);
        } else {
	    gchar * menu_label;

            icon_title = g_strdup_printf("%s", content_desc);
	    menu_label =
		g_strdup_printf(_("part %s: %s"), part_id, content_desc);
	    add_to_attachments_popup(GTK_MENU_SHELL(bm->parts_popup),
				     menu_label, bm, info);
	    g_free(menu_label);
	}

        part_create_menu (info);
        info->path = gtk_tree_model_get_path(model, iter);

        /* add to the tree view */
#ifdef HAVE_GPGME
        content_icon =
	    get_crypto_content_icon(body, content_type, &icon_title);
	if (info->body->was_encrypted) {
	    gchar * new_title =
		g_strconcat(_("encrypted: "), icon_title, NULL);
	    g_free(icon_title);
	    icon_title = new_title;
	}
#else
	content_icon = NULL;
#endif
        if (!content_icon)
	    content_icon =
		libbalsa_icon_finder(GTK_WIDGET(bm), content_type, NULL, NULL, 24);
        gtk_tree_store_set (GTK_TREE_STORE(model), iter,
                            PART_INFO_COLUMN, info,
			    PART_NUM_COLUMN, part_id,
                            MIME_ICON_COLUMN, content_icon,
                            MIME_TYPE_COLUMN, icon_title, -1);

        g_object_unref(info);
        g_free(icon_title);
    } else {
	content_icon =
	    libbalsa_icon_finder(GTK_WIDGET(bm), content_type, NULL, NULL, 24);
        gtk_tree_store_set (GTK_TREE_STORE(model), iter,
                            PART_INFO_COLUMN, NULL,
			    PART_NUM_COLUMN, part_id,
                            MIME_ICON_COLUMN, content_icon,
                            MIME_TYPE_COLUMN, content_desc, -1);
    }

    if (content_icon)
	g_object_unref(G_OBJECT(content_icon));
    g_free(content_desc);
    g_free(content_type);
}

static void
display_parts(BalsaMessage * bm, LibBalsaMessageBody * body,
              GtkTreeIter * parent, gchar * prefix)
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview));
    GtkTreeIter iter;
    gint part_in_level = 1;

    while (body) {
	gchar * part_id;

	if (prefix)
	    part_id = g_strdup_printf("%s.%d", prefix, part_in_level);
	else
	    part_id = g_strdup_printf("%d", part_in_level);
        gtk_tree_store_append(GTK_TREE_STORE(model), &iter, parent);
        display_part(bm, body, model, &iter, part_id);
        display_parts(bm, body->parts, &iter, part_id);
        body = body->next;
	part_in_level++;
	g_free(part_id);
    }
}

/* Display the image in a "Face:" header, if any. */
static void
display_face(BalsaMessage * bm)
{
    GtkWidget *face_box;
    const gchar *face, *x_face = NULL;
    GError *err = NULL;
    GtkWidget *image;

    face_box = bm->face_box;
    gtk_widget_hide(face_box);
    gtk_container_foreach(GTK_CONTAINER(face_box),
                          (GtkCallback) gtk_widget_destroy, NULL);

    if (!bm->message
        || !((face = libbalsa_message_get_user_header(bm->message, "Face"))
             || (x_face =
                 libbalsa_message_get_user_header(bm->message,
                                                  "X-Face")))) {
        return;
    }

    if (face)
        image = libbalsa_get_image_from_face_header(face, &err);
    else {
#if HAVE_COMPFACE
        image = libbalsa_get_image_from_x_face_header(x_face, &err);
#else                           /* HAVE_COMPFACE */
        return;
#endif                          /* HAVE_COMPFACE */
    }
    if (err) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                /* Translators: please do not translate Face. */
                          _("Error loading Face: %s"), err->message);
        g_error_free(err);
        return;
    }

    gtk_box_pack_start(GTK_BOX(face_box), image);
    gtk_widget_show(face_box);
}

static void
display_content(BalsaMessage * bm)
{
    balsa_message_clear_tree(bm);
    if (bm->parts_popup)
	g_object_unref(bm->parts_popup);
    bm->parts_popup = gtk_menu_new();
    g_object_ref_sink(bm->parts_popup);
    display_parts(bm, bm->message->body_list, NULL, NULL);
    if (bm->info_count > 1) {
 	gtk_widget_show(bm->parts_popup);
 	gtk_widget_show
	    (GTK_WIDGET(bm_header_widget_att_button(bm)));
    } else
 	gtk_widget_hide
	    (GTK_WIDGET(bm_header_widget_att_button(bm)));
    display_face(bm);
    gtk_tree_view_columns_autosize(GTK_TREE_VIEW(bm->treeview));
    gtk_tree_view_expand_all(GTK_TREE_VIEW(bm->treeview));
}

void
balsa_message_copy_part(const gchar *url, LibBalsaMessageBody *part)
{
    GError *err = NULL;
    LibBalsaMailbox *mailbox = balsa_find_mailbox_by_url(url);
    GMimeStream *stream;

    g_return_if_fail(mailbox != NULL);

    stream = libbalsa_message_body_get_stream(part, &err);

    if (!stream) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Reading embedded message failed: %s"),
			     err ? err->message : "?");
	g_clear_error(&err);
	return;
    }

    if (!libbalsa_mailbox_add_message(mailbox, stream, 0, &err)) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Appending message to %s failed: %s"),
			     mailbox->name,
			     err ? err->message : "?");
	g_clear_error(&err);
    }
    g_object_unref(stream);
}

static void
part_create_menu (BalsaPartInfo* info)
/* Remarks: Will add items in the following order:
            1) Default application according to GnomeVFS.
            2) GNOME MIME/GnomeVFS key values that don't match default
               application or anything on the shortlist.
            3) GnomeVFS shortlist applications, with the default one (sometimes
               included on shortlist, sometimes not) excluded. */
{
    GtkWidget* menu_item;
    gchar* content_type;

    info->popup_menu = gtk_menu_new ();
    g_object_ref_sink(info->popup_menu);

    content_type = libbalsa_message_body_get_mime_type (info->body);
    libbalsa_vfs_fill_menu_by_content_type(GTK_MENU(info->popup_menu),
					   content_type,
					   G_CALLBACK (balsa_mime_widget_ctx_menu_cb),
					   (gpointer)info->body);

    menu_item = gtk_menu_item_new_with_mnemonic (_("_Save…"));
    g_signal_connect (G_OBJECT (menu_item), "activate",
                      G_CALLBACK (balsa_mime_widget_ctx_menu_save), (gpointer) info->body);
    gtk_menu_shell_append (GTK_MENU_SHELL (info->popup_menu), menu_item);

    if (g_ascii_strcasecmp(content_type, "message/rfc822") == 0) {
        GtkWidget *submenu;

        menu_item =
            gtk_menu_item_new_with_mnemonic(_("_Copy to folder…"));
        gtk_menu_shell_append(GTK_MENU_SHELL(info->popup_menu), menu_item);

        submenu =
            balsa_mblist_mru_menu(GTK_WINDOW(gtk_widget_get_toplevel(info->popup_menu)),
                                  &balsa_app.folder_mru,
                                  G_CALLBACK(balsa_message_copy_part),
                                  info->body);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), submenu);
    }

    gtk_widget_show (info->popup_menu);
    g_free (content_type);
}


static void
balsa_part_info_init(GObject *object, gpointer data)
{
    BalsaPartInfo * info = BALSA_PART_INFO(object);

    info->body = NULL;
    info->mime_widget = NULL;
    info->popup_menu = NULL;
    info->path = NULL;
}

static BalsaPartInfo*
balsa_part_info_new(LibBalsaMessageBody* body)
{
    BalsaPartInfo * info = g_object_new(TYPE_BALSA_PART_INFO, NULL);
    info->body = body;
    return info;
}

static void
balsa_part_info_free(GObject * object)
{
    BalsaPartInfo * info;
    GObjectClass *part_info_parent_class;

    g_return_if_fail(object != NULL);
    g_return_if_fail(IS_BALSA_PART_INFO(object));
    info = BALSA_PART_INFO(object);

    if (info->mime_widget) {
	g_object_unref(G_OBJECT(info->mime_widget));
	info->mime_widget = NULL;
    }
    if (info->popup_menu)
        g_object_unref(info->popup_menu);

    gtk_tree_path_free(info->path);

    part_info_parent_class = g_type_class_peek_parent(G_OBJECT_GET_CLASS(object));
    part_info_parent_class->finalize(object);
}

static void
part_context_save_all_cb(GtkWidget * menu_item, GList * info_list)
{
    while (info_list) {
	balsa_mime_widget_ctx_menu_save(menu_item,
                                        BALSA_PART_INFO(info_list->data)->body);
        info_list = info_list->next;
    }
}

/*
 * Let the user select a folder and save all message parts form info_list in
 * this folder, either with their name (if defined) or as localised "content-
 * type message part". The function protects files from being overwritten by
 * appending "(1)", "(2)", ... to the name to make it unique.
 * Sets balsa_app::save_dir to the selected folder.
 */
static void
part_context_dump_all_cb(GtkWidget * menu_item, GList * info_list)
{
    GtkWidget *dump_dialog;

    g_return_if_fail(info_list);

    dump_dialog =
        gtk_file_chooser_dialog_new(_("Select folder for saving selected parts"),
                                    balsa_get_parent_window(menu_item),
                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_OK"),     GTK_RESPONSE_OK,
                                    NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dump_dialog, balsa_get_parent_window(menu_item));
#endif
    gtk_dialog_set_default_response(GTK_DIALOG(dump_dialog),
                                    GTK_RESPONSE_CANCEL);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dump_dialog),
                                    libbalsa_vfs_local_only());
    if (balsa_app.save_dir)
        gtk_file_chooser_set_current_folder_uri(GTK_FILE_CHOOSER(dump_dialog),
                                                balsa_app.save_dir);

    if (gtk_dialog_run(GTK_DIALOG(dump_dialog)) == GTK_RESPONSE_OK) {
	gchar *dir_name =
            gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(dump_dialog));
        LibbalsaVfs * dir_uri;

        g_message("store to URI: %s", dir_name);
        if (!(dir_uri = libbalsa_vfs_new_from_uri(dir_name)))
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Could not create URI for %s"),
                              dir_name);

	/* remember the folder */
	g_free(balsa_app.save_dir);
	balsa_app.save_dir = dir_name;

	/* save all parts without further user interaction */
	info_list = g_list_first(info_list);
	while (dir_uri && info_list) {
	    BalsaPartInfo *info = BALSA_PART_INFO(info_list->data);
            LibbalsaVfs * save_uri;
	    gboolean result;
            GError *err = NULL;

	    if (info->body->filename)
		save_uri =
		    libbalsa_vfs_dir_append(dir_uri, info->body->filename);
	    else {
		gchar *cont_type =
		    libbalsa_message_body_get_mime_type(info->body);
		gchar *p;

		/* be sure to have no '/' in the file name */
		g_strdelimit(cont_type, G_DIR_SEPARATOR_S, '-');
		p = g_strdup_printf(_("%s message part"), cont_type);
		g_free(cont_type);
		save_uri = libbalsa_vfs_dir_append(dir_uri, p);
		g_free(p);
	    }
            g_message("store to file: %s", libbalsa_vfs_get_uri_utf8(save_uri));

	    /* don't overwrite existing files, append (1), (2), ... instead */
	    if (libbalsa_vfs_file_exists(save_uri)) {
		gint n = 1;
		LibbalsaVfs * base_uri = save_uri;

		save_uri = NULL;
		do {
                    gchar * ext = g_strdup_printf(" (%d)", n++);
		    if (save_uri)
                        g_object_unref(save_uri);
                    save_uri = libbalsa_vfs_append(base_uri, ext);
                    g_free(ext);
		} while (libbalsa_vfs_file_exists(save_uri));
		g_object_unref(base_uri);
	    }
            g_message("store to file: %s", libbalsa_vfs_get_uri_utf8(save_uri));

	    /* try to save the file */
            result =
                libbalsa_message_body_save_vfs(info->body, save_uri,
                                               LIBBALSA_MESSAGE_BODY_UNSAFE,
                                               info->body->body_type ==
                                               LIBBALSA_MESSAGE_BODY_TYPE_TEXT,
                                               &err);
	    if (!result)
		balsa_information(LIBBALSA_INFORMATION_ERROR,
				  _("Could not save %s: %s"),
				  libbalsa_vfs_get_uri_utf8(save_uri),
                                  err && err->message ?
                                  err->message : "Unknown error");
            g_clear_error(&err);
	    g_object_unref(save_uri);
	    info_list = info_list->next;
	}
	g_object_unref(dir_uri);
    }
    gtk_widget_destroy(dump_dialog);
}


typedef struct _selFirst_T {
    GtkTreeIter sel_iter;
    gboolean found;
} selFirst_T;

static void
tree_selection_get_first(GtkTreeModel * model, GtkTreePath * path,
                         GtkTreeIter * iter, gpointer data)
{
    selFirst_T *sel = (selFirst_T *)data;

    if (!sel->found) {
        sel->found = TRUE;
        memcpy (&sel->sel_iter, iter, sizeof(GtkTreeIter));
    }
}

static BalsaPartInfo *
bm_next_part_info(BalsaMessage * bmessage)
{
    selFirst_T sel;
    GtkTreeView *gtv;
    GtkTreeModel *model;

    g_return_val_if_fail(bmessage != NULL, NULL);
    g_return_val_if_fail(bmessage->treeview != NULL, NULL);

    gtv = GTK_TREE_VIEW(bmessage->treeview);
    model = gtk_tree_view_get_model(gtv);

    /* get the info of the first selected part */
    sel.found = FALSE;
    gtk_tree_selection_selected_foreach(gtk_tree_view_get_selection(gtv),
                                        tree_selection_get_first, &sel);
    if (!sel.found) {
        /* return the first part if nothing is selected */
        if (!gtk_tree_model_get_iter_first(model, &sel.sel_iter))
            return NULL;
    } else {
        GtkTreeIter iter;

        /* If the first selected iter has a child, select it, otherwise
         * take next on the same or higher level.  If there is no next,
         * return NULL */
        if (!gtk_tree_model_iter_children (model, &iter, &sel.sel_iter)) {
            GtkTreeIter tmp_iter;

            tmp_iter = iter = sel.sel_iter;
            while (!gtk_tree_model_iter_next (model, &iter)) {
                if (!gtk_tree_model_iter_parent(model, &iter, &tmp_iter))
                    return NULL;
	        tmp_iter = iter;
            }
        }
        sel.sel_iter = iter;
    }

    return tree_next_valid_part_info(model, &sel.sel_iter);
}

void
balsa_message_next_part(BalsaMessage * bmessage)
{
    BalsaPartInfo *info;
    GtkTreeView *gtv;

    if (!(info = bm_next_part_info(bmessage)))
	return;

    gtv = GTK_TREE_VIEW(bmessage->treeview);
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(gtv));
    select_part(bmessage, info);
    g_object_unref(info);
}

gboolean
balsa_message_has_next_part(BalsaMessage * bmessage)
{
    BalsaPartInfo *info;

    if (bmessage && bmessage->treeview
        && (info = bm_next_part_info(bmessage))) {
        g_object_unref(info);
        return TRUE;
    }

    return FALSE;
}

static BalsaPartInfo *
bm_previous_part_info(BalsaMessage * bmessage)
{
    selFirst_T sel;
    GtkTreeView *gtv;
    GtkTreeModel *model;
    BalsaPartInfo *info;

    g_return_val_if_fail(bmessage != NULL, NULL);
    g_return_val_if_fail(bmessage->treeview != NULL, NULL);

    gtv = GTK_TREE_VIEW(bmessage->treeview);
    model = gtk_tree_view_get_model(gtv);

    /* get the info of the first selected part */
    sel.found = FALSE;
    gtk_tree_selection_selected_foreach(gtk_tree_view_get_selection(gtv),
                                        tree_selection_get_first, &sel);
    if (!sel.found) {
        /* return the first part if nothing is selected */
        if (!gtk_tree_model_get_iter_first(model, &sel.sel_iter))
            return NULL;
        gtk_tree_model_get(model, &sel.sel_iter, PART_INFO_COLUMN, &info, -1);
    } else {
        GtkTreePath * path = gtk_tree_model_get_path(model, &sel.sel_iter);

        /* find the previous element with a valid info block */
        do {
            if (!gtk_tree_path_prev (path)) {
                if (gtk_tree_path_get_depth (path) <= 1) {
                    gtk_tree_path_free(path);
                    return NULL;
                }
                gtk_tree_path_up(path);
            }
            gtk_tree_model_get_iter(model, &sel.sel_iter, path);
            gtk_tree_model_get(model, &sel.sel_iter, PART_INFO_COLUMN,
                               &info, -1);
        } while (!info);
        gtk_tree_path_free(path);
    }

    return info;
}

void
balsa_message_previous_part(BalsaMessage * bmessage)
{
    BalsaPartInfo *info;
    GtkTreeView *gtv;

    if (!(info = bm_previous_part_info(bmessage)))
	return;

    gtv = GTK_TREE_VIEW(bmessage->treeview);
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(gtv));
    select_part(bmessage, info);
    g_object_unref(info);
}

gboolean
balsa_message_has_previous_part(BalsaMessage * bmessage)
{
    BalsaPartInfo *info;

    if (bmessage && bmessage->treeview
        && (info = bm_previous_part_info(bmessage))) {
        g_object_unref(info);
        return TRUE;
    }

    return FALSE;
}

#ifdef HAVE_HTML_WIDGET
static gboolean
libbalsa_can_display(LibBalsaMessageBody *part)
{
    gchar *content_type = libbalsa_message_body_get_mime_type(part);
    gboolean res = FALSE;
    if (!balsa_app.display_alt_plain &&
	libbalsa_html_type(content_type))
	res = TRUE;
    else if(g_ascii_strcasecmp(content_type, "multipart/related") == 0 &&
	    part->parts)
	res = libbalsa_can_display(part->parts);
    g_free(content_type);
    return res;
}
#endif                          /* HAVE_HTML_WIDGET */

/** Determines whether given part can be displayed. We display plain
   text, parts html/rtf parts unless it has been disabled in the
   preferences. We make sure the process is correctly recurrsive, to
   display properly messages of following structure:

   multiplart/alternative
     text/plain "A"
     multipart/related
       text/plain "B"
       image/jpeg "C"

   In the case as above, B & C should be displayed.
*/
static LibBalsaMessageBody*
preferred_part(LibBalsaMessageBody *parts)
{
    LibBalsaMessageBody *body, *preferred = parts;

    for (body = parts; body; body = body->next) {
        gchar *content_type;

        content_type = libbalsa_message_body_get_mime_type(body);

        if (g_ascii_strcasecmp(content_type, "text/plain") == 0 ||
            g_ascii_strcasecmp(content_type, "text/calendar") == 0)
            preferred = body;
#ifdef HAVE_HTML_WIDGET
        else if (libbalsa_can_display(body))
            preferred = body;
#endif                          /* HAVE_HTML_WIDGET */

        g_free(content_type);
    }

    return preferred;
}

typedef struct _treeSearchT {
    const LibBalsaMessageBody *body;
    BalsaPartInfo *info;
} treeSearchT;

static gboolean
treeSearch_Func(GtkTreeModel * model, GtkTreePath *path,
                GtkTreeIter * iter, gpointer data)
{
    treeSearchT *search = (treeSearchT *)data;
    BalsaPartInfo *info = NULL;

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
        if (info->body == search->body) {
            search->info = info;
            return TRUE;
        } else
            g_object_unref(info);
    }

    return FALSE;
}

static BalsaPartInfo *
part_info_from_body(BalsaMessage *bm, const LibBalsaMessageBody *body)
{
    treeSearchT search;

    search.body = body;
    search.info = NULL;

    gtk_tree_model_foreach
        (gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview)),
         treeSearch_Func, &search);
    return search.info;
}


static LibBalsaMessageBody *
add_body(BalsaMessage * bm, LibBalsaMessageBody * body,
         GtkWidget * container)
{
    if(body) {
        BalsaPartInfo *info = part_info_from_body(bm, body);

        if (info) {
	    body = add_part(bm, info, container);
            g_object_unref(info);
        } else
	    body = add_multipart(bm, body, container);
    }

    return body;
}

static LibBalsaMessageBody *
add_multipart_digest(BalsaMessage * bm, LibBalsaMessageBody * body,
                     GtkWidget * container)
{
    LibBalsaMessageBody *retval = NULL;
    /* Add all parts */
    retval = add_body(bm, body, container);
    for (body = body->next; body; body = body->next)
        add_body(bm, body, container);

    return retval;
}

static LibBalsaMessageBody *
add_multipart_mixed(BalsaMessage * bm, LibBalsaMessageBody * body,
                    GtkWidget * container)
{
    LibBalsaMessageBody * retval = NULL;
    /* Add first (main) part + anything else with
       Content-Disposition: inline */
    if (body) {
        retval = add_body(bm, body, container);
        for (body = body->next; body; body = body->next) {
#ifdef HAVE_GPGME
	    GMimeContentType *type =
		g_mime_content_type_new_from_string(body->content_type);

            if (libbalsa_message_body_is_inline(body) ||
		bm->force_inline ||
                libbalsa_message_body_is_multipart(body) ||
		g_mime_content_type_is_type(type, "application", "pgp-signature") ||
		(balsa_app.has_smime &&
		 (g_mime_content_type_is_type(type, "application", "pkcs7-signature") ||
		  g_mime_content_type_is_type(type, "application", "x-pkcs7-signature"))))
                add_body(bm, body, container);
	    g_object_unref(type);
#else
            if (libbalsa_message_body_is_inline(body) ||
		bm->force_inline ||
		libbalsa_message_body_is_multipart(body))
                add_body(bm, body, container);
#endif
        }
    }

    return retval;
}

static LibBalsaMessageBody *
add_multipart(BalsaMessage *bm, LibBalsaMessageBody *body,
              GtkWidget * container)
/* This function handles multiparts as specified by RFC2046 5.1 and
 * message/rfc822 types. */
{
    GMimeContentType *type;

    if (!body->parts)
	return body;

    type=g_mime_content_type_new_from_string(body->content_type);

    if (g_mime_content_type_is_type(type, "multipart", "related")) {
        /* FIXME: more processing required see RFC1872 */
        /* Add the first part */
        body = add_body(bm, body->parts, container);
    } else if (g_mime_content_type_is_type(type, "multipart", "alternative")) {
            /* Add the most suitable part. */
        body = add_body(bm, preferred_part(body->parts), container);
    } else if (g_mime_content_type_is_type(type, "multipart", "digest")) {
	body = add_multipart_digest(bm, body->parts, container);
    } else { /* default to multipart/mixed */
	body = add_multipart_mixed(bm, body->parts, container);
    }
    g_object_unref(type);

    return body;
}

static LibBalsaMessageBody *
add_part(BalsaMessage * bm, BalsaPartInfo * info, GtkWidget * container)
{
    GtkTreeSelection *selection;
    GtkWidget *widget;
    LibBalsaMessageBody *body;

    if (!info)
	return NULL;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(bm->treeview));

    if (info->path &&
	!gtk_tree_selection_path_is_selected(selection, info->path))
	gtk_tree_selection_select_path(selection, info->path);

    if (info->mime_widget == NULL)
	part_info_init(bm, info);

    if ((widget = info->mime_widget->widget)) {
        gtk_widget_set_vexpand(widget, TRUE);
        gtk_box_pack_start(GTK_BOX(container), widget);
    }

    body =
        add_multipart(bm, info->body,
                      info->mime_widget->container ?
                      info->mime_widget->container : container);

    return body;
}


static gboolean
gtk_tree_hide_func(GtkTreeModel * model, GtkTreePath * path,
                   GtkTreeIter * iter, gpointer data)
{
    BalsaPartInfo *info;

    gtk_tree_model_get(model, iter, PART_INFO_COLUMN, &info, -1);
    if (info) {
        GtkWidget *widget, *parent;

        if (info->mime_widget && (widget = info->mime_widget->widget)
            && (parent = gtk_widget_get_parent(widget)))
            gtk_container_remove(GTK_CONTAINER(parent), widget);
        g_object_unref(info);
    }

    return FALSE;
}

static void
hide_all_parts(BalsaMessage * bm)
{
    if (bm->current_part) {
	gtk_tree_model_foreach(gtk_tree_view_get_model
			       (GTK_TREE_VIEW(bm->treeview)),
			       gtk_tree_hide_func, bm);
	gtk_tree_selection_unselect_all(gtk_tree_view_get_selection
					(GTK_TREE_VIEW(bm->treeview)));
        g_object_unref(bm->current_part);
	bm->current_part = NULL;
    }

    gtk_container_foreach(GTK_CONTAINER(bm->bm_widget->container),
                          (GtkCallback) gtk_widget_destroy, NULL);
}

/*
 * If part == -1 then change to no part
 * must release selection before hiding a text widget.
 */
static void
select_part(BalsaMessage * bm, BalsaPartInfo *info)
{
    LibBalsaMessageBody *body;

    hide_all_parts(bm);
    bm_disable_find_entry(bm);

    body = add_part(bm, info, bm->bm_widget->container);
    bm->current_part = part_info_from_body(bm, body);

    g_signal_emit(G_OBJECT(bm), balsa_message_signals[SELECT_PART], 0);

    if (body != NULL) {
        GtkAdjustment *hadj, *vadj;

        g_object_get(G_OBJECT(bm->scroll), "hadjustment", &hadj,
                                           "vadjustment", &vadj, NULL);
        gtk_adjustment_set_value(hadj, 0);
        gtk_adjustment_set_value(vadj, 0);
    }
}

GtkWidget *
balsa_message_current_part_widget(BalsaMessage * bmessage)
{
    if (bmessage && bmessage->current_part &&
	bmessage->current_part->mime_widget)
	return bmessage->current_part->mime_widget->widget;
    else
	return NULL;
}

GtkWindow *
balsa_get_parent_window(GtkWidget * widget)
{
    if (widget) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(widget);

        if (gtk_widget_is_toplevel(toplevel) && GTK_IS_WINDOW(toplevel))
            return GTK_WINDOW(toplevel);
    }

    return GTK_WINDOW(balsa_app.main_window);
}


/*
 * This function informs the caller if the currently selected part
 * supports selection/copying etc.
 */
gboolean
balsa_message_can_select(BalsaMessage * bmessage)
{
    GtkWidget *w;

    g_return_val_if_fail(bmessage != NULL, FALSE);

    if (bmessage->current_part == NULL
        || (w = bmessage->current_part->mime_widget->widget) == NULL)
        return FALSE;

    return GTK_IS_EDITABLE(w) || GTK_IS_TEXT_VIEW(w)
#ifdef    HAVE_HTML_WIDGET
        || libbalsa_html_can_select(w)
#endif /* HAVE_HTML_WIDGET */
        ;
}

gboolean
balsa_message_grab_focus(BalsaMessage * bmessage)
{
    GtkWidget *widget;

    g_return_val_if_fail(bmessage != NULL, FALSE);
    g_return_val_if_fail(bmessage->current_part != NULL, FALSE);

    widget = bmessage->current_part->mime_widget->widget;
    g_return_val_if_fail(widget != NULL, FALSE);

    gtk_widget_set_can_focus(widget, TRUE);
    gtk_widget_grab_focus(widget);

    return TRUE;
}

static InternetAddress *
bm_get_mailbox(InternetAddressList * list)
{
    InternetAddress *ia;

    if (!list)
	return NULL;

    ia = internet_address_list_get_address (list, 0);
    if (!ia)
	return NULL;

    while (ia && INTERNET_ADDRESS_IS_GROUP (ia))
	ia = internet_address_list_get_address (INTERNET_ADDRESS_GROUP (ia)->members, 0);
    if (!ia)
	return NULL;

    return ia;
}

static void
handle_mdn_request(GtkWindow *parent, LibBalsaMessage *message)
{
    gboolean suspicious;
    InternetAddressList *use_from;
    InternetAddressList *list;
    InternetAddress *from, *dn;
    BalsaMDNReply action;
    LibBalsaMessage *mdn;
    LibBalsaIdentity *mdn_ident = NULL;
    gint i, len;

    /* Check if the dispnotify_to address is equal to the (in this order,
       if present) reply_to, from or sender address. */
    if (message->headers->reply_to)
        use_from = message->headers->reply_to;
    else if (message->headers->from)
        use_from = message->headers->from;
    else if (message->sender)
        use_from = message->sender;
    else
        use_from = NULL;
    /* note: neither Disposition-Notification-To: nor Reply-To:, From: or
       Sender: may be address groups */
    from = use_from ? internet_address_list_get_address (use_from, 0) : NULL;
    dn = internet_address_list_get_address (message->headers->dispnotify_to, 0);
    suspicious = !libbalsa_ia_rfc2821_equal(dn, from);

    /* Try to find "my" identity first in the to, then in the cc list */
    list = message->headers->to_list;
    len = list ? internet_address_list_length(list) : 0;
    for (i = 0; i < len && !mdn_ident; i++) {
        GList * id_list;

        for (id_list = balsa_app.identities; !mdn_ident && id_list;
             id_list = id_list->next) {
            LibBalsaIdentity *ident = LIBBALSA_IDENTITY(id_list->data);

            if (libbalsa_ia_rfc2821_equal(ident->ia, bm_get_mailbox(list)))
                mdn_ident = ident;
        }
    }

    list = message->headers->cc_list;
    for (i = 0; i < len && !mdn_ident; i++) {
        GList * id_list;

        for (id_list = balsa_app.identities; !mdn_ident && id_list;
             id_list = id_list->next) {
            LibBalsaIdentity *ident = LIBBALSA_IDENTITY(id_list->data);

            if (libbalsa_ia_rfc2821_equal(ident->ia, bm_get_mailbox(list)))
                mdn_ident = ident;
        }
    }


    /* Now we decide from the settings of balsa_app.mdn_reply_[not]clean what
       to do...
    */
    if (suspicious || !mdn_ident)
        action = balsa_app.mdn_reply_notclean;
    else
        action = balsa_app.mdn_reply_clean;
    if (action == BALSA_MDN_REPLY_NEVER)
        return;

    /* fall back to the current identity if the requested one is empty */
    if (!mdn_ident)
        mdn_ident = balsa_app.current_ident;

    /* We *may* send a reply, so let's create a message for that... */
    mdn = create_mdn_reply (mdn_ident, message, action == BALSA_MDN_REPLY_ASKME);

    /* if the user wants to be asked, display a dialog, otherwise send... */
    if (action == BALSA_MDN_REPLY_ASKME) {
        gchar *sender;
        gchar *reply_to;
        sender = from ? internet_address_to_string (from, FALSE) : NULL;
        reply_to =
            internet_address_list_to_string (message->headers->dispnotify_to,
		                             FALSE);
        gtk_widget_show (create_mdn_dialog (parent, sender, reply_to, mdn,
                                                mdn_ident));
        g_free (reply_to);
        g_free (sender);
    } else {
	GError * error = NULL;
	LibBalsaMsgCreateResult result;

        result = libbalsa_message_send(mdn, balsa_app.outbox, NULL,
				       balsa_find_sentbox_by_url,
				       mdn_ident->smtp_server,
                                       parent,
				       TRUE, &error);
	if (result != LIBBALSA_MESSAGE_CREATE_OK)
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Sending the disposition notification failed: %s"),
				 error ? error->message : "?");
	g_error_free(error);
        g_object_unref(G_OBJECT(mdn));
    }
}

static LibBalsaMessage *create_mdn_reply (const LibBalsaIdentity *mdn_ident,
                                          LibBalsaMessage *for_msg,
                                          gboolean manual)
{
    LibBalsaMessage *message;
    LibBalsaMessageBody *body;
    gchar *date, *dummy;
    GString *report;
    gchar **params;
    struct utsname uts_name;
    const gchar *original_rcpt;

    /* create a message with the header set from the incoming message */
    message = libbalsa_message_new();
    message->headers->from = internet_address_list_new();
    internet_address_list_add(message->headers->from,
                              balsa_app.current_ident->ia);
    message->headers->date = time(NULL);
    libbalsa_message_set_subject(message, "Message Disposition Notification");
    message->headers->to_list = internet_address_list_new ();
    internet_address_list_append(message->headers->to_list,
                                 for_msg->headers->dispnotify_to);

    /* RFC 2298 requests this mime type... */
    message->subtype = g_strdup("report");
    params = g_new(gchar *, 3);
    params[0] = g_strdup("report-type");
    params[1] = g_strdup("disposition-notification");
    params[2] = NULL;
    message->parameters = g_list_prepend(message->parameters, params);

    /* the first part of the body is an informational note */
    body = libbalsa_message_body_new(message);
    date = libbalsa_message_date_to_utf8(for_msg, balsa_app.date_string);
    dummy = internet_address_list_to_string(for_msg->headers->to_list, FALSE);
    body->buffer = g_strdup_printf(
        "The message sent on %s to %s with subject “%s” has been displayed.\n"
        "There is no guarantee that the message has been read or understood.\n\n",
        date, dummy, LIBBALSA_MESSAGE_GET_SUBJECT(for_msg));
    g_free (date);
    g_free (dummy);
    libbalsa_utf8_sanitize(&body->buffer, balsa_app.convert_unknown_8bit, NULL);
    if (balsa_app.wordwrap)
        libbalsa_wrap_string(body->buffer, balsa_app.wraplength);
    dummy = (gchar *)g_mime_charset_best(body->buffer, strlen(body->buffer));
    body->charset = g_strdup(dummy ? dummy : "us-ascii");
    libbalsa_message_append_part(message, body);

    /* the second part is a rfc3798 compliant
       message/disposition-notification */
    body = libbalsa_message_body_new(message);
    report = g_string_new("");
    uname(&uts_name);
    g_string_printf(report, "Reporting-UA: %s; " PACKAGE " " VERSION "\n",
		    uts_name.nodename);
    /* see rfc 3798, sections 2.3 and 3.2.3 */
    if ((original_rcpt =
	 libbalsa_message_get_user_header(for_msg, "original-recipient")))
	g_string_append_printf(report, "Original-Recipient: %s\n",
			       original_rcpt);
    g_string_append_printf(report, "Final-Recipient: rfc822; %s\n",
                           INTERNET_ADDRESS_MAILBOX(balsa_app.
                                                    current_ident->ia)->
                           addr);
    if (for_msg->message_id)
        g_string_append_printf(report, "Original-Message-ID: <%s>\n",
                               for_msg->message_id);
    g_string_append_printf(report,
			   "Disposition: %s-action/MDN-sent-%sly; displayed",
			   manual ? "manual" : "automatic",
			   manual ? "manual" : "automatical");
    body->buffer = report->str;
    g_string_free(report, FALSE);
    body->content_type = g_strdup("message/disposition-notification");
    body->charset = g_strdup ("US-ASCII");
    libbalsa_message_append_part(message, body);
    return message;
}

static GtkWidget *
create_mdn_dialog(GtkWindow *parent, gchar * sender, gchar * mdn_to_address,
                  LibBalsaMessage * send_msg, LibBalsaIdentity *mdn_ident)
{
    GtkWidget *mdn_dialog;

    mdn_dialog =
        gtk_message_dialog_new(parent,
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_QUESTION,
                               GTK_BUTTONS_YES_NO,
                               _("The sender of this mail, %s, "
                                 "requested \n"
                                 "a Message Disposition Notification "
                                 "(MDN) to be returned to “%s”.\n"
                                 "Do you want to send "
                                 "this notification?"),
                               sender, mdn_to_address);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(mdn_dialog, parent);
#endif
    gtk_window_set_title(GTK_WINDOW(mdn_dialog), _("Reply to MDN?"));
    g_object_set_data(G_OBJECT(mdn_dialog), "balsa-send-msg", send_msg);
    g_object_set_data(G_OBJECT(mdn_dialog), "mdn-ident",
                      g_object_ref(mdn_ident));
    g_signal_connect(G_OBJECT(mdn_dialog), "response",
                     G_CALLBACK(mdn_dialog_response), NULL);

    return mdn_dialog;
}

static void
mdn_dialog_response(GtkWidget * dialog, gint response, gpointer user_data)
{
    LibBalsaMessage *send_msg =
        LIBBALSA_MESSAGE(g_object_get_data(G_OBJECT(dialog),
                                           "balsa-send-msg"));
    LibBalsaIdentity *mdn_ident =
        LIBBALSA_IDENTITY(g_object_get_data(G_OBJECT(dialog), "mdn-ident"));
    GError * error = NULL;
    LibBalsaMsgCreateResult result;

    g_return_if_fail(send_msg != NULL);
    g_return_if_fail(mdn_ident != NULL);

    if (response == GTK_RESPONSE_YES) {
        result =
            libbalsa_message_send(send_msg, balsa_app.outbox, NULL,
                                  balsa_find_sentbox_by_url,
                                  mdn_ident->smtp_server,
                                  gtk_window_get_transient_for
                                  ((GtkWindow *) dialog),
                                  TRUE, &error);
        if (result != LIBBALSA_MESSAGE_CREATE_OK)
            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                 _("Sending the disposition notification failed: %s"),
                                 error ? error->message : "?");
        if (error)
            g_error_free(error);
    }
    g_object_unref(G_OBJECT(send_msg));
    g_object_unref(G_OBJECT(mdn_ident));
    gtk_widget_destroy(dialog);
}

#ifdef HAVE_HTML_WIDGET
/* Does the current part support zoom? */
gboolean
balsa_message_can_zoom(BalsaMessage * bm)
{
    return bm->current_part
        && libbalsa_html_can_zoom(bm->current_part->mime_widget->widget);
}

/* Zoom an html item. */
void
balsa_message_zoom(BalsaMessage * bm, gint in_out)
{
    gint zoom;

    if (!balsa_message_can_zoom(bm))
        return;

    zoom =
       GPOINTER_TO_INT(g_object_get_data
                       (G_OBJECT(bm->message), BALSA_MESSAGE_ZOOM_KEY));
     if (in_out)
       zoom += in_out;
     else
       zoom = 0;
     g_object_set_data(G_OBJECT(bm->message), BALSA_MESSAGE_ZOOM_KEY,
                     GINT_TO_POINTER(zoom));

     libbalsa_html_zoom(bm->current_part->mime_widget->widget, in_out);

}
#endif /* HAVE_HTML_WIDGET */


#ifdef HAVE_GPGME
/*
 * collected GPG(ME) helper stuff to make the source more readable
 */


/*
 * Calculate and return a "worst case" summary of all checked signatures in a
 * message.
 */
static LibBalsaMsgProtectState
balsa_message_scan_signatures(LibBalsaMessageBody *body,
                              LibBalsaMessage * message)
{
    LibBalsaMsgProtectState result = LIBBALSA_MSG_PROTECT_NONE;

    g_return_val_if_fail(message->headers != NULL, result);

    while (body) {
	LibBalsaMsgProtectState this_part_state =
	    libbalsa_message_body_protect_state(body);

	/* remember: greater means worse... */
	if (this_part_state > result)
	    result = this_part_state;

        /* scan embedded messages */
        if (body->parts) {
            LibBalsaMsgProtectState sub_result =
                balsa_message_scan_signatures(body->parts, message);

            if (sub_result >= result)
                result = sub_result;
        }

	body = body->next;
    }

    return result;
}


/*
 * Check if body (of type content_type) is signed and/or encrypted and return
 * the proper icon in this case, and NULL otherwise. If the part is signed,
 * replace *icon-title by the signature status.
 */
static GdkPixbuf *
get_crypto_content_icon(LibBalsaMessageBody * body, const gchar * content_type,
			gchar ** icon_title)
{
    GdkPixbuf *icon;
    gchar * new_title;
    const gchar * icon_name;

    if ((libbalsa_message_body_protection(body) &
         (LIBBALSA_PROTECT_ENCRYPT | LIBBALSA_PROTECT_ERROR)) ==
        LIBBALSA_PROTECT_ENCRYPT)
        return NULL;

    icon_name = balsa_mime_widget_signature_icon_name(libbalsa_message_body_protect_state(body));
    if (!icon_name)
        return NULL;
    icon =
        gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), icon_name, 24, 0, NULL);
    if (!icon_title)
        return icon;

    if (*icon_title &&
	g_ascii_strcasecmp(content_type, "application/pgp-signature") &&
	g_ascii_strcasecmp(content_type, "application/pkcs7-signature") &&
	g_ascii_strcasecmp(content_type, "application/x-pkcs7-signature"))
	new_title = g_strconcat(*icon_title, "; ",
				libbalsa_gpgme_sig_protocol_name(body->sig_info->protocol),
				libbalsa_gpgme_sig_stat_to_gchar(body->sig_info->status),
				NULL);
    else
	new_title = g_strconcat(libbalsa_gpgme_sig_protocol_name(body->sig_info->protocol),
				libbalsa_gpgme_sig_stat_to_gchar(body->sig_info->status),
				NULL);

    if (*icon_title)
	g_free(*icon_title);
    *icon_title = new_title;

    return icon;
}


/* helper functions for libbalsa_msg_perform_crypto below */

/* this is a helper structure to simplify passing a set of parameters */
typedef struct {
    LibBalsaChkCryptoMode chk_mode;      /* current check mode */
    gboolean no_mp_signed;               /* don't check multipart/signed */
    guint max_ref;                       /* maximum allowed ref count */
    gchar * sender;                      /* shortcut to sender */
    gchar * subject;                     /* shortcut to subject */
} chk_crypto_t;


/*
 * check if body is a multipart/encrypted (RFC 3156) or a signed or encrypted
 * application/pkcs7-mime type, try to decrypt it and return the new body
 * chain.
 */
static LibBalsaMessageBody *
libbalsa_msg_try_decrypt(LibBalsaMessage * message, LibBalsaMessageBody * body,
			 chk_crypto_t * chk_crypto)
{
    gchar * mime_type;
    LibBalsaMessageBody * this_body;

    /* Check for multipart/encrypted or application/pkcs7-mime parts and
       replace this_body by it's decrypted content. Remember that there may be
       weird cases where such parts are nested, so do it in a loop. */
    this_body = body;
    mime_type = libbalsa_message_body_get_mime_type(this_body);
    while (!g_ascii_strcasecmp(mime_type, "multipart/encrypted") ||
	   !g_ascii_strcasecmp(mime_type, "application/pkcs7-mime") ||
	   !g_ascii_strcasecmp(mime_type, "application/x-pkcs7-mime")) {
	gint encrres;

	/* FIXME: not checking for body_ref > 1 (or > 2 when re-checking, which
	 * adds an extra ref) leads to a crash if we have both the encrypted and
	 * the unencrypted version open as the body chain of the first one will be
	 * unref'd. */
	if (message->body_ref > chk_crypto->max_ref) {
	    if (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS) {
		libbalsa_information
		    (LIBBALSA_INFORMATION_ERROR,
		     _("The decryption cannot be performed because this message "
		       "is displayed more than once.\n"
		       "Please close the other instances of this message and try "
		       "again."));
		/* downgrade the check mode to avoid multiple errors popping up */
		chk_crypto->chk_mode = LB_MAILBOX_CHK_CRYPT_MAYBE;
	    }
	    return body;
	}

	/* check if the gmime part is present and force loading it if we are
	   in "always" mode */
	if (!this_body->mime_part) {
            GError *err = NULL;
	    if (chk_crypto->chk_mode != LB_MAILBOX_CHK_CRYPT_ALWAYS)
		return this_body;

	    /* force loading the missing part */
	    if(!libbalsa_mailbox_get_message_part(message, this_body, &err)) {
		libbalsa_information
		    (LIBBALSA_INFORMATION_ERROR,
                     _("Parsing a message part failed: %s"),
                     err ? err->message : _("Possible disk space problem."));
                g_clear_error(&err);
                /* There is nothing more we can do... */
                return body;
            }
	}

	encrres = libbalsa_message_body_protection(this_body);

	if (encrres & LIBBALSA_PROTECT_ENCRYPT) {
	    if (encrres & LIBBALSA_PROTECT_ERROR) {
		libbalsa_information
		    (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
		     LIBBALSA_INFORMATION_ERROR : LIBBALSA_INFORMATION_MESSAGE,
                     _("The message sent by %s with subject “%s” contains "
                       "an encrypted part, but its structure is invalid."),
		     chk_crypto->sender, chk_crypto->subject);
            } else if (encrres & LIBBALSA_PROTECT_RFC3156) {
                if (!balsa_app.has_openpgp)
                    libbalsa_information
                        (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
			 LIBBALSA_INFORMATION_WARNING : LIBBALSA_INFORMATION_MESSAGE,
                         _("The message sent by %s with subject “%s” "
                           "contains a PGP encrypted part, but this "
                           "crypto protocol is not available."),
                         chk_crypto->sender, chk_crypto->subject);
                else
                    this_body =
                        libbalsa_body_decrypt(this_body,
                                              GPGME_PROTOCOL_OpenPGP, NULL);
            } else if (encrres & LIBBALSA_PROTECT_SMIMEV3) {
                if (!balsa_app.has_smime)
                    libbalsa_information
                        (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
			 LIBBALSA_INFORMATION_WARNING : LIBBALSA_INFORMATION_MESSAGE,
                         _("The message sent by %s with subject “%s” "
                           "contains a S/MIME encrypted part, but this "
                           "crypto protocol is not available."),
                         chk_crypto->sender, chk_crypto->subject);
                else
                    this_body =
                        libbalsa_body_decrypt(this_body, GPGME_PROTOCOL_CMS,
					      NULL);
            }
        }

	/* has been decrypted? - eject if not, otherwise... */
	if (!this_body->was_encrypted)
	    return this_body;

	/* ...check decrypted content again... */
	g_free(mime_type);
	mime_type = libbalsa_message_body_get_mime_type(this_body);
    }
    g_free(mime_type);

    return this_body;
}


/*
 * Treatment of a multipart/signed body with protocol protocol.
 */
#define BALSA_MESSAGE_SIGNED_NOTIFIED "balsa-message-signed-notified"
static void
libbalsa_msg_try_mp_signed(LibBalsaMessage * message, LibBalsaMessageBody *body,
			   chk_crypto_t * chk_crypto)
{
    gint signres;

    if (chk_crypto->no_mp_signed)
	return;

    /* check if the gmime part is present and force loading it if we are in
       "always" mode */
    if (!body->mime_part) {
        GError *err = NULL;
	if (chk_crypto->chk_mode != LB_MAILBOX_CHK_CRYPT_ALWAYS)
	    return;

	/* force loading the missing part */
        if(!libbalsa_mailbox_get_message_part(message, body, &err)) {
            libbalsa_information
                (LIBBALSA_INFORMATION_ERROR,
                 _("Parsing a message part failed: %s"),
                 err ? err->message : _("Possible disk space problem."));
            g_clear_error(&err);
            /* There is nothing more we can do... */
            return;
        }
    }

    /* check which type of protection we've got */
    signres = libbalsa_message_body_protection(body);
    if (!(signres & LIBBALSA_PROTECT_SIGN))
	return;

    /* eject if the structure is broken */
    if (signres & LIBBALSA_PROTECT_ERROR) {
	libbalsa_information
	    (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
	     LIBBALSA_INFORMATION_ERROR : LIBBALSA_INFORMATION_MESSAGE,
	     _("The message sent by %s with subject “%s” contains a signed "
	       "part, but its structure is invalid. The signature, if there "
	       "is any, cannot be checked."),
	     chk_crypto->sender, chk_crypto->subject);
	return;
    }

    /* check for an unsupported protocol */
    if (((signres & LIBBALSA_PROTECT_RFC3156) && !balsa_app.has_openpgp) ||
	((signres & LIBBALSA_PROTECT_SMIMEV3) && !balsa_app.has_smime)) {
	libbalsa_information
	    (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
	     LIBBALSA_INFORMATION_WARNING : LIBBALSA_INFORMATION_MESSAGE,
	     _("The message sent by %s with subject “%s” contains a %s "
	       "signed part, but this crypto protocol is not available."),
	     chk_crypto->sender, chk_crypto->subject,
	     signres & LIBBALSA_PROTECT_RFC3156 ? _("PGP") : _("S/MIME"));
	return;
    }

    /* force creating the protection info */
    if (body->parts->next->sig_info) {
	g_object_unref(body->parts->next->sig_info);
	body->parts->next->sig_info = NULL;
    }
    if (!libbalsa_body_check_signature(body,
				       signres & LIBBALSA_PROTECT_RFC3156 ?
				       GPGME_PROTOCOL_OpenPGP : GPGME_PROTOCOL_CMS))
	return;

    /* evaluate the result */
    if (g_object_get_data(G_OBJECT(message), BALSA_MESSAGE_SIGNED_NOTIFIED))
        return;
    g_object_set_data(G_OBJECT(message), BALSA_MESSAGE_SIGNED_NOTIFIED,
                      GUINT_TO_POINTER(TRUE));

    if (body->parts->next->sig_info) {
	switch (libbalsa_message_body_protect_state(body->parts->next)) {
	case LIBBALSA_MSG_PROTECT_SIGN_GOOD:
	    libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
				 _("Detected a good signature"));
	    break;
	case LIBBALSA_MSG_PROTECT_SIGN_NOTRUST:
	    if (body->parts->next->sig_info->protocol == GPGME_PROTOCOL_CMS)
		libbalsa_information
		    (LIBBALSA_INFORMATION_MESSAGE,
		     _("Detected a good signature with insufficient "
		       "validity"));
	    else
		libbalsa_information
		    (LIBBALSA_INFORMATION_MESSAGE,
		     _("Detected a good signature with insufficient "
		       "validity/trust"));
	    break;
	case LIBBALSA_MSG_PROTECT_SIGN_BAD:
            libbalsa_information
		(chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
		 LIBBALSA_INFORMATION_ERROR : LIBBALSA_INFORMATION_MESSAGE,
		 _("Checking the signature of the message sent by %s with "
		   "subject “%s” returned:\n%s"),
		 chk_crypto->sender, chk_crypto->subject,
		 libbalsa_gpgme_sig_stat_to_gchar(body->parts->next->sig_info->status));
	    break;
	default:
	    break;
        }
    } else
	libbalsa_information
	    (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
	     LIBBALSA_INFORMATION_ERROR : LIBBALSA_INFORMATION_MESSAGE,
	     _("Checking the signature of the message sent by %s with subject "
	       "“%s” failed with an error!"),
	     chk_crypto->sender, chk_crypto->subject);
}


/*
 * Check if body is has some RFC 2440 protection and try to decrypt and/or
 * verify it.
 */
static void
libbalsa_msg_part_2440(LibBalsaMessage * message, LibBalsaMessageBody * body,
		       chk_crypto_t * chk_crypto)
{
    gpgme_error_t sig_res;
    GMimePartRfc2440Mode rfc2440mode;

    /* multiparts or complete messages can not be RFC 2440 protected */
    if (body->body_type == LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE ||
	body->body_type == LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART)
	return;

    if (!body->mime_part) {
        GError *err = NULL;
	/* if the user requested to always check everything or if the part is
	   text (and therefore likely to be displayed anyway) force loading
	   the missing part */
	if (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_MAYBE &&
	    body->body_type != LIBBALSA_MESSAGE_BODY_TYPE_TEXT)
	    return;

        if(!libbalsa_mailbox_get_message_part(message, body, &err)) {
            libbalsa_information
                (LIBBALSA_INFORMATION_ERROR,
                 _("Parsing a message part failed: %s"),
                 err ? err->message : _("Possible disk space problem."));
            g_clear_error(&err);
            return;
        }
    }

    /* check if this is a RFC2440 part */
    if (!GMIME_IS_PART(body->mime_part))
	return;
    libbalsa_mailbox_lock_store(body->message->mailbox);
    rfc2440mode = g_mime_part_check_rfc2440(GMIME_PART(body->mime_part));
    libbalsa_mailbox_unlock_store(body->message->mailbox);

    /* if not, or if we have more than one instance of this message open, eject
       (see remark for libbalsa_msg_try_decrypt above) - remember that
       libbalsa_rfc2440_verify would also replace the stream by the "decrypted"
       (i.e. RFC2440 stuff removed) one! */
    if (rfc2440mode == GMIME_PART_RFC2440_NONE)
	return;
    if (message->body_ref > chk_crypto->max_ref) {
	if (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS) {
            libbalsa_information
                (LIBBALSA_INFORMATION_ERROR, "%s\n%s",
		 rfc2440mode == GMIME_PART_RFC2440_ENCRYPTED ?
                 _("The decryption cannot be performed because "
		   "this message is displayed more than once.") :
                 _("The signature check and removal of the OpenPGP armor "
		   "cannot be performed because "
		   "this message is displayed more than once."),
                 _("Please close the other instances of this message "
		   "and try again."));
	    /* downgrade the check mode to avoid multiple errors popping up */
	    chk_crypto->chk_mode = LB_MAILBOX_CHK_CRYPT_MAYBE;
	}
        return;
    }

    /* do the rfc2440 stuff */
    libbalsa_mailbox_lock_store(body->message->mailbox);
    if (rfc2440mode == GMIME_PART_RFC2440_SIGNED)
        sig_res =
            libbalsa_rfc2440_verify(GMIME_PART(body->mime_part),
				    &body->sig_info);
    else {
        sig_res =
            libbalsa_rfc2440_decrypt(GMIME_PART(body->mime_part),
				     &body->sig_info,
				     NULL);
	body->was_encrypted = (body->sig_info || sig_res == GPG_ERR_NO_ERROR);
	if (sig_res == GPG_ERR_NO_ERROR) {
	    /* decrypting may change the charset, so be sure to use the one
	       GMimePart reports */
	    g_free(body->charset);
	    body->charset = NULL;
	}
    }
    libbalsa_mailbox_unlock_store(body->message->mailbox);

    if (body->sig_info && sig_res == GPG_ERR_NO_ERROR) {
        if (body->sig_info->validity >= GPGME_VALIDITY_MARGINAL &&
            body->sig_info->key->owner_trust >= GPGME_VALIDITY_MARGINAL)
            libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
                                 _("Detected a good signature"));
        else
            libbalsa_information
		(LIBBALSA_INFORMATION_MESSAGE,
		 _("Detected a good signature with insufficient "
		   "validity/trust"));
    } else if (sig_res != GPG_ERR_NO_ERROR && sig_res != GPG_ERR_CANCELED)
	libbalsa_information
	    (chk_crypto->chk_mode == LB_MAILBOX_CHK_CRYPT_ALWAYS ?
	     LIBBALSA_INFORMATION_ERROR : LIBBALSA_INFORMATION_MESSAGE,
	     _("Checking the signature of the message sent by %s with "
	       "subject “%s” returned:\n%s"),
	     chk_crypto->sender, chk_crypto->subject,
	     libbalsa_gpgme_sig_stat_to_gchar(sig_res));
}


/*
 * Scan a body chain for RFC 2440, RFC 2633 and RFC 3156 encrypted and/or
 * signed parts. The function is called recursively for nested stuff.
 */
static LibBalsaMessageBody *
libbalsa_msg_perform_crypto_real(LibBalsaMessage * message,
				 LibBalsaMessageBody * body,
				 chk_crypto_t * chk_crypto)
{
    gchar * mime_type;
    LibBalsaMessageBody * chk_body;

    /* First try do decrypt RFC 3156 multipart/encrypted or any RFC 2633
       application/pkcs7-mime stuff. */
    body = libbalsa_msg_try_decrypt(message, body, chk_crypto);

    /* Check for multipart/signed and check the signature. */
    mime_type = libbalsa_message_body_get_mime_type(body);
    if (!g_ascii_strcasecmp(mime_type, "multipart/signed"))
	libbalsa_msg_try_mp_signed(message, body, chk_crypto);
    g_free(mime_type);

    /* loop over the parts, checking for RFC 2440 stuff, but ignore
       application/octet-stream which might be a detached encrypted part
       as well as all detached signatures */
    chk_body = body;
    while (chk_body) {
	mime_type = libbalsa_message_body_get_mime_type(chk_body);

	if (g_ascii_strcasecmp(mime_type, "application/octet-stream") &&
	    g_ascii_strcasecmp(mime_type, "application/pkcs7-signature") &&
	    g_ascii_strcasecmp(mime_type, "application/x-pkcs7-signature") &&
	    g_ascii_strcasecmp(mime_type, "application/pgp-signature"))
	    libbalsa_msg_part_2440(message, chk_body, chk_crypto);
	g_free(mime_type);

	/* check nested stuff */
	if (chk_body->parts)
	    chk_body->parts =
		libbalsa_msg_perform_crypto_real(message, chk_body->parts,
						 chk_crypto);

	chk_body = chk_body->next;
    }

    return body;
}


/*
 * Launches a scan of message with chk_mode and a maximum reference of max_ref.
 * If no_mp_signed is TRUE, multipart/signed parts are ignored, i.e. only
 * encrypted (either "really" encrypted or armored) parts are used. This is
 * useful e.g. for replying.
 */
void
balsa_message_perform_crypto(LibBalsaMessage * message,
			     LibBalsaChkCryptoMode chk_mode,
			     gboolean no_mp_signed, guint max_ref)
{
    chk_crypto_t chk_crypto;

    if (!message->body_list)
	return;

    /* check if the user requested to ignore any crypto stuff */
    if (chk_mode == LB_MAILBOX_CHK_CRYPT_NEVER)
	return;

    /* set up... */
    chk_crypto.chk_mode = chk_mode;
    chk_crypto.no_mp_signed = no_mp_signed;
    chk_crypto.max_ref = max_ref;
    chk_crypto.sender = balsa_message_sender_to_gchar(message->headers->from, -1);
    chk_crypto.subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(message));
    libbalsa_utf8_sanitize(&chk_crypto.subject, balsa_app.convert_unknown_8bit,
			   NULL);

    /* do the real work */
    message->body_list =
	libbalsa_msg_perform_crypto_real(message, message->body_list,
					 &chk_crypto);

    /* clean up */
    g_free(chk_crypto.subject);
    g_free(chk_crypto.sender);
}


/*
 * Callback for the "Check Crypto" button in the message's top-level headers.
 * It works roughly like balsa_message_set, but with less overhead and with
 * "check always" mode. Note that this routine adds a temporary reference to
 * the message.
 */
static void
message_recheck_crypto_cb(GtkWidget * button, BalsaMessage * bm)
{
    LibBalsaMsgProtectState prot_state;
    LibBalsaMessage * message;
    GtkTreeIter iter;
    BalsaPartInfo * info;
    gboolean has_focus = bm->focus_state != BALSA_MESSAGE_FOCUS_STATE_NO;

    g_return_if_fail(bm != NULL);

    message = bm->message;
    g_return_if_fail(message != NULL);

    select_part(bm, NULL);
    balsa_message_clear_tree(bm);

    g_object_ref(G_OBJECT(message));
    if (!libbalsa_message_body_ref(message, TRUE, TRUE)) {
	g_object_unref(G_OBJECT(message));
        return;
    }

    g_object_set_data(G_OBJECT(message), BALSA_MESSAGE_SIGNED_NOTIFIED, NULL);
    balsa_message_perform_crypto(message, LB_MAILBOX_CHK_CRYPT_ALWAYS, FALSE, 2);

    /* calculate the signature summary state */
    prot_state =
        balsa_message_scan_signatures(message->body_list, message);

    /* update the icon if necessary */
    if (message->prot_state != prot_state)
        message->prot_state = prot_state;

    /* may update the icon */
    libbalsa_mailbox_msgno_update_attach(bm->message->mailbox,
					 bm->message->msgno, bm->message);

    display_headers(bm);
    display_content(bm);

    if (bm->info_count > 1)
        gtk_widget_show(bm->switcher);
    else
        gtk_widget_hide(bm->switcher);
    gtk_stack_set_visible_child_name(GTK_STACK(bm->stack), "content");

    if (!gtk_tree_model_get_iter_first (gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview)),
                                        &iter)) {
	libbalsa_message_body_unref(message);
	g_object_unref(G_OBJECT(message));
        return;
    }

    info =
        tree_next_valid_part_info(gtk_tree_view_get_model(GTK_TREE_VIEW(bm->treeview)),
                                  &iter);
    select_part(bm, info);
    if (info)
        g_object_unref(info);

    /* restore keyboard focus to the content, if it was there before */
    if (has_focus)
        balsa_message_grab_focus(bm);

    libbalsa_message_body_unref(message);
    g_object_unref(G_OBJECT(message));
}

#endif  /* HAVE_GPGME */

/*
 * Public method for find-in-message.
 */

void
balsa_message_find_in_message(BalsaMessage * bm)
{
    GtkWidget *w;

    if (bm->current_part
        && (w = bm->current_part->mime_widget->widget)
        && (GTK_IS_TEXT_VIEW(w)
#ifdef HAVE_HTML_WIDGET
            || libbalsa_html_can_search(w)
#endif                          /* HAVE_HTML_WIDGET */
            )) {
        if (GTK_IS_TEXT_VIEW(w)) {
            GtkTextView *text_view = (GtkTextView *) w;
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);

            gtk_text_buffer_get_start_iter(buffer, &bm->find_iter);
        }

        bm->find_forward = TRUE;
        gtk_entry_set_text(GTK_ENTRY(bm->find_entry), "");
        g_signal_connect_swapped(gtk_widget_get_toplevel(GTK_WIDGET(bm)),
                                 "key-press-event",
                                 G_CALLBACK(bm_find_pass_to_entry), bm);

        bm_find_set_status(bm, BM_FIND_STATUS_INIT);

        gtk_widget_show(bm->find_bar);
        if (gtk_widget_get_window(bm->find_entry))
            gtk_widget_grab_focus(bm->find_entry);
    }
}
