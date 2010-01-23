/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

/*
 * GPE address book
 * NOTES:
 See 
 http://cvs.handhelds.org/cgi-bin/viewcvs.cgi/gpe/base/gpe-contacts
 Tags must be UPPERCASE.
 */
#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#if defined(HAVE_SQLITE)

#include "address-book-gpe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "address-book.h"
#include "information.h"

#define ASSURE_GPE_DIR\
    do {gchar* dir = g_strconcat(g_get_home_dir(), "/.gpe", NULL);\
 mkdir(dir, S_IRUSR|S_IWUSR|S_IXUSR); g_free(dir);}while(0)

static LibBalsaAddressBookClass *parent_class = NULL;

static void
libbalsa_address_book_gpe_class_init(LibBalsaAddressBookGpeClass *
				      klass);
static void libbalsa_address_book_gpe_init(LibBalsaAddressBookGpe * ab);
static void libbalsa_address_book_gpe_finalize(GObject * object);

static LibBalsaABErr libbalsa_address_book_gpe_load(LibBalsaAddressBook * ab, 
                                                    const gchar *filter,
                                                    LibBalsaAddressBookLoadFunc callback, 
                                                    gpointer closure);

static void
libbalsa_address_book_gpe_close_db(LibBalsaAddressBookGpe *ab);
static LibBalsaABErr
libbalsa_address_book_gpe_add_address(LibBalsaAddressBook *ab,
                                      LibBalsaAddress *address);
static LibBalsaABErr
libbalsa_address_book_gpe_remove_address(LibBalsaAddressBook *ab,
                                         LibBalsaAddress *address);
static LibBalsaABErr
libbalsa_address_book_gpe_modify_address(LibBalsaAddressBook *ab,
                                         LibBalsaAddress *address,
                                         LibBalsaAddress *newval);


static GList *libbalsa_address_book_gpe_alias_complete(LibBalsaAddressBook *ab,
                                                       const gchar *prefix, 
                                                       gchar **new_prefix);

GType libbalsa_address_book_gpe_get_type(void)
{
    static GType address_book_gpe_type = 0;

    if (!address_book_gpe_type) {
	static const GTypeInfo address_book_gpe_info = {
	    sizeof(LibBalsaAddressBookGpeClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_address_book_gpe_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaAddressBookGpe),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_address_book_gpe_init
	};

	address_book_gpe_type =
            g_type_register_static(LIBBALSA_TYPE_ADDRESS_BOOK,
	                           "LibBalsaAddressBookGpe",
			           &address_book_gpe_info, 0);
    }

    return address_book_gpe_type;
}

static void
libbalsa_address_book_gpe_class_init(LibBalsaAddressBookGpeClass * klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GObjectClass *object_class;

    parent_class = g_type_class_peek_parent(klass);

    object_class = G_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->finalize = libbalsa_address_book_gpe_finalize;

    address_book_class->load = libbalsa_address_book_gpe_load;
    address_book_class->add_address = libbalsa_address_book_gpe_add_address;
    address_book_class->remove_address = 
        libbalsa_address_book_gpe_remove_address;
    address_book_class->modify_address =
        libbalsa_address_book_gpe_modify_address;

    address_book_class->alias_complete = 
	libbalsa_address_book_gpe_alias_complete;
}

static void
libbalsa_address_book_gpe_init(LibBalsaAddressBookGpe * ab)
{
    ab->db = NULL;
    LIBBALSA_ADDRESS_BOOK(ab)->is_expensive = FALSE;
}

static void
libbalsa_address_book_gpe_finalize(GObject * object)
{
    libbalsa_address_book_gpe_close_db(LIBBALSA_ADDRESS_BOOK_GPE(object));

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaAddressBook *
libbalsa_address_book_gpe_new(const gchar *name)
{
    LibBalsaAddressBookGpe *gpe;
    LibBalsaAddressBook *ab;

    gpe = LIBBALSA_ADDRESS_BOOK_GPE(g_object_new
                                    (LIBBALSA_TYPE_ADDRESS_BOOK_GPE,
                                     NULL));
    ab = LIBBALSA_ADDRESS_BOOK(gpe);

    ab->name = g_strdup(name);
    /* We open on demand... */
    gpe->db = NULL;
    return ab;
}

/*
 * Close the SQLite db....
 */
static void
libbalsa_address_book_gpe_close_db(LibBalsaAddressBookGpe * ab)
{
    if (ab->db) {
#ifdef HAVE_SQLITE3
	sqlite3_close(ab->db);
#else                           /* HAVE_SQLITE3 */
	sqlite_close(ab->db);
#endif                          /* HAVE_SQLITE3 */
	ab->db = NULL;
    }
}

/*
 * Opens the SQLite db
 */
#ifndef HAVE_SQLITE3
#define DB_NAME "/.gpe/contacts"
#endif                          /* HAVE_SQLITE3 */
static const char *schema_str =
"create table contacts (urn INTEGER NOT NULL, tag TEXT NOT NULL, "
"value TEXT NOT NULL)";

static const char *schema2_str =
"create table contacts_urn (urn INTEGER PRIMARY KEY)";

static int
libbalsa_address_book_gpe_open_db(LibBalsaAddressBookGpe * ab)
{
#ifdef HAVE_SQLITE3
    gchar *dir, *name;

    dir = g_build_filename(g_get_home_dir(), ".gpe", NULL);
    mkdir(dir, S_IRUSR|S_IWUSR|S_IXUSR);
    name = g_build_filename(dir, "contacts", NULL);
    g_free(dir);

    if (sqlite3_open(name, &ab->db) != SQLITE_OK) {
        printf("Cannot open \"%s\": %s\n", name, sqlite3_errmsg(ab->db));
        g_free(name);
        sqlite3_close(ab->db);
        ab->db = NULL;
        return 0;
    }
    g_free(name);

    sqlite3_exec(ab->db, schema_str,  NULL, NULL, NULL);
    sqlite3_exec(ab->db, schema2_str, NULL, NULL, NULL);
#else                           /* HAVE_SQLITE3 */
    gchar *name, *errmsg = NULL;

    ASSURE_GPE_DIR;
    name = g_strconcat(g_get_home_dir(), DB_NAME, NULL);
    ab->db = sqlite_open(name, 0, &errmsg);
    g_free(name);
    if(ab->db == NULL) {
        printf("Cannot open: %s\n", errmsg);
        free(errmsg);
        return 0;
    }
    sqlite_exec (ab->db, schema_str,  NULL, NULL, NULL);
    sqlite_exec (ab->db, schema2_str, NULL, NULL, NULL);
#endif                          /* HAVE_SQLITE3 */

    return 1;
}


/* libbalsa_address_book_gpe_get_address:
 * a callback to fill in the LibBalsaAddress object.
 * see http://www.handhelds.org:8080/wiki/WellKnownDBTags for tag list
 * We only use limited subset.
 * 
 given_name         # how to handle "J. Edgar Hoover"?
 middle_initial
 family_name        # nb family name is written first in many cultures
 nickname
 home_email         # multiple addresses, separated by commas
 work_organization
 work_email         # multiple addresses, separated by commas
*/


static int
gpe_read_attr(void *arg, int argc, char **argv, char **names)
{
    LibBalsaAddress * a= arg;

    /* follow read_entry_data/db_set_multi_data */
    if(g_ascii_strcasecmp(argv[0], "NAME") == 0 &&
       !a->full_name) a->full_name = g_strdup(argv[1]);
    else if(g_ascii_strcasecmp(argv[0], "GIVEN_NAME") == 0 &&
            !a->first_name) a->first_name = g_strdup(argv[1]);
    else if(g_ascii_strcasecmp(argv[0], "FAMILY_NAME") == 0 &&
            !a->last_name) a->last_name = g_strdup(argv[1]);
    else if(g_ascii_strcasecmp(argv[0], "NICKNAME") == 0 &&
            !a->nick_name) a->nick_name = g_strdup(argv[1]);
    else if(g_ascii_strcasecmp(argv[0], "WORK.ORGANIZATION") == 0 &&
            !a->organization) a->organization = g_strdup(argv[1]);
    else if(g_ascii_strcasecmp(argv[0], "HOME.EMAIL") == 0)
        a->address_list = g_list_prepend(a->address_list, g_strdup(argv[1]));
    else if(g_ascii_strcasecmp(argv[0], "WORK.EMAIL") == 0)
        a->address_list = g_list_prepend(a->address_list, g_strdup(argv[1]));
    return 0;
}

/*
 * create_name()
 *
 * Creates a full name from a given first name and surname.
 * 
 * Returns:
 *   gchar * a full name
 *   NULL on failure (both first and last names invalid.
 */
static gchar *
create_name(gchar * first, gchar * last)
{
    if ((first == NULL) && (last == NULL))
	return NULL;
    else if (first == NULL)
	return g_strdup(last);
    else if (last == NULL)
	return g_strdup(first);
    else
	return g_strdup_printf("%s %s", first, last);
}

struct gpe_closure {
    LibBalsaAddressBookLoadFunc callback;
    gpointer closure;
    LibBalsaAddressBookGpe *gpe;
};
                                             
static int
gpe_read_address(void *arg, int argc, char **argv, char **names)
{
    struct gpe_closure *gc = arg;
    LibBalsaAddress * a= libbalsa_address_new();
    guint uid = atoi(argv[0]);

    /* follow read_entry_data. FIXME: error reporting */
#ifdef HAVE_SQLITE3
    gchar *sql =
        sqlite3_mprintf("select tag,value from contacts where urn=%d",
                        uid);
    sqlite3_exec(gc->gpe->db, sql, gpe_read_attr, a, NULL);
    sqlite3_free(sql);
#else                           /* HAVE_SQLITE3 */
    sqlite_exec_printf (gc->gpe->db,
                        "select tag,value from contacts where urn=%d",
                        gpe_read_attr, a, NULL, uid);
#endif                          /* HAVE_SQLITE3 */
    if(!a->address_list) { /* entry without address: ignore! */
        g_object_unref(a);
        return 0;
    }
    if(!a->full_name)
        a->full_name = create_name(a->first_name, a->last_name);
    g_object_set_data(G_OBJECT(a), "urn", GUINT_TO_POINTER(uid));
    gc->callback(LIBBALSA_ADDRESS_BOOK(gc->gpe), a, gc->closure);
    return 0;
}


/*
 * gpe_load:
 * opens the db only if needed.
 */
static LibBalsaABErr
libbalsa_address_book_gpe_load(LibBalsaAddressBook * ab,
                               const gchar *filter,
                               LibBalsaAddressBookLoadFunc callback,
                               gpointer closure)
{
    LibBalsaAddressBookGpe *gpe_ab;
    gchar *err = NULL;
    struct gpe_closure gc;
    int r;
    LibBalsaABErr ret;

    g_return_val_if_fail ( LIBBALSA_IS_ADDRESS_BOOK_GPE(ab), LBABERR_OK);

    if (callback == NULL)
	return LBABERR_OK;

    gpe_ab = LIBBALSA_ADDRESS_BOOK_GPE(ab);
    
    if (gpe_ab->db == NULL)
        if (!libbalsa_address_book_gpe_open_db(gpe_ab))
            return LBABERR_CANNOT_CONNECT;

    gc.callback = callback;
    gc.closure  = closure;
    gc.gpe      = gpe_ab;
    /* FIXME: error reporting */
#ifdef HAVE_SQLITE3
{
    if (filter && *filter) {
        gchar *sql =
            sqlite3_mprintf("select distinct urn from contacts where "
                            "(upper(tag)='FAMILY_NAME' or"
                            " upper(tag)='GIVEN_NAME' or"
                            " upper(tag)='NAME' or"
                            " upper(tag)='WORK.EMAIL' or"
                            " upper(tag)='HOME.EMAIL') "
                            "and value LIKE '%q%%'",
                            filter);
        r = sqlite3_exec(gpe_ab->db, sql, gpe_read_address, &gc, &err);
        sqlite3_free(sql);
    } else {
        r = sqlite3_exec(gpe_ab->db,
                         "select distinct urn from contacts_urn",
                         gpe_read_address, &gc, &err);
    }
}
#else                           /* HAVE_SQLITE3 */
    if(filter && *filter) {
        r = sqlite_exec_printf
            (gpe_ab->db, 
             "select distinct urn from contacts where "
             "(upper(tag)='FAMILY_NAME' or upper(tag)='GIVEN_NAME' or "
             "upper(tag)='NAME' or "
             "upper(tag)='WORK.EMAIL' or upper(tag)='HOME.EMAIL') "
             "and value LIKE '%q%%'",
             gpe_read_address, &gc, &err, filter);
    } else {
        r = sqlite_exec(gpe_ab->db, "select distinct urn from contacts_urn",
                        gpe_read_address, &gc, &err);
    }
#endif                          /* HAVE_SQLITE3 */

    if(r != SQLITE_OK) {
        printf("r=%d err=%s\n", r, err);
        libbalsa_address_book_set_status(ab, g_strdup(err));
#ifdef HAVE_SQLITE3
        sqlite3_free(err);
#else                           /* HAVE_SQLITE3 */
        free(err);
#endif                          /* HAVE_SQLITE3 */
        ret = LBABERR_CANNOT_READ;
    } else {
        libbalsa_address_book_set_status(ab, NULL);
        ret = LBABERR_OK;
    }

    callback(ab, NULL, closure);
    return ret;
}

#ifdef HAVE_SQLITE3
#define INSERT_ATTR(db,id,attr,val) \
 do { if( (val) && *(val)) {\
 sql = sqlite3_mprintf("insert into contacts values ('%d', '%q', '%q')",\
                       (id), (attr), (val));\
 sqlite3_exec((db), sql, NULL, NULL, NULL);\
 sqlite3_free(sql);}} while(0)
#else                           /* HAVE_SQLITE3 */
#define INSERT_ATTR(db,id,attr,val) \
 do { if( (val) && *(val)) {\
 sqlite_exec_printf((db), "insert into contacts values ('%d', '%q', '%q')",\
                    NULL, NULL, NULL, (id), (attr), (val));}} while(0);
#endif                          /* HAVE_SQLITE3 */

static LibBalsaABErr
libbalsa_address_book_gpe_add_address(LibBalsaAddressBook *ab,
                                       LibBalsaAddress *address)
{
    LibBalsaAddressBookGpe *gpe_ab = LIBBALSA_ADDRESS_BOOK_GPE(ab);
    int r;
    guint id;
    char *err = NULL;
#ifdef HAVE_SQLITE3
    gchar *sql;
#endif                          /* HAVE_SQLITE3 */

    g_return_val_if_fail(address, LBABERR_CANNOT_WRITE);
    g_return_val_if_fail(address->address_list, LBABERR_CANNOT_WRITE);

    if (gpe_ab->db == NULL) {
        if(!libbalsa_address_book_gpe_open_db(gpe_ab))
	    return LBABERR_CANNOT_CONNECT;
    }
#ifdef HAVE_SQLITE3
    r = sqlite3_exec(gpe_ab->db, "insert into contacts_urn values (NULL)",
                     NULL, NULL, &err);
    if (r != SQLITE_OK) {
        libbalsa_address_book_set_status(ab, g_strdup(err));
        sqlite3_free(err);
#else                           /* HAVE_SQLITE3 */
    r = sqlite_exec(gpe_ab->db, "insert into contacts_urn values (NULL)",
                    NULL, NULL, &err);
    if (r != SQLITE_OK) {
        libbalsa_address_book_set_status(ab, g_strdup(err));
        free(err);
#endif                          /* HAVE_SQLITE3 */
        return LBABERR_CANNOT_WRITE;
    }
    /* FIXME: duplicate detection! */

#ifdef HAVE_SQLITE3
    id = sqlite3_last_insert_rowid(gpe_ab->db);
#else                           /* HAVE_SQLITE3 */
    id = sqlite_last_insert_rowid(gpe_ab->db);
#endif                          /* HAVE_SQLITE3 */
    INSERT_ATTR(gpe_ab->db,id, "NAME",        address->full_name);
    INSERT_ATTR(gpe_ab->db,id, "GIVEN_NAME",  address->first_name);
    INSERT_ATTR(gpe_ab->db,id, "FAMILY_NAME", address->last_name);
    INSERT_ATTR(gpe_ab->db,id, "NICKNAME",    address->nick_name);
    INSERT_ATTR(gpe_ab->db,id, "WORK.ORGANIZATION", address->organization);
    INSERT_ATTR(gpe_ab->db,id, "WORK.EMAIL",
                (char*)address->address_list->data);
#ifdef HAVE_SQLITE3
    sql = sqlite3_mprintf("insert into contacts values "
                          "('%d', 'MODIFIED', %d)",
                          id, time(NULL));
    sqlite3_exec(gpe_ab->db, sql, NULL, NULL, NULL);
    sqlite3_free(sql);
#else                           /* HAVE_SQLITE3 */
    sqlite_exec_printf(gpe_ab->db, "insert into contacts values "
                       "('%d', 'MODIFIED', %d)", NULL, NULL, NULL,
                       id, time(NULL));
#endif                          /* HAVE_SQLITE3 */
    return LBABERR_OK;
}
#ifdef HAVE_SQLITE3
static gchar *
db_delete_by_uid(sqlite3 * db, guint uid)
{
    int r;
    gchar *err;
    gchar *sql;

    if (sqlite3_exec(db, "begin transaction", NULL, NULL, &err) !=
        SQLITE_OK)
        return err;

    sql = sqlite3_mprintf("delete from contacts where urn='%d'", uid);
    r = sqlite3_exec(db, sql, NULL, NULL, &err);
    sqlite3_free(sql);
    if (r != SQLITE_OK) {
        sqlite3_exec(db, "rollback transaction", NULL, NULL, NULL);
        return err;
    }

    sql = sqlite3_mprintf("delete from contacts_urn where urn='%d'", uid);
    r = sqlite3_exec(db, sql, NULL, NULL, &err);
    sqlite3_free(sql);
    if (r != SQLITE_OK) {
        sqlite3_exec(db, "rollback transaction", NULL, NULL, NULL);
        return err;
    }

    if (sqlite3_exec(db, "commit transaction", NULL, NULL, &err) !=
        SQLITE_OK) {
        sqlite3_exec(db, "rollback transaction", NULL, NULL, NULL);
        return err;
    }

    return NULL;
}
#else                           /* HAVE_SQLITE3 */
static gchar*
db_delete_by_uid(sqlite *db, guint uid)
{
    int r;
    gchar *err;
    
    if( (r=sqlite_exec(db, "begin transaction",
                       NULL, NULL, &err)) == SQLITE_OK)
        return err;
    
    if( (r=sqlite_exec_printf(db, "delete from contacts where urn='%d'",
                              NULL, NULL, &err, uid)) == SQLITE_OK &&
        (r=sqlite_exec_printf(db, "delete from contacts_urn where urn='%d'",
                              NULL, NULL, &err, uid)) == SQLITE_OK &&
        (r=sqlite_exec(db, "commit transaction", NULL,
                       NULL, &err)) == SQLITE_OK) return NULL;
    
    sqlite_exec(db, "rollback transaction", NULL, NULL, NULL);
    return err;
}
#endif                          /* HAVE_SQLITE3 */

static LibBalsaABErr
libbalsa_address_book_gpe_remove_address(LibBalsaAddressBook *ab,
                                          LibBalsaAddress *address)
{
    LibBalsaAddressBookGpe *gpe_ab = LIBBALSA_ADDRESS_BOOK_GPE(ab);
    guint uid;
    char *err;
    g_return_val_if_fail(address, LBABERR_CANNOT_WRITE);
    g_return_val_if_fail(address->address_list, LBABERR_CANNOT_WRITE);

    if (gpe_ab->db == NULL) {
        if( !libbalsa_address_book_gpe_open_db(gpe_ab))
	    return LBABERR_CANNOT_CONNECT;
    }
    uid = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(address), "urn"));
    if(!uid)/* safety check, perhaps unnecessary */
        return LBABERR_CANNOT_WRITE;
        
    err = db_delete_by_uid(gpe_ab->db, uid);
    if(err) {
        libbalsa_address_book_set_status(ab, g_strdup(err));
#ifdef HAVE_SQLITE3
        sqlite3_free(err);
#else                           /* HAVE_SQLITE3 */
        free(err);
#endif                          /* HAVE_SQLITE3 */
        return LBABERR_CANNOT_WRITE;
    } else return LBABERR_OK;
}


/** libbalsa_address_book_gpe_modify_address:
    modify given address.
*/
#ifdef HAVE_SQLITE3
#define INSERT_ATTR_R(db,id,attr,val) \
 if ((val) && *(val)) {\
 sql = sqlite3_mprintf("insert into contacts values ('%d', '%q', '%q')",\
                       (id), (attr), (val));\
 r = sqlite3_exec((db), sql, NULL, NULL, &err);\
 sqlite3_free(sql);\
 if (r != SQLITE_OK) goto rollback;}
#else                           /* HAVE_SQLITE3 */
#define INSERT_ATTR_R(db,id,attr,val) \
 if( (val) && *(val)) {\
 if( (r=sqlite_exec_printf \
         ((db), "insert into contacts values ('%d', '%q', '%q')",\
                    NULL, NULL, &err, (id), (attr), (val))) != SQLITE_OK)\
    goto rollback;}
#endif                          /* HAVE_SQLITE3 */

static LibBalsaABErr
libbalsa_address_book_gpe_modify_address(LibBalsaAddressBook *ab,
                                         LibBalsaAddress *address,
                                         LibBalsaAddress *newval)
{
    LibBalsaAddressBookGpe *gpe_ab = LIBBALSA_ADDRESS_BOOK_GPE(ab);
    guint uid;
    int r;
    char *err;
#ifdef HAVE_SQLITE3
    gchar *sql;
#endif                          /* HAVE_SQLITE3 */

    g_return_val_if_fail(address, LBABERR_CANNOT_WRITE);
    g_return_val_if_fail(address->address_list, LBABERR_CANNOT_WRITE);
    g_return_val_if_fail(newval->address_list, LBABERR_CANNOT_WRITE);

    if (gpe_ab->db == NULL) {
        if( !libbalsa_address_book_gpe_open_db(gpe_ab))
	    return LBABERR_CANNOT_CONNECT;
    }
    uid = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(address), "urn"));
    /* safety check, perhaps unnecessary */
    g_return_val_if_fail(uid, LBABERR_CANNOT_WRITE);

    /* do the real work here */
#ifdef HAVE_SQLITE3
    if ((r = sqlite3_exec(gpe_ab->db, "begin transaction",
                          NULL, NULL, &err)) != SQLITE_OK) {
        libbalsa_address_book_set_status(ab, g_strdup(err));
        sqlite3_free(err);              /* failed, so soon!? */
        return LBABERR_CANNOT_WRITE;
    }

    sql = sqlite3_mprintf("delete from contacts where urn='%d' and "
                          "(upper(tag)='NAME' or"
                          " upper(tag)='GIVEN_NAME' or"
                          " upper(tag)='NICKNAME' or"
                          " upper(tag)='WORK.ORGANIZATION' or"
                          " upper(tag)='WORK.EMAIL' or"
                          " upper(tag)='MODIFIED')", uid);
    r = sqlite3_exec(gpe_ab->db, sql, NULL, NULL, &err);
    sqlite3_free(sql);
    if (r != SQLITE_OK)
        goto rollback;
#else                           /* HAVE_SQLITE3 */
    if( (r=sqlite_exec(gpe_ab->db, "begin transaction",
                       NULL, NULL, &err)) != SQLITE_OK) {
        libbalsa_address_book_set_status(ab, g_strdup(err));
        free(err); /* failed, so soon!? */
        return LBABERR_CANNOT_WRITE;
    }

    if( (r=sqlite_exec_printf
         (gpe_ab->db,
          "delete from contacts where urn='%d' and "
          "(upper(tag)='NAME' or upper(tag)='GIVEN_NAME' or "
          "upper(tag)='NICKNAME' or upper(tag)='WORK.ORGANIZATION' or "
          "upper(tag)='WORK.EMAIL' or upper(tag)='MODIFIED')",
          NULL, NULL, &err, uid)) != SQLITE_OK)
        goto rollback;
#endif                          /* HAVE_SQLITE3 */
    INSERT_ATTR_R(gpe_ab->db,uid, "NAME",        newval->full_name);
    INSERT_ATTR_R(gpe_ab->db,uid, "GIVEN_NAME",  newval->first_name);
    INSERT_ATTR_R(gpe_ab->db,uid, "FAMILY_NAME", newval->last_name);
    INSERT_ATTR_R(gpe_ab->db,uid, "NICKNAME",    newval->nick_name);
    INSERT_ATTR_R(gpe_ab->db,uid, "WORK.ORGANIZATION", newval->organization);
    INSERT_ATTR_R(gpe_ab->db,uid, "WORK.EMAIL",
                (char*)newval->address_list->data);
#ifdef HAVE_SQLITE3
    sql = sqlite3_mprintf("insert into contacts values "
                          "('%d', 'MODIFIED', %d)", uid, time(NULL));
    r = sqlite3_exec(gpe_ab->db, sql, NULL, NULL, &err);
    sqlite3_free(sql);
    if (r != SQLITE_OK)
        goto rollback;

    if (sqlite3_exec(gpe_ab->db, "commit transaction", NULL, NULL, &err) ==
        SQLITE_OK)
        return LBABERR_OK;
#else                           /* HAVE_SQLITE3 */
    if( (r=sqlite_exec_printf(gpe_ab->db, "insert into contacts values "
                              "('%d', 'MODIFIED', %d)", NULL, NULL, &err,
                              uid, time(NULL))) != SQLITE_OK)
        goto rollback;
    
    if( (r=sqlite_exec(gpe_ab->db, "commit transaction", NULL,
                       NULL, &err)) == SQLITE_OK) return LBABERR_OK;
#endif                          /* HAVE_SQLITE3 */

 rollback:
    libbalsa_address_book_set_status(ab, g_strdup(err));
#ifdef HAVE_SQLITE3
    sqlite3_free(err);
    sqlite3_exec(gpe_ab->db, "rollback transaction", NULL, NULL, NULL);
#else                           /* HAVE_SQLITE3 */
    free(err);
    sqlite_exec(gpe_ab->db, "rollback transaction", NULL, NULL, NULL);
#endif                          /* HAVE_SQLITE3 */

    return LBABERR_CANNOT_WRITE;
}

struct gpe_completion_closure {
#ifdef HAVE_SQLITE3
    sqlite3 *db;
#else                           /* HAVE_SQLITE3 */
    sqlite *db;
#endif                          /* HAVE_SQLITE3 */
    const gchar *prefix;
    gchar **new_prefix;
    GList *res;
};

static int
gpe_read_completion(void *arg, int argc, char **argv, char **names)
{
    struct gpe_completion_closure *gc = arg;
    LibBalsaAddress * a= libbalsa_address_new();
    InternetAddress *ia;
    GList *l;
    guint uid = atoi(argv[0]);
#ifdef HAVE_SQLITE3
    gchar *sql;

    /* follow read_entry_data. FIXME: error reporting */
    sql = sqlite3_mprintf("select tag,value from contacts where urn=%d",
                          uid);
    sqlite3_exec(gc->db, sql, gpe_read_attr, a, NULL);
    sqlite3_free(sql);
#else                           /* HAVE_SQLITE3 */

    /* follow read_entry_data. FIXME: error reporting */
    sqlite_exec_printf(gc->db,
                       "select tag,value from contacts where urn=%d",
                       gpe_read_attr, a, NULL, uid);
#endif                          /* HAVE_SQLITE3 */
    if(!a->address_list) { /* entry without address: ignore! */
        g_object_unref(a);
        return 0;
    }
    if(!a->full_name)
        a->full_name = create_name(a->first_name, a->last_name);
    for(l=a->address_list; l; l = l->next) {
        ia = internet_address_mailbox_new(a->full_name, l->data);
        gc->res = g_list_prepend(gc->res, ia);
    }
    if(gc->new_prefix && !*gc->new_prefix)
        *gc->new_prefix = libbalsa_address_to_gchar(a, 0);
    g_object_unref(G_OBJECT(a));
    return 0;
}

static GList *
libbalsa_address_book_gpe_alias_complete(LibBalsaAddressBook * ab,
					  const gchar * prefix, 
					  gchar ** new_prefix)
{
    static const char *query = 
        "select distinct urn from contacts where "
        "(upper(tag)='FAMILY_NAME' or upper(tag)='GIVEN_NAME' or "
        "upper(tag)='NAME' or "
        "upper(tag)='WORK.EMAIL' or upper(tag)='HOME.EMAIL') "
        "and upper(value) LIKE '%q%%'";
    struct gpe_completion_closure gcc;
    LibBalsaAddressBookGpe *gpe_ab;
    char *err = NULL;
    int r;

    g_return_val_if_fail ( LIBBALSA_ADDRESS_BOOK_GPE(ab), NULL);

    gpe_ab = LIBBALSA_ADDRESS_BOOK_GPE(ab);

    if (!ab->expand_aliases) return NULL;
    if (gpe_ab->db == NULL) {
        if( !libbalsa_address_book_gpe_open_db(gpe_ab))
	    return NULL;
    }

    if(new_prefix) *new_prefix = NULL;
    gcc.db = gpe_ab->db;
    gcc.prefix = prefix;
    gcc.new_prefix = new_prefix;
    gcc.res = NULL;
#ifdef HAVE_SQLITE3
    if (prefix) {
        gchar *sql = sqlite3_mprintf(query, prefix);
        r = sqlite3_exec(gpe_ab->db, sql, gpe_read_completion, &gcc, &err);
        sqlite3_free(sql);
    } else
        r = sqlite3_exec(gpe_ab->db,
                         "select distinct urn from contacts_urn",
                         gpe_read_completion, &gcc, &err);
    if(err) {
        printf("r=%d err=%s\n", r, err);
        sqlite3_free(err);
    }
#else                           /* HAVE_SQLITE3 */
    if(prefix)
        r = sqlite_exec_printf(gpe_ab->db, query,
                               gpe_read_completion, &gcc, &err,
                               prefix);
    else
        r = sqlite_exec(gpe_ab->db, "select distinct urn from contacts_urn",
                        gpe_read_completion, &gcc, &err);
    if(err) {
        printf("r=%d err=%s\n", r, err);
        free(err);
    }
#endif                          /* HAVE_SQLITE3 */
    return gcc.res;
}
#endif				/* WITH_SQLITE */
