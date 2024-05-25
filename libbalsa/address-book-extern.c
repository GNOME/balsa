/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
  A external source (program opened via popen) address book
  If you do not have ant appriopriate program installed, use
  following script:
  #! /bin/sh 
  f=$HOME/.extern-addrbook 
  if test "$1" != ""; then
  perl -ne 'BEGIN{print "#ldb '"$1"'\n"} print if /'"$1"'/i;' "$f"
  else
  echo "#ldb"
  cat "$f"
  fi
 
  where $HOME/.extern-addrbook contains:
  mailbox1@example.comTABnameTABx
  ...
*/

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#define _POSIX_C_SOURCE 2
#include "address-book-extern.h"

#include "libbalsa-conf.h"
#include <glib/gi18n.h>

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "address-book"

/* FIXME: Arbitrary constant */
#define LINE_LEN 256
#define LINE_LEN_STR "256"

static void libbalsa_address_book_externq_finalize(GObject * object);

static LibBalsaABErr libbalsa_address_book_externq_load(LibBalsaAddressBook* ab, 
                                                        const gchar *filter,
                                                        LibBalsaAddressBookLoadFunc 
                                                        callback, 
                                                        gpointer closure);
static LibBalsaABErr libbalsa_address_book_externq_add_address(LibBalsaAddressBook *ab,
                                                               LibBalsaAddress *address);

static LibBalsaABErr libbalsa_address_book_externq_remove_address(LibBalsaAddressBook *ab,
                                                                  LibBalsaAddress *address);

static LibBalsaABErr libbalsa_address_book_externq_modify_address(LibBalsaAddressBook *ab,
                                                                  LibBalsaAddress *address,
                                                                  LibBalsaAddress *newval);

static void libbalsa_address_book_externq_save_config(LibBalsaAddressBook *ab,
                                                      const gchar * prefix);
static void libbalsa_address_book_externq_load_config(LibBalsaAddressBook *ab,
                                                      const gchar * prefix);

static gboolean parse_externq_file(LibBalsaAddressBookExternq *ab_externq,
                                   gchar *pattern,
                                   void (*cb)(const gchar*,const gchar*,void*),
                                   void *data);

static GList *libbalsa_address_book_externq_alias_complete(LibBalsaAddressBook *ab, 
                                                           const gchar * prefix);

struct _LibBalsaAddressBookExternq {
    LibBalsaAddressBook parent;

    gchar *load;
    gchar *save;

    GList *address_list;

    time_t mtime;
};

G_DEFINE_TYPE(LibBalsaAddressBookExternq, libbalsa_address_book_externq,
        LIBBALSA_TYPE_ADDRESS_BOOK)

static void
libbalsa_address_book_externq_class_init(LibBalsaAddressBookExternqClass *
                                         klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->finalize = libbalsa_address_book_externq_finalize;

    address_book_class->load = libbalsa_address_book_externq_load;
    address_book_class->add_address =
	libbalsa_address_book_externq_add_address;
    address_book_class->remove_address =
	libbalsa_address_book_externq_remove_address;
    address_book_class->modify_address =
	libbalsa_address_book_externq_modify_address;

    address_book_class->save_config =
	libbalsa_address_book_externq_save_config;
    address_book_class->load_config =
	libbalsa_address_book_externq_load_config;

    address_book_class->alias_complete =
	libbalsa_address_book_externq_alias_complete;

}

static void
libbalsa_address_book_externq_init(LibBalsaAddressBookExternq * ab_externq)
{
    ab_externq->load = NULL;
    ab_externq->save = NULL;
    ab_externq->address_list = NULL;
    ab_externq->mtime = 0;
}

static void
libbalsa_address_book_externq_finalize(GObject * object)
{
    LibBalsaAddressBookExternq *ab_externq;

    ab_externq = LIBBALSA_ADDRESS_BOOK_EXTERNQ(object);

    g_free(ab_externq->load);
    g_free(ab_externq->save);

    g_list_free_full(ab_externq->address_list, g_object_unref);
    ab_externq->address_list = NULL;

    G_OBJECT_CLASS(libbalsa_address_book_externq_parent_class)->finalize(object);
}

LibBalsaAddressBook *
libbalsa_address_book_externq_new(const gchar * name, const gchar * load,
                                  const gchar * save)
{
    LibBalsaAddressBookExternq *ab_externq;
    LibBalsaAddressBook *ab;

    ab_externq =
        LIBBALSA_ADDRESS_BOOK_EXTERNQ(g_object_new
                                     (LIBBALSA_TYPE_ADDRESS_BOOK_EXTERNQ,
                                      NULL));
    ab = LIBBALSA_ADDRESS_BOOK(ab_externq);

    libbalsa_address_book_set_name(ab, name);
    ab_externq->load = g_strdup(load);
    ab_externq->save = g_strdup(save);

    return ab;
}

struct lbe_load_data {
    LibBalsaAddressBook *ab;
    LibBalsaAddressBookLoadFunc callback;
    gpointer closure;
};

static void
lbe_load_cb(const gchar *email, const gchar *name, void *data)
{
    struct lbe_load_data *d = (struct lbe_load_data*)data;
    LibBalsaAddress *address = libbalsa_address_new();

    /* The extern database doesn't support Id's, sorry! */
    libbalsa_address_set_nick_name(address, _("No-Id"));
    libbalsa_address_append_addr(address, email);
    libbalsa_address_set_full_name(address, name != NULL ? name : _("No-Name"));

    d->callback(d->ab, address, d->closure);

    g_object_unref(address);
}

static LibBalsaABErr
libbalsa_address_book_externq_load(LibBalsaAddressBook * ab, 
                                   const gchar *filter,
                                   LibBalsaAddressBookLoadFunc callback, 
                                   gpointer closure)
{
    LibBalsaAddressBookExternq *ab_externq = LIBBALSA_ADDRESS_BOOK_EXTERNQ(ab);
    gboolean rc = TRUE;
    struct lbe_load_data data;

    /* Erase the current address list */
    g_list_free_full(ab_externq->address_list, g_object_unref);
    ab_externq->address_list = NULL;
    if(callback) {
        data.ab = ab;
        data.callback = callback;
        data.closure  = closure;
        rc = parse_externq_file(ab_externq, " ", lbe_load_cb, &data);
        callback(ab, NULL, closure);
    }
    return rc ? LBABERR_OK : LBABERR_CANNOT_READ;
}

static gboolean
parse_externq_file(LibBalsaAddressBookExternq *ab_externq,
                   gchar *pattern,
                   void (*cb)(const gchar *, const gchar *, void*),
                   void *data)
{
    FILE *gc;
    gchar string[LINE_LEN];
    char name[LINE_LEN + 1], email[LINE_LEN + 1], tmp[LINE_LEN + 1];
    gchar command[LINE_LEN];

    /* Start the program */
    g_snprintf(command, sizeof(command), "%s \"%s\"",
               ab_externq->load, pattern);

    gc = popen(command,"r");

    if (gc == NULL) 
        return FALSE;

    if (fgets(string, sizeof(string), gc)) {
    /* The first line should be junk, just debug output */
        g_debug("%s", string);
    }  /* FIXME check error */
	
    while (fgets(string, sizeof(string), gc)) {
        int i=sscanf(string, "%" LINE_LEN_STR "[^\t]\t"
                             "%" LINE_LEN_STR "[^\t]"
                             "%" LINE_LEN_STR "[^\n]",
                             email, name, tmp);
        g_debug("%s =>%i", string, i);
        if(i<2) continue;
        g_debug("%s,%s,%s",email,name,tmp);
        cb(email, name, data);
    }
    pclose(gc);
    
    return TRUE;
}

static LibBalsaABErr
libbalsa_address_book_externq_add_address(LibBalsaAddressBook * ab,
                                          LibBalsaAddress * new_address)
{
    gchar command[LINE_LEN];
    LibBalsaAddressBookExternq *ab_externq;
    FILE *gc; 
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERNQ(ab), LBABERR_OK);

    ab_externq = LIBBALSA_ADDRESS_BOOK_EXTERNQ(ab);
    if (ab_externq->save != NULL) {
        const gchar *addr;
        const gchar *full_name;

        addr      = libbalsa_address_get_addr(new_address);
        full_name = libbalsa_address_get_full_name(new_address);

        g_snprintf(command, sizeof(command), "%s \"%s\" \"%s\" \"%s\"",
                   ab_externq->save,
                   addr,
                   full_name, "TODO");
        if ((gc = popen(command, "r")) == NULL)
            return LBABERR_CANNOT_WRITE;
        if (pclose(gc) != 0)
            return LBABERR_CANNOT_WRITE;

        return LBABERR_OK;
    } else return LBABERR_CANNOT_WRITE;
}

static LibBalsaABErr
libbalsa_address_book_externq_remove_address(LibBalsaAddressBook *ab,
                                             LibBalsaAddress *address)
{
    /* FIXME: implement */
    return LBABERR_CANNOT_WRITE;
}

static LibBalsaABErr
libbalsa_address_book_externq_modify_address(LibBalsaAddressBook *ab,
                                             LibBalsaAddress *address,
                                             LibBalsaAddress *newval)
{
    /* FIXME: implement */
    return LBABERR_CANNOT_WRITE;
}

static void
libbalsa_address_book_externq_save_config(LibBalsaAddressBook * ab,
                                         const gchar * prefix)
{
    LibBalsaAddressBookExternq *ab_externq;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERNQ(ab));

    ab_externq = LIBBALSA_ADDRESS_BOOK_EXTERNQ(ab);

    libbalsa_conf_set_string("Load", ab_externq->load);
    libbalsa_conf_set_string("Save", ab_externq->save);

    if (LIBBALSA_ADDRESS_BOOK_CLASS(libbalsa_address_book_externq_parent_class)->save_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(libbalsa_address_book_externq_parent_class)->save_config(ab, prefix);
}

static void
libbalsa_address_book_externq_load_config(LibBalsaAddressBook * ab,
                                         const gchar * prefix)
{
    LibBalsaAddressBookExternq *ab_externq;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERNQ(ab));

    ab_externq = LIBBALSA_ADDRESS_BOOK_EXTERNQ(ab);

    g_free(ab_externq->load);
    ab_externq->load = libbalsa_conf_get_string("Load");
    ab_externq->save = libbalsa_conf_get_string("Save");

    if (LIBBALSA_ADDRESS_BOOK_CLASS(libbalsa_address_book_externq_parent_class)->load_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(libbalsa_address_book_externq_parent_class)->load_config(ab, prefix);
}

static void
lbe_expand_cb(const gchar *email, const gchar *name, void *d)
{
    GList **res = (GList**)d;
    if(email && *email) {
        if(!name || !*name)
            name = _("No-Name");
        *res = g_list_prepend(*res,
                              internet_address_mailbox_new(name, email));
    }
}

static GList*
libbalsa_address_book_externq_alias_complete(LibBalsaAddressBook * ab,
                                            const gchar * prefix)
{
    LibBalsaAddressBookExternq *ab_externq;
    GList *res = NULL;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERNQ(ab), NULL);

    ab_externq = LIBBALSA_ADDRESS_BOOK_EXTERNQ(ab);

    if (!libbalsa_address_book_get_expand_aliases(ab))
	return NULL;

    if (!parse_externq_file(ab_externq, (gchar *)prefix, lbe_expand_cb, &res))
        return NULL;

    res = g_list_reverse(res);

    return res;
}

/*
 * Getters
 */

const gchar *
libbalsa_address_book_externq_get_load(LibBalsaAddressBookExternq * ab_externq)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERNQ(ab_externq), NULL);

    return ab_externq->load;
}

const gchar *
libbalsa_address_book_externq_get_save(LibBalsaAddressBookExternq * ab_externq)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERNQ(ab_externq), NULL);

    return ab_externq->save;
}

/*
 * Setters
 */

void
libbalsa_address_book_externq_set_load(LibBalsaAddressBookExternq * ab_externq,
                                       const gchar                * load)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERNQ(ab_externq));

    g_free(ab_externq->load);
    ab_externq->load = g_strdup(load);
}

void
libbalsa_address_book_externq_set_save(LibBalsaAddressBookExternq * ab_externq,
                                       const gchar                * save)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERNQ(ab_externq));

    g_free(ab_externq->save);
    ab_externq->save = g_strdup(save);
}
