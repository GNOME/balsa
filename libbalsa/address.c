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
#include <libgnome/gnome-i18n.h>
#include <gmime/gmime.h>

#include "address.h"
#include "misc.h"

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
    addr->middle_name = NULL;
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
    g_free(addr->middle_name);  addr->middle_name = NULL;
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


/* returns only first address on the list; ignores remaining ones */
LibBalsaAddress *
libbalsa_address_new_from_string(const gchar * str)
{
    LibBalsaAddress* addr;
    InternetAddressList *list;
    gchar *tmp = g_strdup(str);
    
    libbalsa_utf8_sanitize(&tmp, FALSE, NULL);
    list = internet_address_parse_string(tmp);
    g_free(tmp);
    if (!list)
	return NULL;

    addr = libbalsa_address_new();
    addr->full_name = g_strdup(list->address->name);
    addr->address_list = g_list_append(addr->address_list, 
				       g_strdup(list->address->value.addr));
    internet_address_list_destroy(list);
    return addr;
}

void
libbalsa_address_set_copy(LibBalsaAddress *dest, LibBalsaAddress *src)
{
    GList *src_al;
    if(dest == src) /* safety check */
        return;
    g_free(dest->nick_name); dest->nick_name = g_strdup(src->nick_name);
    g_free(dest->full_name); dest->full_name = g_strdup(src->full_name);
    g_free(dest->middle_name); dest->middle_name = g_strdup(src->middle_name);
    g_free(dest->last_name); dest->last_name = g_strdup(src->last_name);
    g_free(dest->organization);
    dest->organization = g_strdup(src->organization);
    g_list_foreach(dest->address_list, (GFunc)g_free, NULL);
    g_list_free(dest->address_list);

    dest->address_list = NULL;
    for(src_al = src->address_list; src_al; src_al = src_al->next)
        dest->address_list = 
            g_list_prepend(dest->address_list, g_strdup(src_al->data));

    dest->address_list = g_list_reverse(dest->address_list);
}

GList*
libbalsa_address_new_list_from_string(const gchar * str)
{
    LibBalsaAddress* addr;
    InternetAddressList *list, *l;
    GList* lst = NULL;

    gchar *tmp = g_strdup(str);
    
    libbalsa_utf8_sanitize(&tmp, FALSE, NULL);
    l = list = internet_address_parse_string(tmp);
    g_free(tmp);
    if (!list)
	return NULL;

    while(list) {
	addr = libbalsa_address_new();
	addr->full_name = g_strdup(list->address->name);
	addr->address_list = g_list_append(addr->address_list, 
					   g_strdup(list->address->value.addr));
	lst = g_list_append(lst, addr);
	list = list->next;
    }
    internet_address_list_destroy(l);
    return lst;
}

static gboolean
needs_quotes(const gchar *str)
{
    gboolean quoted = FALSE;

    while (*str) {
        if (*str == '\\') {
            if (*++str)
                ++str;
        } else {
            if (*str == '"')
                quoted = !quoted;
            else if (!quoted
                /* RFC 2822 specials, less '"': */
                 && strchr("()<>[]:;@\\,.", *str))
                return TRUE;
            ++str;
        }
    }
    return FALSE;
}

static gchar*
rfc2822_mailbox(const gchar *full_name, gchar *address)
{
    gchar *new_str;

    if(full_name && *full_name) {
        gchar *dequote = g_new(char, strlen(full_name) + 1);
        const gchar *p = full_name;
        gchar *q = dequote;
    
        do {
            if (*p == '\\') {
                *q++ = *p++;
                if (*p)
                    *q++ = *p++;
            } else if (*p == '"')
                ++p;
            else
                *q++ = *p++;
        } while (*p);
        *q = '\0';

        if (needs_quotes(dequote))
	    new_str = g_strdup_printf("\042%s\042 <%s>", dequote, address);
        else
            new_str = g_strdup_printf("%s <%s>", dequote, address);
        g_free(dequote);
    } else
	new_str = g_strdup(address);
    return new_str;
}


#if 1
static gchar*
rfc2822_group(const gchar *full_name, GList *addr_list)
{
    GString *str = g_string_new("");
    GList *addr_entry;
    gchar *res;

    if(full_name) { 
	if(needs_quotes(full_name))
	    g_string_printf(str, "\042%s\042: ", full_name);
	else
	    g_string_printf(str, "%s: ", full_name);
    }

    if(addr_list) {
	g_string_append(str, (gchar*)addr_list->data);
        
	for(addr_entry=g_list_next(addr_list); addr_entry; 
	    addr_entry=g_list_next(addr_entry)) {
	    g_string_append_printf(str, ", %s", (gchar*)addr_entry->data);
	}
    }
    if(full_name)
	g_string_append(str, ";");
    
    res=str->str;
    g_string_free(str, FALSE);
    
    return res;
}
#endif

#if 0
static gchar*
rfc2822_list(GList *list)
{
    gchar *retc = NULL; 
    GString *str;
    GList *addr_entry;
    
    g_return_val_if_fail(list!=NULL, NULL);

    str=g_string_new((gchar *)list->data);

    for(addr_entry=g_list_next(list); addr_entry; 
	addr_entry=g_list_next(addr_entry)) {
	g_string_append_printf(str, ", %s", (gchar *)addr_entry->data);
    }
    retc=str->str;
    g_string_free(str, FALSE);

    return retc;
}
#endif

/* private version */
gchar *
libbalsa_address_to_gchar_p(LibBalsaAddress * address, gint n);
gchar *
libbalsa_address_to_gchar_p(LibBalsaAddress * address, gint n)
{
    gchar *retc = NULL;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    /* FIXME: for n==-1, we should be returning nice rfc822_group but
     * the entry widgets have not proper support for group string format
     * so we drop this idea for a while. */
    if(!address->address_list)
        return NULL;
    if(n==-1) {
        if(address->address_list->next)
            retc = rfc2822_group(address->full_name, address->address_list);
        else
            retc = rfc2822_mailbox(address->full_name,
                                   address->address_list->data);
    } else {
	GList *nth_address = g_list_nth(address->address_list, n);
	g_return_val_if_fail(nth_address != NULL, NULL);

	retc = rfc2822_mailbox(address->full_name, nth_address->data);
    }

    return retc;
}

/* 
   Get a string version of this address.

   If n == -1 then return all addresses, else return the n'th one.
   If n > the number of addresses, will cause an error.
*/
gchar *
libbalsa_address_to_gchar(LibBalsaAddress * address, gint n)
{
    return libbalsa_address_to_gchar_p(address, n);
}

const gchar *
libbalsa_address_get_name(const LibBalsaAddress * addr)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(addr), NULL);

    return addr->full_name ? addr->full_name :
	(addr->address_list ? addr->address_list->data : NULL);
}

#if ENABLE_ESMTP

/* XXX - added by Brian Stafford <brian@stafford.uklinux.net> */

/* libESMTP works with the RFC 821 mailbox and the RFC 822 phrase and 
   mailbox as seperate entities.  Because of this it is useful to add
   these extra methods. */

/* Extract the RFC 822 phrase from the address.  Almost the same
   as libbalsa_address_get_name() except returns NULL if no phrase. */
const gchar *
libbalsa_address_get_phrase(LibBalsaAddress * address)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    return address->full_name;
}

#endif
/* Extract the nth RFC 821/RFC 822 mailbox from the address. */
const gchar *
libbalsa_address_get_mailbox(LibBalsaAddress * address, gint n)
{
    GList *nth_address;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    nth_address = g_list_nth(address->address_list, n);
    if(nth_address == NULL) return NULL;
    return (const gchar*)nth_address->data;
}


/* =================================================================== */
/*                                UI PART                              */
/* =================================================================== */

/** libbalsa_address_get_edit_widget() returns an widget adapted
    for a LibBalsaAddress edition, with initial values set if address
    is provided. The edit entries are set in entries array 
    and enumerated with LibBalsaAddressField constants
*/
GtkWidget*
libbalsa_address_get_edit_widget(LibBalsaAddress *address, GtkWidget **entries,
                                 GCallback changed_cb, gpointer changed_data)
{
    const static gchar *labels[NUM_FIELDS] = {
	N_("_Displayed Name:"),
	N_("_First Name:"),
	N_("_Middle Name:"),
	N_("_Last Name:"),
	N_("_Nickname:"),
	N_("O_rganization:"),
	N_("_Email Address:")
    };

    GtkWidget *table, *label;
    gchar *new_name = NULL;
    gchar *new_email = NULL;
    gchar *new_organization = NULL;
    gchar *first_name = NULL;
    gchar *middle_name = NULL;
    gchar *last_name = NULL;
    gchar *carrier = NULL;
    gint cnt, cnt2;

    new_email = g_strdup(address ? address->address_list->data : ""); 
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

	/* guess the first name, middle name and last name */
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

	    g_strfreev(names);
	}
    }

    if (first_name == NULL)
	first_name = g_strdup("");
    if (middle_name == NULL)
	middle_name = g_strdup("");
    if (last_name == NULL)
	last_name = g_strdup("");


    table = gtk_table_new(NUM_FIELDS, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 3);

    for (cnt = 0; cnt < NUM_FIELDS; cnt++) {
	label = gtk_label_new_with_mnemonic(_(labels[cnt]));
	gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        entries[cnt] = gtk_entry_new();
        if(changed_cb)
            g_signal_connect(G_OBJECT(entries[cnt]), "changed",
                             changed_cb, changed_data);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), entries[cnt]);

	gtk_table_attach(GTK_TABLE(table), label, 0, 1, cnt + 1, cnt + 2,
			 GTK_FILL, GTK_FILL, 4, 4);

	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);

	gtk_table_attach(GTK_TABLE(table), entries[cnt], 1, 2, cnt + 1,
			 cnt + 2, GTK_FILL | GTK_EXPAND,
			 GTK_FILL | GTK_EXPAND, 2, 2);
    }

    gtk_entry_set_text(GTK_ENTRY(entries[FULL_NAME]), new_name);
    gtk_entry_set_text(GTK_ENTRY(entries[FIRST_NAME]), first_name);
    gtk_entry_set_text(GTK_ENTRY(entries[MIDDLE_NAME]), middle_name);
    gtk_entry_set_text(GTK_ENTRY(entries[LAST_NAME]), last_name);
    gtk_entry_set_text(GTK_ENTRY(entries[EMAIL_ADDRESS]), new_email);
    gtk_entry_set_text(GTK_ENTRY(entries[ORGANIZATION]), new_organization);

    gtk_editable_select_region(GTK_EDITABLE(entries[FULL_NAME]), 0, -1);

    for (cnt = FULL_NAME + 1; cnt < NUM_FIELDS; cnt++)
        gtk_editable_set_position(GTK_EDITABLE(entries[cnt]), 0);

    g_free(new_name);
    g_free(first_name);
    g_free(middle_name);
    g_free(last_name);
    g_free(new_email);
    g_free(new_organization);
    return table;
}

LibBalsaAddress*
libbalsa_address_new_from_edit_entries(GtkWidget **entries)
{
#define SET_FIELD(f,e)\
  do{ (f) = g_strstrip(gtk_editable_get_chars(GTK_EDITABLE(e), 0, -1));\
      if( !(f) || !*(f)) { g_free(f); (f) = NULL; }                    \
 else { while( (p=strchr(address->full_name,';'))) *p = ','; }  } while(0)

    LibBalsaAddress *address;
    char *p, *addr;
    /* FIXME: This problem should be solved in the VCard
       implementation in libbalsa: semicolons mess up how GnomeCard
       processes the fields, so disallow them and replace them
       by commas. */

    address = libbalsa_address_new();
    SET_FIELD(address->full_name,   entries[FULL_NAME]);
    SET_FIELD(address->first_name,  entries[FIRST_NAME]);
    SET_FIELD(address->middle_name, entries[MIDDLE_NAME]);
    SET_FIELD(address->last_name,   entries[LAST_NAME]);
    SET_FIELD(address->nick_name,   entries[NICK_NAME]);
    SET_FIELD(address->organization,entries[ORGANIZATION]);
    SET_FIELD(addr,                 entries[EMAIL_ADDRESS]);

    if(addr)
        address->address_list = g_list_append(address->address_list,addr);
    return address;
}
