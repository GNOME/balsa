/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

#include "config.h"

#include <string.h>
#include <gmime/gmime.h>

#include "address.h"
#include "misc.h"
#include "i18n.h"

static GObjectClass *parent_class;

static void libbalsa_address_class_init(LibBalsaAddressClass * klass);
static void libbalsa_address_init(LibBalsaAddress * ab);
static void libbalsa_address_finalize(GObject * object);

GType libbalsa_address_get_type(void)
{
    static GType address_type = 0;

    if (!address_type) {
	static const GTypeInfo address_info = {
	    sizeof(LibBalsaAddressClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_address_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaAddress),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_address_init
	};

	address_type =
	    g_type_register_static(G_TYPE_OBJECT,
	                           "LibBalsaAddress",
                                   &address_info, 0);
    }

    return address_type;
}

static void
libbalsa_address_class_init(LibBalsaAddressClass * klass)
{
    GObjectClass *object_class;

    parent_class = g_type_class_peek_parent(klass);

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = libbalsa_address_finalize;
}

static void
libbalsa_address_init(LibBalsaAddress * addr)
{
    addr->nick_name = NULL;
    addr->full_name = NULL;
    addr->first_name = NULL;
    addr->last_name = NULL;
    addr->organization = NULL;
    addr->address_list = NULL;
}

static void
libbalsa_address_finalize(GObject * object)
{
    LibBalsaAddress *addr;

    g_return_if_fail(object != NULL);

    addr = LIBBALSA_ADDRESS(object);

    g_free(addr->nick_name);    addr->nick_name = NULL;
    g_free(addr->full_name);    addr->full_name = NULL;
    g_free(addr->first_name);   addr->first_name = NULL;
    g_free(addr->last_name);    addr->last_name = NULL;
    g_free(addr->organization); addr->organization = NULL;

    g_list_foreach(addr->address_list, (GFunc) g_free, NULL);
    g_list_free(addr->address_list);
    addr->address_list = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaAddress *
libbalsa_address_new(void)
{
    return g_object_new(LIBBALSA_TYPE_ADDRESS, NULL);
}

void
libbalsa_address_set_copy(LibBalsaAddress * dest, LibBalsaAddress * src)
{
    GList *src_al, *dst_al;

    if (dest == src)            /* safety check */
        return;

    g_free(dest->nick_name);
    dest->nick_name = g_strdup(src->nick_name);
    g_free(dest->full_name);
    dest->full_name = g_strdup(src->full_name);
    g_free(dest->first_name);
    dest->first_name = g_strdup(src->first_name);
    g_free(dest->last_name);
    dest->last_name = g_strdup(src->last_name);
    g_free(dest->organization);
    dest->organization = g_strdup(src->organization);
    g_list_foreach(dest->address_list, (GFunc) g_free, NULL);
    g_list_free(dest->address_list);

    dst_al = NULL;
    for (src_al = src->address_list; src_al; src_al = src_al->next)
        dst_al = g_list_prepend(dst_al, g_strdup(src_al->data));
    dest->address_list = g_list_reverse(dst_al);
}

static gchar *
rfc2822_mailbox(const gchar * full_name, const gchar * address)
{
    InternetAddress *ia;
    gchar *new_str;

    ia = internet_address_new_name(full_name, address);
    new_str = internet_address_to_string(ia, FALSE);
    internet_address_unref(ia);

    return new_str;
}

static gchar*
rfc2822_group(const gchar *full_name, GList *addr_list)
{
    InternetAddress *ia;
    gchar *res;

    ia = internet_address_new_group(full_name);
    for (; addr_list; addr_list = addr_list->next) {
	InternetAddress *member;

	member = internet_address_new_name(NULL, addr_list->data);
	internet_address_add_member(ia, member);
	internet_address_unref(member);
    }
    res = internet_address_to_string(ia, FALSE);
    internet_address_unref(ia);

    return res;
}

/* 
   Get a string version of this address.

   If n == -1 then return all addresses, else return the n'th one.
   If n > the number of addresses, will cause an error.
*/
gchar *
libbalsa_address_to_gchar(LibBalsaAddress * address, gint n)
{
    gchar *retc = NULL;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    if(!address->address_list)
        return NULL;
    if(n==-1) {
        if(address->address_list->next)
            retc = rfc2822_group(address->full_name, address->address_list);
        else
            retc = rfc2822_mailbox(address->full_name,
                                   address->address_list->data);
    } else {
	const gchar *mailbox = g_list_nth_data(address->address_list, n);
	g_return_val_if_fail(mailbox != NULL, NULL);

	retc = rfc2822_mailbox(address->full_name, mailbox);
    }

    return retc;
}

/* Helper */
static const gchar *
lba_get_name_or_mailbox(const InternetAddressList * address_list,
                        gboolean get_name, gboolean in_group)
{
    const gchar *retval = NULL;

    for (; address_list; address_list = address_list->next) {
        InternetAddress *ia = address_list->address;

        if (get_name && ia->name)
            return ia->name;

        if (ia->type == INTERNET_ADDRESS_NAME)
            retval = ia->value.addr;
        else if (ia->type == INTERNET_ADDRESS_GROUP) {
            if (in_group)
                g_message("Ignoring nested group address");
            else
                retval = lba_get_name_or_mailbox(ia->value.members,
			get_name, TRUE);
        }
        if (retval)
            break;
    }

    return retval;
}

/* Get either a name or a mailbox from an InternetAddressList. */
const gchar *
libbalsa_address_get_name_from_list(const InternetAddressList *
                                    address_list)
{
    return lba_get_name_or_mailbox(address_list, TRUE, FALSE);
}

/* Get a mailbox from an InternetAddressList. */
const gchar *
libbalsa_address_get_mailbox_from_list(const InternetAddressList *
                                       address_list)
{
    return lba_get_name_or_mailbox(address_list, FALSE, FALSE);
}

/* =================================================================== */
/*                                UI PART                              */
/* =================================================================== */

/** libbalsa_address_set_edit_entries() initializes the GtkEntry widgets
    in entries with values from address
*/
void
libbalsa_address_set_edit_entries(LibBalsaAddress * address,
                                  GtkWidget **entries)
{
    gchar *new_name = NULL;
    gchar *new_email = NULL;
    gchar *new_organization = NULL;
    gchar *first_name = NULL;
    gchar *last_name = NULL;
    gint cnt;

    new_email = g_strdup(address
                         && address->address_list
                         && address->address_list->data ?
                         address->address_list->data : "");
    /* initialize the organization... */
    if (!address || address->organization == NULL)
	new_organization = g_strdup("");
    else
	new_organization = g_strdup(address->organization);

    /* if the message only contains an e-mail address */
    if (!address || address->full_name == NULL)
	new_name = g_strdup(new_email);
    else {
        gchar **names;
        g_assert(address);
	/* make sure address->personal is not all whitespace */
	new_name = g_strstrip(g_strdup(address->full_name));

	/* guess the first name and last name */
	if (*new_name != '\0') {
	    names = g_strsplit(new_name, " ", 0);

	    for (cnt=0; names[cnt]; cnt++)
		;

	    /* get first name */
	    first_name = g_strdup(address->first_name 
                                  ? address->first_name : names[0]);

	    /* get last name */
            if(address->last_name)
                last_name = g_strdup(address->last_name);
            else {
                if (cnt == 1)
                    last_name = g_strdup("");
                else
                    last_name = g_strdup(names[cnt - 1]);
            }
#if 0
	    /* get middle name */
	    middle_name = g_strdup("");

	    cnt2 = 1;
	    if (cnt > 2)
		while (cnt2 != cnt - 1) {
		    carrier = middle_name;
		    middle_name = g_strconcat(middle_name, names[cnt2++], NULL);
		    g_free(carrier);

		    if (cnt2 != cnt - 1) {
			carrier = middle_name;
			middle_name = g_strconcat(middle_name, " ", NULL);
			g_free(carrier);
		    }
		}
#endif
	    g_strfreev(names);
	}
    }

    if (first_name == NULL)
	first_name = g_strdup("");
    if (last_name == NULL)
	last_name = g_strdup("");

    /* Full name must be set after first and last names. */
    gtk_entry_set_text(GTK_ENTRY(entries[FIRST_NAME]), first_name);
    gtk_entry_set_text(GTK_ENTRY(entries[LAST_NAME]), last_name);
    gtk_entry_set_text(GTK_ENTRY(entries[FULL_NAME]), new_name);
    gtk_entry_set_text(GTK_ENTRY(entries[ORGANIZATION]), new_organization);

    if (address) {
        GtkListStore *store =
            GTK_LIST_STORE(gtk_tree_view_get_model
                           (GTK_TREE_VIEW(entries[EMAIL_ADDRESS])));
        GList *list;

        gtk_list_store_clear(store);
        for (list = address->address_list; list; list = list->next) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, list->data, -1);
        }
    }

    gtk_editable_select_region(GTK_EDITABLE(entries[FULL_NAME]), 0, -1);

    for (cnt = FULL_NAME + 1; cnt < NUM_FIELDS; cnt++)
        if (GTK_IS_EDITABLE(entries[cnt]))
            gtk_editable_set_position(GTK_EDITABLE(entries[cnt]), 0);

    g_free(new_name);
    g_free(first_name);
    g_free(last_name);
    g_free(new_email);
    g_free(new_organization);
    gtk_widget_grab_focus(entries[FULL_NAME]);
}

/** libbalsa_address_get_edit_widget() returns an widget adapted
    for a LibBalsaAddress edition, with initial values set if address
    is provided. The edit entries are set in entries array 
    and enumerated with LibBalsaAddressField constants
*/
static void
lba_entry_changed(GtkEntry * entry, GtkEntry ** entries)
{
    gchar *full_name =
        g_strconcat(gtk_entry_get_text(entries[FIRST_NAME]), " ",
                    gtk_entry_get_text(entries[LAST_NAME]), NULL);
    gtk_entry_set_text(entries[FULL_NAME], full_name);
    g_free(full_name);
}

static void
lba_cell_edited(GtkCellRendererText * cell, const gchar * path_string,
                const gchar * new_text, GtkListStore * store)
{
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store),
                                            &iter, path_string))
        gtk_list_store_set(store, &iter, 0, new_text, -1);
}

static GtkWidget *
lba_address_list_widget(GCallback changed_cb, gpointer changed_data)
{
    GtkListStore *store;
    GtkWidget *tree_view;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    store = gtk_list_store_new(1, G_TYPE_STRING);
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), FALSE);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "editable", TRUE, NULL);
    g_signal_connect(renderer, "edited", G_CALLBACK(lba_cell_edited),
                     store);
    if (changed_cb)
        g_signal_connect_swapped(renderer, "edited",
                                 changed_cb, changed_data);

    column = gtk_tree_view_column_new_with_attributes(NULL, renderer,
                                                      "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    return tree_view;
}

GtkWidget*
libbalsa_address_get_edit_widget(LibBalsaAddress *address, GtkWidget **entries,
                                 GCallback changed_cb, gpointer changed_data)
{
    const static gchar *labels[NUM_FIELDS] = {
	N_("_Displayed Name:"),
	N_("_First Name:"),
	N_("_Last Name:"),
	N_("_Nickname:"),
	N_("O_rganization:"),
        N_("_Email Address:")
    };

    GtkWidget *table, *label;
    gint cnt;

    table = gtk_table_new(NUM_FIELDS, 2, FALSE);
#define HIG_PADDING 6
    gtk_table_set_row_spacings(GTK_TABLE(table), HIG_PADDING);
    gtk_table_set_col_spacings(GTK_TABLE(table), HIG_PADDING);
    gtk_container_set_border_width(GTK_CONTAINER(table), HIG_PADDING);

    for (cnt = 0; cnt < NUM_FIELDS; cnt++) {
        if (!labels[cnt])
            continue;
	label = gtk_label_new_with_mnemonic(_(labels[cnt]));
	gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.0);
        if (cnt == EMAIL_ADDRESS)
            entries[cnt] = lba_address_list_widget(changed_cb,
                                                   changed_data);
        else {
            entries[cnt] = gtk_entry_new();
            if (changed_cb)
                g_signal_connect_swapped(entries[cnt], "changed",
                                         changed_cb, changed_data);
        }
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), entries[cnt]);

	gtk_table_attach(GTK_TABLE(table), label, 0, 1, cnt + 1, cnt + 2,
			 GTK_FILL, GTK_FILL, 4, 4);

	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);

	gtk_table_attach(GTK_TABLE(table), entries[cnt], 1, 2, cnt + 1,
			 cnt + 2, GTK_FILL | GTK_EXPAND,
			 GTK_FILL | GTK_EXPAND, 2, 2);
    }
    g_signal_connect(entries[FIRST_NAME], "changed",
                     G_CALLBACK(lba_entry_changed), entries);
    g_signal_connect(entries[LAST_NAME], "changed",
                     G_CALLBACK(lba_entry_changed), entries);

    libbalsa_address_set_edit_entries(address, entries);

    return table;
}

LibBalsaAddress *
libbalsa_address_new_from_edit_entries(GtkWidget ** entries)
{
#define SET_FIELD(f,e)\
  do{ (f) = g_strstrip(gtk_editable_get_chars(GTK_EDITABLE(e), 0, -1));\
      if( !(f) || !*(f)) { g_free(f); (f) = NULL; }                    \
 else { while( (p=strchr(address->full_name,';'))) *p = ','; }  } while(0)

    LibBalsaAddress *address;
    char *p;
    GList *list = NULL;
    GtkTreeModel *model;
    gboolean valid;
    GtkTreeIter iter;

    /* FIXME: This problem should be solved in the VCard
       implementation in libbalsa: semicolons mess up how GnomeCard
       processes the fields, so disallow them and replace them
       by commas. */

    address = libbalsa_address_new();
    SET_FIELD(address->full_name,   entries[FULL_NAME]);
    SET_FIELD(address->first_name,  entries[FIRST_NAME]);
    SET_FIELD(address->last_name,   entries[LAST_NAME]);
    SET_FIELD(address->nick_name,   entries[NICK_NAME]);
    SET_FIELD(address->organization,entries[ORGANIZATION]);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(entries[EMAIL_ADDRESS]));
    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        gchar *email;

        gtk_tree_model_get(model, &iter, 0, &email, -1);
        if (email && *email)
            list = g_list_prepend(list, email);
    }
    address->address_list = g_list_reverse(list);

    return address;
}
