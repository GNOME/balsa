/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include <gnome.h>

#include "config.h"
#include "identity.h"

#define gnome_stock_button_with_label(p,l) gtk_button_new_with_label(l)
#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

static GtkObjectClass* parent_class;
static const gchar* default_ident_name = N_("Default");
static const guint padding = 4;

static void libbalsa_identity_class_init(LibBalsaIdentityClass* klass);
static void libbalsa_identity_init(LibBalsaIdentity* ident);

static void libbalsa_identity_destroy(GtkObject* object);

static void new_ident_cb(GtkButton* , gpointer);
static void edit_ident_cb(GtkButton* , gpointer);
static void set_default_ident_cb(GtkButton* , gpointer);
static void delete_ident_cb(GtkWidget* , gpointer);
static void identity_list_update(GtkCList* clist);
static void config_frame_button_select_cb(GtkCList* clist, 
                                          gint row, 
                                          gint column, 
                                          GdkEventButton* event, 
                                          gpointer user_data);

static GtkWidget* setup_ident_dialog(GtkWindow* parent, gboolean createp, 
				     LibBalsaIdentity*, gpointer);

static void ident_dialog_add_checkbutton(GtkDialog*, const gchar*, 
                                         const gchar*, gboolean);
static GtkWidget* ident_dialog_add_entry(GtkDialog*, const gchar*,
					 const gchar*, gchar*);
static gchar* ident_dialog_get_text(GtkDialog*, const gchar*);
static gboolean ident_dialog_get_bool(GtkDialog*, const gchar*);
static gboolean ident_dialog_update(GtkDialog*);
static void config_dialog_select_cb(GtkCList* clist, gint row, gint column,
                                    GdkEventButton* event, gpointer user_data);

static GtkWidget* libbalsa_identity_display_frame(void);
static void  display_frame_add_field(GtkFrame* frame, GtkBox* box, 
                                     const gchar* name, const gchar* key);
static void display_frame_add_boolean(GtkFrame* frame, GtkBox* box,
                                      const gchar* name, const gchar* key);
static void display_frame_update(GtkFrame* frame, LibBalsaIdentity* ident);
static void display_frame_set_field(GtkFrame* frame, const gchar* key, 
                                    const gchar* value);
static void display_frame_set_boolean(GtkFrame* frame, const gchar* key, 
                                      gboolean value);


static void select_dialog_row_cb(GtkCList* clist, 
                                 gint row, 
                                 gint column, 
                                 GdkEventButton* event, 
                                 gpointer user_data);


GtkType
libbalsa_identity_get_type()
{
    static GtkType libbalsa_identity_type = 0;
    
    if (!libbalsa_identity_type) {
        static const GtkTypeInfo libbalsa_identity_info = 
            {
                "LibBalsaIdentity",
                sizeof(LibBalsaIdentity),
                sizeof(LibBalsaIdentityClass),
                (GtkClassInitFunc) libbalsa_identity_class_init,
                (GtkObjectInitFunc) libbalsa_identity_init,
                /* reserved_1 */ NULL,
                /* reserved_2 */ NULL,
                (GtkClassInitFunc) NULL
            };
        
        libbalsa_identity_type = 
            gtk_type_unique(gtk_object_get_type(), &libbalsa_identity_info);
    }
    
    return libbalsa_identity_type;
}


static void
libbalsa_identity_class_init(LibBalsaIdentityClass* klass)
{
    GtkObjectClass* object_class;

    parent_class = gtk_type_class(gtk_object_get_type());

    object_class = GTK_OBJECT_CLASS(klass);
    object_class->destroy = libbalsa_identity_destroy;
}


/* 
 * Class inititialization function, set defaults for new objects
 */
static void
libbalsa_identity_init(LibBalsaIdentity* ident)
{
    ident->identity_name = NULL;
    ident->address = libbalsa_address_new();
    gtk_object_ref(GTK_OBJECT(ident->address)); 
    gtk_object_sink(GTK_OBJECT(ident->address));
    ident->replyto = NULL;
    ident->domain = NULL;
    ident->bcc = NULL;
    ident->reply_string = g_strdup(_("Re:"));
    ident->forward_string = g_strdup(_("Fwd:"));
    ident->signature_path = NULL;
    ident->sig_executable = FALSE;
    ident->sig_sending = TRUE;
    ident->sig_whenforward = TRUE;
    ident->sig_whenreply = TRUE;
    ident->sig_separator = TRUE;
    ident->sig_prepend = FALSE;
}


/* 
 * Create a new object with the identity name "Default".  Does not add
 * it to the list of identities for the application.
 */
GtkObject* 
libbalsa_identity_new(void) 
{
    LibBalsaIdentity* ident;
    
    ident = LIBBALSA_IDENTITY(gtk_type_new(LIBBALSA_TYPE_IDENTITY));
    ident->identity_name = g_strdup(default_ident_name);
    return GTK_OBJECT(ident);
}


/*
 * Create a new object with the specified identity name.  Does not add
 * it to the list of identities for the application.
 */
GtkObject*
libbalsa_identity_new_with_name(const gchar* ident_name)
{
    LibBalsaIdentity* ident;
    
    ident = LIBBALSA_IDENTITY(gtk_type_new(LIBBALSA_TYPE_IDENTITY));
    ident->identity_name = g_strdup(ident_name);
    return GTK_OBJECT(ident);
}


/* 
 * Destroy the object, freeing all the values in the process.
 */
static void
libbalsa_identity_destroy(GtkObject* object)
{
    LibBalsaIdentity* ident;
    
    g_return_if_fail(object != NULL);
    
    ident = LIBBALSA_IDENTITY(object);

    gtk_object_destroy(GTK_OBJECT(ident->address));
    g_free(ident->identity_name);
    g_free(ident->replyto);
    g_free(ident->domain);
    g_free(ident->bcc);
    g_free(ident->reply_string);
    g_free(ident->forward_string);
    g_free(ident->signature_path);

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (object);
}

void
libbalsa_identity_set_identity_name(LibBalsaIdentity* ident, const gchar* name)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->identity_name);
    ident->identity_name = g_strdup(name);
}


void
libbalsa_identity_set_address(LibBalsaIdentity* ident, LibBalsaAddress* ad)
{
    g_return_if_fail(ident != NULL);
    
    gtk_object_unref(GTK_OBJECT(ident->address));
    gtk_object_ref(GTK_OBJECT(ad));
    gtk_object_sink(GTK_OBJECT(ad));
    ident->address = ad;
}


void
libbalsa_identity_set_replyto(LibBalsaIdentity* ident, const gchar* address)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->replyto);
    ident->replyto = g_strdup(address);
}


void
libbalsa_identity_set_domain(LibBalsaIdentity* ident, const gchar* dom)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->domain);
    ident->domain = g_strdup(dom);
}


void 
libbalsa_identity_set_bcc(LibBalsaIdentity* ident, const gchar* bcc)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->bcc);
    ident->bcc = g_strdup(bcc);
}


void 
libbalsa_identity_set_reply_string(LibBalsaIdentity* ident, const gchar* reply)
{
    g_return_if_fail(ident != NULL);

    g_free(ident->reply_string);
    ident->reply_string = g_strdup(reply);
}


void 
libbalsa_identity_set_forward_string(LibBalsaIdentity* ident, const gchar* forward)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->forward_string);
    ident->forward_string = g_strdup(forward);
}


void
libbalsa_identity_set_signature_path(LibBalsaIdentity* ident, const gchar* path)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->signature_path);
    ident->signature_path = g_strdup(path);
}

void
libbalsa_identity_set_sig_executable(LibBalsaIdentity* ident, gboolean sig_executable)
{
    g_return_if_fail(ident != NULL);
    ident->sig_executable = sig_executable;
}


void
libbalsa_identity_set_sig_sending(LibBalsaIdentity* ident, gboolean sig_sending)
{
    g_return_if_fail(ident != NULL);
    ident->sig_sending = sig_sending;
}


void
libbalsa_identity_set_sig_whenforward(LibBalsaIdentity* ident, gboolean forward)
{
    g_return_if_fail(ident != NULL);
    ident->sig_whenforward = forward;
}


void 
libbalsa_identity_set_sig_whenreply(LibBalsaIdentity* ident, gboolean reply)
{
    g_return_if_fail(ident != NULL);
    ident->sig_whenreply = reply;
}


void
libbalsa_identity_set_sig_separator(LibBalsaIdentity* ident, gboolean separator)
{
    g_return_if_fail(ident != NULL);
    ident->sig_separator = separator;
}


void 
libbalsa_identity_set_sig_prepend(LibBalsaIdentity* ident, gboolean prepend)
{
    g_return_if_fail(ident != NULL);
    ident->sig_prepend = prepend;
}





/*
 * The close callback for the editing/creation dialog.
 * if we create new identity and the dialog gets closed, we need to destroy
 * the half-created object.
 */
static int
ident_dialog_cleanup(GtkDialog* dialog)
{
    gboolean edit;
    gpointer tmpptr;

    edit = (gboolean) gtk_object_get_data(GTK_OBJECT(dialog), 
                                          "identity-edit");
    if (!edit) {
        tmpptr = gtk_object_get_data(GTK_OBJECT(dialog), "identity");
        
        if (tmpptr != NULL) {
            gtk_object_destroy(GTK_OBJECT(tmpptr));
	    g_warning("Object destroyed.\n");
	}
    }

    return FALSE;
}

struct IdentitySelectDialog {
    LibBalsaIdentity * ident;
    gboolean close_ok;
};

LibBalsaIdentity*
libbalsa_identity_select_dialog(GtkWindow* parent, const gchar* prompt,
				GList** identities, LibBalsaIdentity** defid)
{
    struct IdentitySelectDialog isd;
    GtkWidget* frame1 = gtk_frame_new(NULL);
    GtkWidget* clist = gtk_clist_new(2);
    GtkWidget* dialog;
    gint choice = 1;

    isd.ident = NULL;
    isd.close_ok = FALSE;

    gtk_object_set_data(GTK_OBJECT(clist), "parent-window", parent);
    gtk_object_set_data(GTK_OBJECT(clist), "identities", identities);
    gtk_object_set_data(GTK_OBJECT(clist), "default-id", defid);
    dialog = gtk_dialog_new_with_buttons(prompt, 
                                         parent,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK,
                                         GTK_RESPONSE_OK,
                                         GTK_STOCK_CANCEL,
                                         GTK_RESPONSE_CANCEL,
                                         NULL);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
                       frame1, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(frame1), clist);
    gtk_container_set_border_width(GTK_CONTAINER(frame1), padding);
    gtk_widget_show_all(frame1);

    gtk_clist_column_titles_hide(GTK_CLIST(clist));
    gtk_clist_set_row_height(GTK_CLIST(clist), 0);
    gtk_clist_set_column_min_width(GTK_CLIST(clist), 0, 20);
    gtk_clist_set_column_min_width(GTK_CLIST(clist), 1, 200);
    gtk_clist_set_column_auto_resize(GTK_CLIST(clist), 1, TRUE);
    gtk_widget_set_usize(GTK_WIDGET(clist), 200, 200);
    gtk_signal_connect(GTK_OBJECT(clist), "select-row",
                       GTK_SIGNAL_FUNC(select_dialog_row_cb),
                       &isd);
    identity_list_update(GTK_CLIST(clist));

#if TO_BE_PORTED
    gnome_dialog_set_parent(GNOME_DIALOG(dialog), parent);
#endif
    /* default action=Enter is to select the identity */
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    choice = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (choice != GTK_RESPONSE_OK && !isd.close_ok)
        return NULL;
    
    return isd.ident;
}


static void
select_dialog_row_cb(GtkCList* clist, 
                     gint row, 
                     gint column, 
                     GdkEventButton* event, 
		     gpointer user_data)
{
    struct IdentitySelectDialog *isd_p = user_data;

    if (event && event->type == GDK_2BUTTON_PRESS) {
        /* it's a double-click:
         * isd->ident was set in the callback from the first click,
         * so we can set close_ok and close the dialog
         *
         * if we wanted to make this dialog single-click
         * (select-and-close), we'd ignore the GdkEventButton and just
         * set the identity then close the dialog (we'd also want to
         * remove the `OK' button from the dialog)
         * */
        isd_p->close_ok = TRUE;
        gtk_widget_destroy(gtk_widget_get_ancestor
                           (GTK_WIDGET(clist), GTK_TYPE_WINDOW));
    } else
        isd_p->ident =
            LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
}



/*
 * Create and return a frame containing a list of the identities in
 * the application and a number of buttons to edit, create, and delete
 * identities.  Also provides a way to set the default identity.
 */
GtkWidget* 
libbalsa_identity_config_frame(gboolean with_buttons, GList** identities,
			       LibBalsaIdentity** defid)
{

    GtkWidget* config_frame = gtk_frame_new(NULL);
    GtkWidget* hbox = gtk_hbox_new(FALSE, padding);
    GtkWidget* vbox;
    GtkWidget* clist = gtk_clist_new(2);
    GtkWidget* buttons[4];
    gint i = 0;

    gtk_container_set_border_width(GTK_CONTAINER(config_frame), 0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), padding);
    gtk_container_add(GTK_CONTAINER(config_frame), hbox);
    gtk_box_pack_start(GTK_BOX(hbox), clist, TRUE, TRUE, 0);
    
    if (with_buttons) {
        vbox = gtk_vbox_new(FALSE, padding);
        gtk_box_pack_end(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

        buttons[0] = gtk_button_new_from_stock (GNOME_STOCK_PIXMAP_NEW);
        gtk_signal_connect(GTK_OBJECT(buttons[0]), "clicked",
                           GTK_SIGNAL_FUNC(new_ident_cb), clist);
        
        buttons[1] = gtk_button_new_from_stock(GNOME_STOCK_PIXMAP_PROPERTIES);
        gtk_button_set_label(GTK_BUTTON(buttons[1]), _("Edit"));
        gtk_signal_connect(GTK_OBJECT(buttons[1]), "clicked",
                           GTK_SIGNAL_FUNC(edit_ident_cb), clist);
        
        buttons[2] = gtk_button_new_from_stock(GNOME_STOCK_PIXMAP_ADD);
        gtk_button_set_label(GTK_BUTTON(buttons[2]), _("Set Default"));
        gtk_signal_connect(GTK_OBJECT(buttons[2]), "clicked",
                           GTK_SIGNAL_FUNC(set_default_ident_cb), clist);
        
        buttons[3] = gtk_button_new_from_stock(GNOME_STOCK_PIXMAP_REMOVE);
        gtk_signal_connect(GTK_OBJECT(buttons[3]), "clicked",
                           GTK_SIGNAL_FUNC(delete_ident_cb), clist);
        
        gtk_box_pack_start(GTK_BOX(vbox), buttons[0], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), buttons[1], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), buttons[2], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), buttons[3], FALSE, FALSE, 0);
	
        for (i = 1; i < 4; ++i) {
            gtk_signal_connect(GTK_OBJECT(clist), "select-row",
                               GTK_SIGNAL_FUNC(config_frame_button_select_cb),
                               buttons[i]);
	}
    }

    gtk_clist_column_titles_hide(GTK_CLIST(clist));
    gtk_clist_set_row_height(GTK_CLIST(clist), 0);
    gtk_clist_set_column_min_width(GTK_CLIST(clist), 0, 20);
    gtk_clist_set_column_min_width(GTK_CLIST(clist), 1, 200);
    gtk_clist_set_column_auto_resize(GTK_CLIST(clist), 1, TRUE);
    gtk_widget_set_usize(GTK_WIDGET(clist), 200, 200);
    gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_BROWSE);

    gtk_object_set_data(GTK_OBJECT(clist), "identities", identities);
    gtk_object_set_data(GTK_OBJECT(clist), "default-id", defid);
    identity_list_update(GTK_CLIST(clist));
    gtk_object_set_data(GTK_OBJECT(config_frame), "clist", clist);

    return config_frame;
}


/* identity_list_update:
 * Update the list of identities in the config frame, displaying the
 * available identities in the application, and which is default.
 * We need to tempararily switch to selection mode single to avoid row 
 * autoselection when there is no data attached to it yet (between
 * gtk_clist_append() and gtk_clist_set_row_data()).
 */
static void
identity_list_update(GtkCList* clist)
{
    LibBalsaIdentity* ident;
    GdkPixmap* pixmap = NULL;
    GdkPixmap* bitmap = NULL;
    GList** identities, *list;
    LibBalsaIdentity* *default_id, *current;
    gchar* text[2];
    gint i = 0;

    text[0] = NULL;

    identities = gtk_object_get_data(GTK_OBJECT(clist), "identities");
    default_id = gtk_object_get_data(GTK_OBJECT(clist), "default-id");

    if(clist->selection) {
	i = GPOINTER_TO_INT(clist->selection->data);
	current = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, i));
    } else current = *default_id;
    gtk_clist_set_selection_mode(clist, GTK_SELECTION_SINGLE);

    gtk_clist_freeze(clist);
    gtk_clist_clear(clist);
    
    for (list = *identities; list; list = g_list_next(list)) {
        ident = LIBBALSA_IDENTITY(list->data);
        text[1] = ident->identity_name;
        i = gtk_clist_append(clist, text);
        gtk_clist_set_row_data(clist, i, ident);
        
        /* do something to indicate it is the active style */
#if BALSA_MAJOR < 2
        if (ident == *default_id) {
            gnome_stock_pixmap_gdk(GNOME_STOCK_MENU_FORWARD, 
                                   GNOME_STOCK_PIXMAP_REGULAR, 
                                   &pixmap, &bitmap);
            gtk_clist_set_pixmap(clist, i, 0, pixmap, bitmap);
        } else {
            gnome_stock_pixmap_gdk(GNOME_STOCK_MENU_BLANK,
                                   GNOME_STOCK_PIXMAP_REGULAR,
                                   &pixmap, &bitmap);
            gtk_clist_set_pixmap(clist, i, 0, pixmap, bitmap);
        }
#else
        {
            GdkPixbuf *pixbuf =
                gtk_widget_render_icon(GTK_WIDGET(clist),
                                       ident == *default_id ?
                                       GNOME_STOCK_MENU_FORWARD :
                                       GNOME_STOCK_MENU_BLANK,
                                       GTK_ICON_SIZE_BUTTON,
                                       "Balsa");
            gdk_pixbuf_render_pixmap_and_mask(pixbuf, &pixmap, &bitmap, 1);
            gdk_pixbuf_unref(pixbuf);
            gtk_clist_set_pixmap(clist, i, 0, pixmap, bitmap);
        }
#endif                          /* BALSA_MAJOR < 2 */
    }

    gtk_clist_set_selection_mode(clist, GTK_SELECTION_BROWSE);
    gtk_clist_sort(clist);
    gtk_clist_thaw(clist);
    i = gtk_clist_find_row_from_data(clist, current);
    gtk_clist_select_row(clist, i, -1); 
}


static void
config_frame_button_select_cb(GtkCList* clist, gint row, gint column,
                              GdkEventButton* event, gpointer user_data)
{
    GtkWidget* button = GTK_WIDGET(user_data);
    
    gtk_widget_set_sensitive(button, TRUE);
}

/*
 * Create a new identity dialog
 */
static void
new_ident_cb(GtkButton* button, gpointer user_data)
{
    LibBalsaIdentity* ident;
    GtkWidget* dialog;
    GtkWindow* parent;

    ident = LIBBALSA_IDENTITY(libbalsa_identity_new());
    parent = gtk_object_get_data(GTK_OBJECT(user_data), "parent-window");
    dialog = setup_ident_dialog(parent, TRUE, ident, user_data);
    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        if(ident_dialog_update(GTK_DIALOG(dialog))) {
            int row;
            GtkCList* clist = GTK_CLIST(user_data);
            GList** identities = 
                gtk_object_get_data(GTK_OBJECT(clist), "identities");
            *identities = g_list_append(*identities, ident);
            gtk_object_set_data(GTK_OBJECT(dialog), "identity", NULL);
            identity_list_update(clist);
            /* select just added identity */
            row = gtk_clist_find_row_from_data(clist, ident);
            gtk_clist_select_row(clist, row, -1);
        }
    }
    gtk_widget_destroy(dialog);
}

/*
 * Edit the selected identity in a new dialog.
 * there is always exactly one identity selected - _BROWSE selection mode.
 */
static void 
edit_ident_cb(GtkButton* button, gpointer user_data)
{
    GtkWidget* dialog;
    GtkWindow* parent;
    LibBalsaIdentity* ident;
    GtkCList* clist = GTK_CLIST(user_data);
    GList* list = clist->selection;
    gint row;

    g_return_if_fail(list);
    
    row = GPOINTER_TO_INT(list->data);
    ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    
    parent = gtk_object_get_data(GTK_OBJECT(clist), "parent-window");
    dialog = setup_ident_dialog(parent, FALSE, ident, clist);
    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        if(ident_dialog_update(GTK_DIALOG(dialog)))
            identity_list_update(clist);
    }
    gtk_widget_destroy(dialog);
}


/*
 * Put the required GtkEntries, Labels, and Checkbuttons in the dialog
 * for creating/editing identities.
 */
static GtkWidget*
setup_ident_dialog(GtkWindow* parent, gboolean createp, 
                   LibBalsaIdentity* ident, gpointer clist)
{
     
    GtkWidget* frame = gtk_frame_new(NULL);
    GtkWidget* vbox = gtk_vbox_new(FALSE, padding);
    GtkWidget* main_box, *w;
    GtkDialog* dialog;
    dialog = GTK_DIALOG(gtk_dialog_new_with_buttons(createp 
                                                    ? _("Create Identity")  
                                                    : _("Edit Identity"),
                                                    parent,
                                                    GTK_DIALOG_MODAL,
                                                    createp 
                                                    ? GTK_STOCK_OK 
                                                    : GTK_STOCK_APPLY,
                                                    GTK_RESPONSE_OK,
                                                    GTK_STOCK_CANCEL,
                                                    GTK_RESPONSE_CANCEL,
                                                    NULL));
    main_box = dialog->vbox;
    gtk_object_set_data(GTK_OBJECT(dialog), "clist", clist);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity", ident);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity-edit", 
			(gpointer)!createp);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
#if TO_BE_PORTED
    gnome_dialog_set_parent(dialog, parent);
#endif    
    gtk_box_pack_start(GTK_BOX(main_box), frame, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(frame), vbox);

    gtk_container_set_border_width(GTK_CONTAINER(frame), padding);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), padding);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), padding);
    gtk_object_set_data(GTK_OBJECT(dialog), "box", vbox);

    g_signal_connect (G_OBJECT (dialog), "close", 
                      G_CALLBACK (ident_dialog_cleanup),
                      G_OBJECT (dialog));

    w = ident_dialog_add_entry(dialog, _("Identity Name:"), 
			       "identity-name",
			       ident->identity_name);
    gtk_widget_grab_focus(w);

    ident_dialog_add_entry(dialog, _("Full Name:"), 
                           "identity-fullname",
                           ident->address->full_name);
    
    if (ident->address->address_list != NULL) {
        ident_dialog_add_entry(dialog, _("Mailing Address:"), 
                               "identity-address",
                               (gchar*) ident->address->address_list->data);
    } else {
        ident_dialog_add_entry(dialog, _("Mailing Address:"),
                               "identity-address",
                               NULL);
    }
    
    ident_dialog_add_entry(dialog, _("Reply To:"), 
                           "identity-replyto",
                           ident->replyto);
    ident_dialog_add_entry(dialog, _("Domain:"), 
                           "identity-domain",
                           ident->domain);
    ident_dialog_add_entry(dialog, _("Bcc:"), 
                           "identity-bcc",
                           ident->bcc);
    ident_dialog_add_entry(dialog, _("Reply String:"), 
                           "identity-replystring",
                           ident->reply_string);
    ident_dialog_add_entry(dialog, _("Forward String:"), 
                           "identity-forwardstring",
                           ident->forward_string);
    ident_dialog_add_entry(dialog, _("Signature Path:"), 
                           "identity-sigpath",
                           ident->signature_path);
    
    ident_dialog_add_checkbutton(dialog, _("Execute Signature"),
				"identity-sigexecutable",
				ident->sig_executable);
 
    ident_dialog_add_checkbutton(dialog, _("Include Signature"), 
                                 "identity-sigappend", 
                                 ident->sig_sending);
    ident_dialog_add_checkbutton(dialog, 
                                 _("Include Signature When Forwarding"),
                                 "identity-whenforward", 
                                 ident->sig_whenforward);
    ident_dialog_add_checkbutton(dialog, _("Include Signature When Replying"),
                                 "identity-whenreply", 
                                 ident->sig_whenreply);
    ident_dialog_add_checkbutton(dialog, _("Add Signature Separator"),
                                 "identity-sigseparator", 
                                 ident->sig_separator);
    ident_dialog_add_checkbutton(dialog, _("Prepend Signature"),
                                 "identity-sigprepend",
                                 ident->sig_prepend);
    gtk_widget_show_all(main_box);
    return GTK_WIDGET(dialog);
}


/*
 * Create and add a GtkCheckButton to the given dialog with caption
 * and add a pointer to it stored under the given key.  The check
 * button is initialized to the given value.
 */
static void
ident_dialog_add_checkbutton(GtkDialog* dialog, const gchar* check_label,
                             const gchar* check_key, gboolean init_value)
{
    GtkWidget* check;
    GtkBox* box;

    
    box = GTK_BOX(gtk_object_get_data(GTK_OBJECT(dialog), "box"));
    check = gtk_check_button_new_with_label(check_label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), init_value);
    gtk_box_pack_start(box, check, FALSE, FALSE, 0);
    gtk_object_set_data(GTK_OBJECT(dialog), check_key, check);
    gtk_widget_show(check);
}


/*
 * Add a GtkEntry to the given dialog with a label next to it
 * explaining the contents.  A reference to the entry is stored as
 * object data attached to the dialog with the given key.  The entry
 * is initialized to the init_value given.
 */
static GtkWidget*
ident_dialog_add_entry(GtkDialog* dialog, const gchar* label_name, 
                       const gchar* entry_key, gchar* init_value)
{
    GtkBox* box;
    GtkWidget* label;
    GtkWidget* entry;
    GtkWidget* hbox;


    box = GTK_BOX(gtk_object_get_data(GTK_OBJECT(dialog), "box"));
    label = gtk_label_new(label_name);
    entry = gtk_entry_new();
    hbox = gtk_hbox_new(FALSE, 4);
    
    if (init_value != NULL)
        gtk_entry_set_text(GTK_ENTRY(entry), init_value);

    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(box, hbox, FALSE, FALSE, 4);
    gtk_object_set_data(GTK_OBJECT(dialog), entry_key, entry);
    gtk_widget_show(label);
    gtk_widget_show(entry);
    gtk_widget_show(hbox);
    return entry;
}




/* 
 * Update the identity object associated with the edit/new dialog,
 * validating along the way.  Correct validation results in a true
 * return value, otherwise it returns false.
 */
static gboolean
ident_dialog_update(GtkDialog* dlg)
{
    LibBalsaIdentity* id;
    LibBalsaIdentity* exist_ident;
    LibBalsaAddress* address;
    GtkWidget* error, *clist;
    GList **identities, *list;
    gchar* text;
    
    id = gtk_object_get_data(GTK_OBJECT(dlg), "identity");
    clist      = gtk_object_get_data(GTK_OBJECT(dlg), "clist");
    identities = gtk_object_get_data(GTK_OBJECT(clist), "identities");

    text = ident_dialog_get_text(dlg, "identity-name");
    g_return_val_if_fail(text != NULL, FALSE);

    if (text[0] == '\0') {
        error = 
            gtk_message_dialog_new(GTK_WINDOW(dlg),
                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_CLOSE,
                                   _("Error: The identity does not have a name"));
        gtk_dialog_run(GTK_DIALOG(error));
        gtk_widget_destroy(error);
        return FALSE;
    }

    for (list=*identities; list; list = g_list_next(list)) {
        exist_ident = LIBBALSA_IDENTITY(list->data);
        
        if (g_strcasecmp(exist_ident->identity_name, text) == 0 && 
	    id != exist_ident) {
            error = 
                gtk_message_dialog_new(GTK_WINDOW(dlg),
                                       GTK_DIALOG_MODAL,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_CLOSE,
                                       _("Error: An identity with that name already exists"));
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);
            return FALSE;
        }
    }

    g_free(id->identity_name); id->identity_name = text;

    text = ident_dialog_get_text(dlg, "identity-fullname");
    g_return_val_if_fail(text != NULL, FALSE);
    address = libbalsa_address_new();
    address->full_name = text;
    
    text = ident_dialog_get_text(dlg, "identity-address");
    address->address_list = g_list_append(address->address_list, text);
    libbalsa_identity_set_address(id, address);

    g_free(id->replyto);
    id->replyto         = ident_dialog_get_text(dlg, "identity-replyto");
    g_free(id->domain);
    id->domain          = ident_dialog_get_text(dlg, "identity-domain");    
    g_free(id->bcc);
    id->bcc             = ident_dialog_get_text(dlg, "identity-bcc");
    g_free(id->reply_string);
    id->reply_string    = ident_dialog_get_text(dlg, "identity-replystring");
    g_free(id->forward_string);
    id->forward_string  = ident_dialog_get_text(dlg, "identity-forwardstring");
    g_free(id->signature_path);
    id->signature_path  = ident_dialog_get_text(dlg, "identity-sigpath");
    
    id->sig_executable  = ident_dialog_get_bool(dlg, "identity-sigexecutable");
    id->sig_sending     = ident_dialog_get_bool(dlg, "identity-sigappend");
    id->sig_whenforward = ident_dialog_get_bool(dlg, "identity-whenforward");
    id->sig_whenreply   = ident_dialog_get_bool(dlg, "identity-whenreply");
    id->sig_separator   = ident_dialog_get_bool(dlg, "identity-sigseparator");
    id->sig_prepend     = ident_dialog_get_bool(dlg, "identity-sigprepend");
   
    return TRUE;
}


/* 
 * Get the text from an entry in the editing/creation dialog.  The
 * given key accesses the entry using object data.
 */
static gchar*
ident_dialog_get_text(GtkDialog* dialog, const gchar* key)
{
    GtkEntry* entry;
    gchar* text;
    
    entry = GTK_ENTRY(gtk_object_get_data(GTK_OBJECT(dialog), key));
    text = gtk_editable_get_chars(GTK_EDITABLE(entry), 0, -1);

    return text;
}


/*
 * Get the value of a check button from the editing dialog.  The key
 * is used to retreive the reference to the check button using object
 * data
 */
static gboolean
ident_dialog_get_bool(GtkDialog* dialog, const gchar* key)
{
    GtkCheckButton* button;
    gboolean value;
    
    button = GTK_CHECK_BUTTON(gtk_object_get_data(GTK_OBJECT(dialog), key));
    value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    
    return value;
}

/* 
 * Set the default identity to the currently selected.
 */
static void
set_default_ident_cb(GtkButton* button, gpointer user_data)
{
    LibBalsaIdentity* ident, **default_id;
    GtkCList* clist = GTK_CLIST(user_data);
    GList* list = clist->selection;
    gint row;
    
    /* should never be true with the _BROWSE selection mode
     * as long as there are any elements on the list */
    g_return_if_fail (list);

    row = GPOINTER_TO_INT(list->data);
    default_id = gtk_object_get_data(GTK_OBJECT(clist), "default-id");
    ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    g_return_if_fail(ident);
    *default_id = ident;
    identity_list_update(clist);
}


/*
 * Confirm the deletion of an identity, do the actual deletion here,
 * and close the dialog.
 */
static void
identity_delete_selected(GtkCList* clist)
{
    LibBalsaIdentity* ident;
    GList **identities;
    GList* select = clist->selection;
    gint row = GPOINTER_TO_INT(select->data);

    ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    identities = gtk_object_get_data(GTK_OBJECT(clist), "identities");
    *identities = g_list_remove(*identities, ident);
    identity_list_update(clist);
    gtk_object_destroy(GTK_OBJECT(ident));
}

/* 
 * Delete the currently selected identity after confirming. 
 */
static void
delete_ident_cb(GtkWidget* button, gpointer user_data)
{
    LibBalsaIdentity* ident, **default_id;
    GtkCList* clist = GTK_CLIST(user_data);
    GtkWidget* error;
    GtkWindow* parent;
    GList* select = clist->selection;
    gint row;
    
    row = GPOINTER_TO_INT(select->data);
    ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    default_id = gtk_object_get_data(GTK_OBJECT(clist), "default-id");
    parent = gtk_object_get_data(GTK_OBJECT(clist), "parent-window");
    if (ident == *default_id) {
        error = 
            gtk_message_dialog_new(parent,
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_CLOSE,
            _("Cannot delete default profile, please make another default first."));
        gtk_dialog_run(GTK_DIALOG(error));
        gtk_widget_destroy(error);
        return;
    }
    error = 
        gtk_dialog_new_with_buttons (_("Question"),
                                     parent,
                                     GTK_DIALOG_MODAL|
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_OK,
                                     GTK_RESPONSE_OK,
                                     GTK_STOCK_CANCEL,
                                     GTK_RESPONSE_CANCEL,
                                     NULL);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(error)->vbox),
                      gtk_label_new(_("Do you really want to delete the selected identity?")));
                      
    if(gtk_dialog_run(GTK_DIALOG(error)) == GTK_RESPONSE_OK)
        identity_delete_selected(clist);
}


GtkWidget*
libbalsa_identity_config_dialog(GtkWindow* parent, GList**identities,
				LibBalsaIdentity**default_id)
{
    GtkWidget* dialog;
    GtkWidget* frame = libbalsa_identity_config_frame(FALSE, identities, 
						      default_id);
    GtkWidget* display_frame = libbalsa_identity_display_frame();
    GtkWidget* hbox = gtk_hbox_new(FALSE, padding);
    GtkCList* clist;
    unsigned i;
    const static struct {
        const gchar* label; 
        GCallback cb;
    } buttons[] = {
	{ N_("_New"),         G_CALLBACK(new_ident_cb)         },
	{ N_("_Edit"),        G_CALLBACK(edit_ident_cb)        },
	{ N_("_Set Default"), G_CALLBACK(set_default_ident_cb) },
	{ N_("_Delete"),      G_CALLBACK(delete_ident_cb)      } 
    };
#if TO_BE_PORTED
    const gchar* button_pixmaps[] = {
	GNOME_STOCK_PIXMAP_NEW,
	GNOME_STOCK_PIXMAP_PROPERTIES,
	GNOME_STOCK_PIXMAP_ADD,
	GNOME_STOCK_PIXMAP_REMOVE,
	NULL
    };
#endif
    
    dialog = gtk_dialog_new_with_buttons(_("Manage Identities"),
                                         parent,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_CLOSE,
                                         GTK_RESPONSE_CLOSE,
                                         NULL);
#if TO_BE_PORTED
    gnome_dialog_set_parent(GNOME_DIALOG(dialog), parent);
#endif
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
                       hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), display_frame, TRUE, TRUE, 0);

    clist = GTK_CLIST(gtk_object_get_data(GTK_OBJECT(frame), "clist"));
    gtk_object_set_data(GTK_OBJECT(frame), "clist", NULL);
    gtk_object_set_data(GTK_OBJECT(clist), "parent-window", parent);
    gtk_object_set_data(GTK_OBJECT(clist), "frame", display_frame);
    
    gtk_signal_connect(GTK_OBJECT(clist), "select-row",
                       GTK_SIGNAL_FUNC(config_dialog_select_cb),
                       dialog);

    gtk_widget_show_all(GTK_WIDGET(GTK_DIALOG(dialog)->vbox));

    gtk_widget_grab_focus(GTK_WIDGET(clist));
    /* The identity list has been created prior to the "select-row" signal
     * connection. We have to update the identity info frame by hand. */
    g_return_val_if_fail(clist->selection, dialog);

    for(i=0; i< ELEMENTS(buttons); i++) {
        GtkWidget* butt = gtk_button_new_with_mnemonic(buttons[i].label);
        g_signal_connect(G_OBJECT(butt), "pressed", buttons[i].cb, clist);
        gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->action_area),
                           butt);
    }
    gtk_widget_show_all(GTK_WIDGET(GTK_DIALOG(dialog)->action_area));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    config_dialog_select_cb(clist, 
			    GPOINTER_TO_INT(clist->selection->data), 
			    -1, NULL, dialog);

    return dialog;
}

static void
config_dialog_select_cb(GtkCList* clist, gint row, gint column,
                        GdkEventButton* event, gpointer user_data)
{
    LibBalsaIdentity* ident, **default_id;
    GtkFrame *frame;
#if TO_BE_PORTED
    GtkDialog* dialog = GTK_DIALOG(user_data);
#endif

    ident = LIBBALSA_IDENTITY(gtk_clist_get_row_data(clist, row));
    
    /* disable default identity deletion */
    default_id = gtk_object_get_data(GTK_OBJECT(clist), "default-id");
#if TO_BE_PORTED
    gnome_dialog_set_sensitive(dialog, 3, ident != *default_id);
#endif
    frame = gtk_object_get_data(GTK_OBJECT(clist), "frame");
    g_return_if_fail(frame);
    display_frame_update(frame, ident);
}

static GtkWidget*
libbalsa_identity_display_frame(void)
{
    GtkWidget* frame1 = gtk_frame_new(NULL);
    GtkWidget* vbox1 = gtk_vbox_new(FALSE, padding/2);
    
    gtk_container_add(GTK_CONTAINER(frame1), vbox1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox1), padding);

    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Identity Name:"), "identity-name");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Full Name:"), "identity-fullname");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Mailing Address:"), "identity-address");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Reply To:"), "identity-replyto");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Domain:"), "identity-domain");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Bcc:"), "identity-bcc");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Reply String:"), "identity-replystring");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Forward String:"), "identity-forwardstring");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Signature Path:"), "identity-sigpath");

    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1),
			      _("Execute Signature"), "identity-sigexecutable");

    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Include Signature"), "identity-sigappend");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Include Signature When Forwarding"), 
                              "identity-whenforward");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Include Signature When Replying"), 
                              "identity-whenreply");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Add Signature Separator"), 
                              "identity-sigseparator");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Prepend Signature"), 
                              "identity-sigprepend");
    
    gtk_widget_show_all(frame1);

    return frame1;
}


static void 
display_frame_add_field(GtkFrame* frame, GtkBox* box, 
                        const gchar* name, const gchar* key)
{
    GtkWidget* hbox = gtk_hbox_new(FALSE, 4);
    GtkWidget* label1 = gtk_label_new(name);
    GtkWidget* label2 = gtk_label_new(NULL);
    
    gtk_box_pack_start(box, hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label2, FALSE, FALSE, 0);
    gtk_object_set_data(GTK_OBJECT(frame), key, label2);
}


static void
display_frame_add_boolean(GtkFrame* frame, GtkBox* box,
                        const gchar* name, const gchar* key)
{
    GtkWidget* hbox = gtk_hbox_new(FALSE, 4);
    GtkWidget* label1 = gtk_label_new(NULL);
    GtkWidget* label2 = gtk_label_new(name);
    
    gtk_box_pack_start(box, hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label2, FALSE, FALSE, 0);
    gtk_object_set_data(GTK_OBJECT(frame), key, label1);
}


static void 
display_frame_update(GtkFrame* frame, LibBalsaIdentity* ident)
{
    display_frame_set_field(frame, "identity-name", ident->identity_name);
    display_frame_set_field(frame, "identity-fullname", 
                            ident->address->full_name);
    if (ident->address->address_list)
        display_frame_set_field(frame, "identity-address", 
                                (gchar*)ident->address->address_list->data);
    else
        display_frame_set_field(frame, "identity-address", NULL);
    
    display_frame_set_field(frame, "identity-replyto", ident->replyto);
    display_frame_set_field(frame, "identity-domain", ident->domain);
    display_frame_set_field(frame, "identity-bcc", ident->bcc);
    display_frame_set_field(frame, "identity-replystring", 
                            ident->reply_string);
    display_frame_set_field(frame, "identity-forwardstring", 
                            ident->forward_string);
    display_frame_set_field(frame, "identity-sigpath", ident->signature_path);

    display_frame_set_boolean(frame, "identity-sigexecutable", ident->sig_executable);

    display_frame_set_boolean(frame, "identity-sigappend", ident->sig_sending);
    display_frame_set_boolean(frame, "identity-whenforward", 
                              ident->sig_whenforward);
    display_frame_set_boolean(frame, "identity-whenreply", 
                              ident->sig_whenreply);
    display_frame_set_boolean(frame, "identity-sigseparator", 
                              ident->sig_separator);    
    display_frame_set_boolean(frame, "identity-sigprepend", 
                              ident->sig_prepend);    
}


static void
display_frame_set_field(GtkFrame* frame, const gchar* key, const gchar* value)
{
    GtkLabel* label = GTK_LABEL(gtk_object_get_data(GTK_OBJECT(frame), key));
    
    gtk_label_set_text(label, value);
}

static void
display_frame_set_boolean(GtkFrame* frame, const gchar* key, gboolean value)
{
    GtkLabel* label = GTK_LABEL(gtk_object_get_data(GTK_OBJECT(frame), key));
    
    gtk_label_set_text(label, value ? _("Do") : _("Do Not"));
}


/* libbalsa_identity_new_config:
   factory-type method creating new Identity object from given
   configuration data.
*/
LibBalsaIdentity*
libbalsa_identity_new_config(const gchar* prefix, const gchar* name)
{
    LibBalsaIdentity* ident;
    gchar* tmpstr;
    
    g_return_val_if_fail(prefix != NULL, NULL);

    gnome_config_push_prefix(prefix);

    ident = LIBBALSA_IDENTITY(libbalsa_identity_new_with_name(name));
    ident->address->full_name = gnome_config_get_string("FullName");
    ident->address->address_list = 
        g_list_append(ident->address->address_list, 
                      gnome_config_get_string("Address"));
    ident->replyto = gnome_config_get_string("ReplyTo");
    ident->domain = gnome_config_get_string("Domain");
    ident->bcc = gnome_config_get_string("Bcc");

    /* 
     * these two have defaults, so we need to use the appropriate
     * functions to manage the memory. 
     */
    if ((tmpstr = gnome_config_get_string("ReplyString"))) {
        g_free(ident->reply_string);
        ident->reply_string = tmpstr;
    }
    
    if ((tmpstr = gnome_config_get_string("ForwardString"))) {
        g_free(ident->forward_string);
        ident->forward_string = tmpstr;
    }
    
    ident->signature_path = gnome_config_get_string("SignaturePath");
    ident->sig_executable = gnome_config_get_bool("SigExecutable");
    ident->sig_sending = gnome_config_get_bool("SigSending");
    ident->sig_whenforward = gnome_config_get_bool("SigForward");
    ident->sig_whenreply = gnome_config_get_bool("SigReply");
    ident->sig_separator = gnome_config_get_bool("SigSeparator");
    ident->sig_prepend = gnome_config_get_bool("SigPrepend");
    gnome_config_pop_prefix();

    return ident;
}

void 
libbalsa_identity_save(LibBalsaIdentity* ident, const gchar* prefix)
{
    g_return_if_fail(ident);

    gnome_config_push_prefix(prefix);
    gnome_config_set_string("FullName", ident->address->full_name);
    
    if (ident->address->address_list != NULL)
        gnome_config_set_string("Address", ident->address->address_list->data);

    gnome_config_set_string("ReplyTo", ident->replyto);
    gnome_config_set_string("Domain", ident->domain);
    gnome_config_set_string("Bcc", ident->bcc);
    gnome_config_set_string("ReplyString", ident->reply_string);
    gnome_config_set_string("ForwardString", ident->forward_string);
    gnome_config_set_string("SignaturePath", ident->signature_path);

    gnome_config_set_bool("SigExecutable", ident->sig_executable);
    gnome_config_set_bool("SigSending", ident->sig_sending);
    gnome_config_set_bool("SigForward", ident->sig_whenforward);
    gnome_config_set_bool("SigReply", ident->sig_whenreply);
    gnome_config_set_bool("SigSeparator", ident->sig_separator);
    gnome_config_set_bool("SigPrepend", ident->sig_prepend);

    gnome_config_pop_prefix();
}

