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
 * LibBalsaAddressBookText
 * 
 * The code that is common to vCard (== GnomeCard) and LDIF address
 * books ...
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "address-book-text.h"

#include <sys/stat.h>

#include "abook-completion.h"
#include "libbalsa-conf.h"
#include "misc.h"

/* FIXME: Perhaps the whole thing could be rewritten to use a g_scanner ?? */

/* FIXME: Arbitrary constant */
#define LINE_LEN 256


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "address-book"


static void
libbalsa_address_book_text_finalize(GObject * object);

static LibBalsaABErr
libbalsa_address_book_text_load(LibBalsaAddressBook * ab,
                                const gchar * filter,
                                LibBalsaAddressBookLoadFunc callback,
                                gpointer data);
static LibBalsaABErr
libbalsa_address_book_text_add_address(LibBalsaAddressBook * ab,
                                       LibBalsaAddress * address);
static LibBalsaABErr
libbalsa_address_book_text_remove_address(LibBalsaAddressBook * ab,
                                          LibBalsaAddress * address);
static LibBalsaABErr
libbalsa_address_book_text_modify_address(LibBalsaAddressBook * ab,
                                          LibBalsaAddress * address,
                                          LibBalsaAddress * newval);

static void
libbalsa_address_book_text_save_config(LibBalsaAddressBook * ab,
                                        const gchar * prefix);
static void
libbalsa_address_book_text_load_config(LibBalsaAddressBook * ab,
                                       const gchar * prefix);
static GList *
libbalsa_address_book_text_alias_complete(LibBalsaAddressBook * ab,
                                          const gchar * prefix);

/* GObject class stuff */

typedef struct {
    gchar *path;

    GSList *item_list;

    time_t mtime;

    LibBalsaCompletion *name_complete;
} LibBalsaAddressBookTextPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(LibBalsaAddressBookText, libbalsa_address_book_text,
        LIBBALSA_TYPE_ADDRESS_BOOK)

typedef struct {
    long begin;
    long end;
    LibBalsaAddress *address;
} LibBalsaAddressBookTextItem;

static void
lbab_text_item_free(LibBalsaAddressBookTextItem * item)
{
    if (item->address)
        g_object_unref(item->address);
    g_free(item);
}

static void
libbalsa_address_book_text_class_init(LibBalsaAddressBookTextClass * klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    klass->text_item_free_func = (GDestroyNotify) lbab_text_item_free;

    object_class->finalize = libbalsa_address_book_text_finalize;

    address_book_class->load = libbalsa_address_book_text_load;
    address_book_class->add_address =
        libbalsa_address_book_text_add_address;
    address_book_class->remove_address =
        libbalsa_address_book_text_remove_address;
    address_book_class->modify_address =
        libbalsa_address_book_text_modify_address;

    address_book_class->save_config =
        libbalsa_address_book_text_save_config;
    address_book_class->load_config =
        libbalsa_address_book_text_load_config;

    address_book_class->alias_complete =
        libbalsa_address_book_text_alias_complete;
}

static void
libbalsa_address_book_text_init(LibBalsaAddressBookText * ab_text)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    priv->path = NULL;
    priv->item_list = NULL;
    priv->mtime = 0;

    priv->name_complete =
        libbalsa_completion_new((LibBalsaCompletionFunc)
                                completion_data_extract);
    libbalsa_completion_set_compare(priv->name_complete, strncmp_word);
}

static LibBalsaAddressBookTextItem *
lbab_text_item_new(void)
{
    LibBalsaAddressBookTextItem *item =
        g_new(LibBalsaAddressBookTextItem, 1);
    item->address = libbalsa_address_new();
    return item;
}

static void
libbalsa_address_book_text_finalize(GObject * object)
{
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(object);
    LibBalsaAddressBookTextClass *ab_text_class = LIBBALSA_ADDRESS_BOOK_TEXT_GET_CLASS(object);
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    g_free(priv->path);

    g_slist_free_full(priv->item_list, ab_text_class->text_item_free_func);
    priv->item_list = NULL;

    g_list_foreach(priv->name_complete->items,
                   (GFunc) completion_data_free, NULL);
    libbalsa_completion_free(priv->name_complete);

    G_OBJECT_CLASS(libbalsa_address_book_text_parent_class)->finalize(object);
}

/* Load helpers */

/* returns true if the book has changed or there is an error */
static gboolean
lbab_text_address_book_need_reload(LibBalsaAddressBookText * ab_text)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);
    struct stat stat_buf;

    if (stat(priv->path, &stat_buf) == -1)
        return TRUE;

    if (stat_buf.st_mtime > priv->mtime) {
        priv->mtime = stat_buf.st_mtime;
        return TRUE;
    }

    return FALSE;
}

/* Case-insensitive utf-8 string-has-prefix */
static gboolean
lbab_text_starts_from(const gchar * str, const gchar * filter_hi)
{
    if (!str)
        return FALSE;

    while (*str && *filter_hi &&
           g_unichar_toupper(g_utf8_get_char(str)) ==
           g_utf8_get_char(filter_hi)) {
        str = g_utf8_next_char(str);
        filter_hi = g_utf8_next_char(filter_hi);
    }

    return *filter_hi == '\0';
}

/* To create distribution lists based on the ORG field:
 * #define MAKE_GROUP_BY_ORGANIZATION TRUE
 */
#if MAKE_GROUP_BY_ORGANIZATION
static void
lbab_text_group_address(const gchar * group_name,
                        GSList * lbab_text_group_addresses,
                        GList ** completion_list)
{
    GSList *l;
    CompletionData *cmp_data;
    InternetAddress *ia;

    if (!lbab_text_group_addresses || !lbab_text_group_addresses->next)
        return;

    ia = internet_address_group_new(group_name);
    for (l = lbab_text_group_addresses; l; l = l->next) {
        GList *mailbox;
        LibBalsaAddress *address = LIBBALSA_ADDRESS(l->data);

        for (mailbox = address->address_list; mailbox;
             mailbox = mailbox->next) {
            InternetAddress *member =
                internet_address_mailbox_new(address->full_name,
					     mailbox->data);
            internet_address_group_add_member((InternetAddressGroup *)ia, member);
            g_object_unref(member);
        }
    }
    g_slist_free(lbab_text_group_addresses);

    cmp_data = completion_data_new(ia, NULL);
    g_object_unref(ia);
    *completion_list = g_list_prepend(*completion_list, cmp_data);
}
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */

/* GCompareFunc for LibBalsaAddressBookTextItem */
static gint
lbab_text_item_compare(LibBalsaAddressBookTextItem * a,
                       LibBalsaAddressBookTextItem * b)
{
    g_return_val_if_fail(a != NULL, -1);
    g_return_val_if_fail(b != NULL, 1);

    if (a->address == NULL)
        return -1;
    if (b->address == NULL)
        return 1;

    return g_ascii_strcasecmp(libbalsa_address_get_full_name(a->address),
                              libbalsa_address_get_full_name(b->address));
}

/* Load the book from the stream */
static gboolean
lbab_text_load_file(LibBalsaAddressBookText * ab_text, FILE * stream)
{
    LibBalsaAddressBook *ab = ( LibBalsaAddressBook *) ab_text;
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);
    LibBalsaAddressBookTextClass *ab_text_class =
        LIBBALSA_ADDRESS_BOOK_TEXT_GET_CLASS(ab_text);
    LibBalsaABErr(*parse_address) (FILE * stream_in,
                                   LibBalsaAddress * address,
                                   FILE * stream_out,
                                   LibBalsaAddress * newval);
    GSList *list = NULL;
    GList *completion_list = NULL;
    CompletionData *cmp_data;
#if MAKE_GROUP_BY_ORGANIZATION
    GHashTable *group_table;
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */

    if (!lbab_text_address_book_need_reload(ab_text))
        return TRUE;

    g_slist_free_full(priv->item_list, ab_text_class->text_item_free_func);
    priv->item_list = NULL;

    g_list_foreach(priv->name_complete->items,
                   (GFunc) completion_data_free, NULL);
    libbalsa_completion_clear_items(priv->name_complete);

    parse_address =
        LIBBALSA_ADDRESS_BOOK_TEXT_GET_CLASS(ab_text)->parse_address;
    while (!feof(stream)) {
        LibBalsaAddressBookTextItem *item;

        item = lbab_text_item_new();
        item->begin = ftell(stream);
        if (parse_address(stream, item->address, NULL, NULL) != LBABERR_OK) {
            g_object_unref(item->address);
            item->address = NULL;
        }
        item->end = ftell(stream);
        list = g_slist_prepend(list, item);
    }

    if (!list)
        return TRUE;

    priv->item_list =
        g_slist_sort(list, (GCompareFunc) lbab_text_item_compare);

    completion_list = NULL;
#if MAKE_GROUP_BY_ORGANIZATION
    group_table =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */
    for (list = priv->item_list; list; list = list->next) {
        LibBalsaAddressBookTextItem *item = list->data;
        LibBalsaAddress *address = item->address;
#if MAKE_GROUP_BY_ORGANIZATION
        gchar **groups, **group;
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */
        guint n_addrs;

        if (address == NULL)
            continue;

        n_addrs = libbalsa_address_get_n_addrs(address);
	if (libbalsa_address_book_get_dist_list_mode(ab)
            && n_addrs > 1) {
            /* Create a group address. */
            InternetAddress *ia =
                internet_address_group_new(libbalsa_address_get_full_name(address));
            guint n;

            for (n = 0; n < n_addrs; ++n) {
                const gchar *addr = libbalsa_address_get_nth_addr(address, n);
                InternetAddress *member =
                    internet_address_mailbox_new(NULL, addr);
                internet_address_group_add_member((InternetAddressGroup *)ia, member);
                g_object_unref(member);
            }
            cmp_data = completion_data_new(ia, libbalsa_address_get_nick_name(address));
            completion_list = g_list_prepend(completion_list, cmp_data);
            g_object_unref(ia);
        } else {
            /* Create name addresses. */
            guint n;

            for (n = 0; n < n_addrs; ++n) {
                const gchar *addr = libbalsa_address_get_nth_addr(address, n);
                InternetAddress *ia =
                    internet_address_mailbox_new(libbalsa_address_get_full_name(address),
                                                 addr);
                cmp_data =
                    completion_data_new(ia, libbalsa_address_get_nick_name(address));
                completion_list =
                    g_list_prepend(completion_list, cmp_data);
                g_object_unref(ia);
            }
        }

#if MAKE_GROUP_BY_ORGANIZATION
        if (!address->organization || !*address->organization)
            continue;
        groups = g_strsplit(address->organization, ";", 0);
        for (group = groups; *group; group++) {
            gchar *group_name;
            GSList *lbab_text_group_addresses;

            g_strstrip(*group);
            group_name = group == groups ? g_strdup(*group) :
                g_strconcat(*groups, " ", *group, NULL);
            lbab_text_group_addresses =
                g_hash_table_lookup(group_table, group_name);
            lbab_text_group_addresses =
                g_slist_prepend(lbab_text_group_addresses, address);
            g_hash_table_replace(group_table, group_name,
                                 lbab_text_group_addresses);
        }
        g_strfreev(groups);
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */
    }

#if MAKE_GROUP_BY_ORGANIZATION
    g_hash_table_foreach(group_table, (GHFunc) lbab_text_group_address,
                         &completion_list);
    g_hash_table_destroy(group_table);
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */

    completion_list = g_list_reverse(completion_list);
    libbalsa_completion_add_items(priv->name_complete, completion_list);
    g_list_free(completion_list);

    return TRUE;
}

/* Lock and unlock an address book file */
static gboolean
lbab_text_lock_book(LibBalsaAddressBookText * ab_text, FILE * stream,
                    gboolean exclusive)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    return (libbalsa_lock_file
            (priv->path, fileno(stream), exclusive, TRUE, TRUE) >= 0);
}
static void
lbab_text_unlock_book(LibBalsaAddressBookText * ab_text, FILE * stream)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    libbalsa_unlock_file(priv->path, fileno(stream), TRUE);
}

/* Modify helpers */

/* Create a temporary file for the modified book */
static LibBalsaABErr
lbab_text_open_temp(LibBalsaAddressBookText * ab_text, gchar ** path,
                    FILE ** stream)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    *path = g_strconcat(priv->path, ".tmp", NULL);
    *stream = fopen(*path, "w");
    if (*stream == NULL) {
    	libbalsa_address_book_set_status(LIBBALSA_ADDRESS_BOOK(ab_text), g_strerror(errno));
        g_debug("Failed to open temporary address book file “%s”, changes not saved", *path);
        g_free(*path);
        *path = NULL;
        return LBABERR_CANNOT_WRITE;
    }
    return LBABERR_OK;
}

/* Rename the temporary file as the real book */
static LibBalsaABErr
lbab_text_close_temp(LibBalsaAddressBookText * ab_text, const gchar * path)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    if (unlink(priv->path) < 0
        && g_file_error_from_errno(errno) != G_FILE_ERROR_NOENT) {
    	libbalsa_address_book_set_status(LIBBALSA_ADDRESS_BOOK(ab_text), g_strerror(errno));
    	g_debug("Failed to unlink address book file “%s” new address book file saved as “%s”. Error: %s",
        	priv->path, path, g_strerror(errno));
        return LBABERR_CANNOT_WRITE;
    }

    if (rename(path, priv->path) < 0) {
    	libbalsa_address_book_set_status(LIBBALSA_ADDRESS_BOOK(ab_text), g_strerror(errno));
    	g_debug("Failed to rename temporary address book file “%s”", path);
        return LBABERR_CANNOT_WRITE;
    }

    return LBABERR_OK;
}

/* Copy part of one stream to another */
static LibBalsaABErr
lbab_text_copy_stream(FILE * stream_in, long begin, long end,
                      FILE * stream_out)
{
    gint count;
    size_t len;

    if (end < 0) {
        (void) fseek(stream_in, 0, SEEK_END);
        end = ftell(stream_in);
    }

    (void) fseek(stream_in, begin, SEEK_SET);
    for (count = end - begin; count > 0; count -= len) {
        gchar buf[4096];

        len = MIN(sizeof buf, (size_t) count);
        if (fread(buf, 1, len, stream_in) < len)
            return LBABERR_CANNOT_READ;
        if (fwrite(buf, 1, len, stream_out) < len)
            return LBABERR_CANNOT_WRITE;
    }

    return LBABERR_OK;
}

/* Class methods */

/* Load method */
static LibBalsaABErr
libbalsa_address_book_text_load(LibBalsaAddressBook * ab,
                                const gchar * filter,
                                LibBalsaAddressBookLoadFunc callback,
                                gpointer data)
{
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab);
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);
    FILE *stream;
    gboolean ok;
    GSList *list;
    gchar *filter_hi = NULL;

    stream = fopen(priv->path, "r");
    if (!stream)
        return LBABERR_CANNOT_READ;

    ok = lbab_text_lock_book(ab_text, stream, FALSE);
    if (ok) {
        ok = lbab_text_load_file(ab_text, stream);
        lbab_text_unlock_book(ab_text, stream);
    }
    fclose(stream);
    if (!ok)
        return LBABERR_CANNOT_READ;

    if (filter)
        filter_hi = g_utf8_strup(filter, -1);

    if (callback != NULL) {
        for (list = priv->item_list; list != NULL; list = list->next) {
            LibBalsaAddressBookTextItem *item = list->data;
            LibBalsaAddress *address = item->address;

            if (address == NULL)
                continue;

            if ((!filter_hi ||
                 lbab_text_starts_from(libbalsa_address_get_last_name(address), filter_hi) ||
                 lbab_text_starts_from(libbalsa_address_get_full_name(address), filter_hi)))
                callback(ab, address, data);
        }

        callback(ab, NULL, data);
    }

    g_free(filter_hi);

    return LBABERR_OK;
}

/* Add address method */
static LibBalsaABErr
libbalsa_address_book_text_add_address(LibBalsaAddressBook * ab,
                                       LibBalsaAddress * new_address)
{
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab);
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);
    LibBalsaAddressBookTextItem new_item;
    FILE *stream;
    LibBalsaABErr res = LBABERR_OK;

    stream = fopen(priv->path, "a+");
    if (stream == NULL)
        return LBABERR_CANNOT_WRITE;

    if (!lbab_text_lock_book(ab_text, stream, TRUE)) {
        fclose(stream);
        return LBABERR_CANNOT_WRITE;
    }

    lbab_text_load_file(ab_text, stream);       /* Ignore reading error,
                                                 * we may be adding
                                                 * the first address. */

    new_item.address = new_address;
    if (g_slist_find_custom(priv->item_list, &new_item,
                            (GCompareFunc) lbab_text_item_compare))
        return LBABERR_DUPLICATE;

    res = LIBBALSA_ADDRESS_BOOK_TEXT_GET_CLASS(ab_text)->save_address
        (stream, new_address);

    lbab_text_unlock_book(ab_text, stream);
    fclose(stream);

    /* Invalidate the time stamp, so the book will be reloaded. */
    priv->mtime = 0;

    return res;
}

/* Remove address method */
static LibBalsaABErr
libbalsa_address_book_text_remove_address(LibBalsaAddressBook * ab,
                                          LibBalsaAddress * address)
{
    return libbalsa_address_book_text_modify_address(ab, address, NULL);
}

/* Modify address method */
static LibBalsaABErr
libbalsa_address_book_text_modify_address(LibBalsaAddressBook * ab,
                                          LibBalsaAddress * address,
                                          LibBalsaAddress * newval)
{
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab);
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);
    LibBalsaAddressBookTextItem old_item;
    GSList *found;
    LibBalsaAddressBookTextItem *item;
    FILE *stream_in;
    LibBalsaABErr res;
    gchar *path = NULL;
    FILE *stream_out = NULL;

    if ((stream_in = fopen(priv->path, "r")) == NULL)
        return LBABERR_CANNOT_READ;

    if (!lbab_text_lock_book(ab_text, stream_in, FALSE)) {
        fclose(stream_in);
        return LBABERR_CANNOT_READ;
    }

    lbab_text_load_file(ab_text, stream_in);

    old_item.address = address;
    found = g_slist_find_custom(priv->item_list, &old_item,
                                (GCompareFunc) lbab_text_item_compare);
    if (!found) {
        lbab_text_unlock_book(ab_text, stream_in);
        fclose(stream_in);
        return LBABERR_ADDRESS_NOT_FOUND;
    }
    item = found->data;

    res = lbab_text_open_temp(ab_text, &path, &stream_out);

    if (res == LBABERR_OK)
        res = lbab_text_copy_stream(stream_in, 0, item->begin, stream_out);

    if (res == LBABERR_OK && newval)
        res = LIBBALSA_ADDRESS_BOOK_TEXT_GET_CLASS(ab)->parse_address
            (stream_in, NULL, stream_out, newval);

    if (res == LBABERR_OK)
        res = lbab_text_copy_stream(stream_in, item->end, -1, stream_out);

    lbab_text_unlock_book(ab_text, stream_in);
    fclose(stream_in);

    if (stream_out)
        fclose(stream_out);

    if (res == LBABERR_OK)
        res = lbab_text_close_temp(ab_text, path);
    else {
    	libbalsa_address_book_set_status(ab, g_strerror(errno));
        g_debug("Failed to write to temporary address book file “%s”, changes not saved", path);
    }
    g_free(path);

    /* Invalidate the time stamp, so the book will be reloaded. */
    priv->mtime = 0;

    return res;
}

/* Save config method */
static void
libbalsa_address_book_text_save_config(LibBalsaAddressBook * ab,
                                       const gchar * prefix)
{
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab);
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    libbalsa_conf_set_string("Path", priv->path);

    LIBBALSA_ADDRESS_BOOK_CLASS(libbalsa_address_book_text_parent_class)->save_config(ab, prefix);
}

/* Load config method */
static void
libbalsa_address_book_text_load_config(LibBalsaAddressBook * ab,
                                       const gchar * prefix)
{
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab);
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    g_free(priv->path);
    priv->path = libbalsa_conf_get_string("Path");

    LIBBALSA_ADDRESS_BOOK_CLASS(libbalsa_address_book_text_parent_class)->load_config(ab, prefix);
}

/* Alias complete method */
static GList *
libbalsa_address_book_text_alias_complete(LibBalsaAddressBook * ab,
                                          const gchar * prefix)
{
    LibBalsaAddressBookText *ab_text = LIBBALSA_ADDRESS_BOOK_TEXT(ab);
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);
    FILE *stream;
    GList *list;
    GList *res = NULL;

    if (!libbalsa_address_book_get_expand_aliases(ab))
        return NULL;

    stream = fopen(priv->path, "r");
    if (!stream)
        return NULL;

    if (!lbab_text_lock_book(ab_text, stream, FALSE)) {
        fclose(stream);
        return NULL;
    }

    lbab_text_load_file(ab_text, stream);

    lbab_text_unlock_book(ab_text, stream);
    fclose(stream);

    for (list =
         libbalsa_completion_complete(priv->name_complete,
                                      (gchar *) prefix);
         list; list = list->next) {
        InternetAddress *ia = ((CompletionData *) list->data)->ia;
        g_object_ref(ia);
        res = g_list_prepend(res, ia);
    }

    return g_list_reverse(res);
}

/*
 * Getters
 */

const gchar *
libbalsa_address_book_text_get_path(LibBalsaAddressBookText * ab_text)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_TEXT(ab_text), NULL);

    return priv->path;
}

GSList *
libbalsa_address_book_text_get_item_list(LibBalsaAddressBookText * ab_text)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_TEXT(ab_text), NULL);

    return priv->item_list;
}

time_t
libbalsa_address_book_text_get_mtime(LibBalsaAddressBookText * ab_text)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_TEXT(ab_text), 0);

    return priv->mtime;
}

LibBalsaCompletion *
libbalsa_address_book_text_get_name_complete(LibBalsaAddressBookText * ab_text)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_TEXT(ab_text), NULL);

    return priv->name_complete;
}

/*
 * Setters
 */

void
libbalsa_address_book_text_set_path(LibBalsaAddressBookText * ab_text,
                                    const gchar             * path)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_TEXT(ab_text));

    g_free(priv->path);
    priv->path = g_strdup(path);
}

void
libbalsa_address_book_text_set_item_list(LibBalsaAddressBookText * ab_text,
                                         GSList                  * item_list)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);
    LibBalsaAddressBookTextClass *ab_text_class;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_TEXT(ab_text));

    ab_text_class = LIBBALSA_ADDRESS_BOOK_TEXT_GET_CLASS(ab_text);
    g_slist_free_full(priv->item_list, ab_text_class->text_item_free_func);
    priv->item_list = item_list;
}

void
libbalsa_address_book_text_set_mtime(LibBalsaAddressBookText * ab_text,
                                     time_t                    mtime)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_TEXT(ab_text));

    priv->mtime = mtime;
}

void
libbalsa_address_book_text_set_name_complete(LibBalsaAddressBookText * ab_text,
                                             LibBalsaCompletion      * name_complete)
{
    LibBalsaAddressBookTextPrivate *priv =
        libbalsa_address_book_text_get_instance_private(ab_text);

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_TEXT(ab_text));

    priv->name_complete = name_complete;
}
