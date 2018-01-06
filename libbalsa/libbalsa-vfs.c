/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * libbalsa vfs glue layer library
 * Copyright (C) 2008 Albrecht Dreﬂ <albrecht.dress@arcor.de>
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
#include "libbalsa-vfs.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gmime/gmime.h>
#include "libbalsa.h"
#include "misc.h"

#include <gio/gio.h>


#define LIBBALSA_VFS_ERROR_QUARK (g_quark_from_static_string("libbalsa-vfs"))


#define LIBBALSA_VFS_MIME_ACTION "mime_action"

#define GIO_INFO_ATTS                           \
    G_FILE_ATTRIBUTE_STANDARD_TYPE ","          \
    G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","  \
    G_FILE_ATTRIBUTE_STANDARD_SIZE ","          \
    "access::*"


struct _LibbalsaVfsPriv {
    gchar * file_uri;
    gchar * file_utf8;
    gchar * folder_uri;
    gchar * mime_type;
    gchar * charset;
    LibBalsaTextAttribute text_attr;
    GFile * gio_gfile;
    GFileInfo * info;
};


static GObjectClass *libbalsa_vfs_parent_class = NULL;


static void libbalsa_vfs_class_init(LibbalsaVfsClass * klass);
static void libbalsa_vfs_init(LibbalsaVfs * self);
static void libbalsa_vfs_finalize(LibbalsaVfs * self);


gboolean
libbalsa_vfs_local_only(void)
{
    return FALSE;
}


GType
libbalsa_vfs_get_type(void)
{
    static GType libbalsa_vfs_type = 0;

    if (!libbalsa_vfs_type) {
        static const GTypeInfo libbalsa_vfs_type_info = {
            sizeof(LibbalsaVfsClass),     /* class_size */
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) libbalsa_vfs_class_init,   /* class_init */
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(LibbalsaVfs),  /* instance_size */
            0,                  /* n_preallocs */
            (GInstanceInitFunc) libbalsa_vfs_init,      /* instance_init */
            /* no value_table */
        };

        libbalsa_vfs_type =
            g_type_register_static(G_TYPE_OBJECT, "LibbalsaVfs",
                                   &libbalsa_vfs_type_info, 0);
    }

    return libbalsa_vfs_type;
}


static void
libbalsa_vfs_class_init(LibbalsaVfsClass * klass)
{
    GObjectClass *gobject_klass = G_OBJECT_CLASS(klass);

    libbalsa_vfs_parent_class = g_type_class_peek(G_TYPE_OBJECT);
    gobject_klass->finalize =
        (GObjectFinalizeFunc) libbalsa_vfs_finalize;
}


static void
libbalsa_vfs_init(LibbalsaVfs * self)
{
    self->priv = NULL;
}


static void
libbalsa_vfs_finalize(LibbalsaVfs * self)
{
    struct _LibbalsaVfsPriv * priv;

    g_return_if_fail(self != NULL);
    priv = self->priv;

    if (priv) {
        g_free(priv->file_uri);
        g_free(priv->file_utf8);
        g_free(priv->folder_uri);
        g_free(priv->mime_type);
        g_free(priv->charset);
        g_clear_object(&priv->gio_gfile);
        g_clear_object(&priv->info);
        g_free(priv);
    }

    libbalsa_vfs_parent_class->finalize(G_OBJECT(self));
}


LibbalsaVfs *
libbalsa_vfs_new(void)
{
    return LIBBALSA_VFS(g_object_new(LIBBALSA_TYPE_VFS, NULL));
}


LibbalsaVfs *
libbalsa_vfs_new_from_uri(const gchar * uri)
{
    LibbalsaVfs * retval;

    g_return_val_if_fail(uri, NULL);
    if (!(retval = libbalsa_vfs_new()))
        return NULL;

    if (!(retval->priv = g_new0(struct _LibbalsaVfsPriv, 1))) {
        g_object_unref(G_OBJECT(retval));
        return NULL;
    }
    retval->priv->text_attr = (LibBalsaTextAttribute) -1;

    retval->priv->file_uri = g_strdup(uri);
    retval->priv->gio_gfile = g_file_new_for_uri(uri);

    return retval;
}


/* create a new LibbalsaVfs object by appending text to the existing object
 * file (note: text is in utf8, not escaped) */
LibbalsaVfs *
libbalsa_vfs_append(const LibbalsaVfs * file, const gchar * text)
{
    gchar * p;
    gchar * q;
    LibbalsaVfs * retval;

    g_return_val_if_fail(file, NULL);
    g_return_val_if_fail(file->priv, NULL);
    g_return_val_if_fail(file->priv->file_uri, NULL);
    g_return_val_if_fail(text, NULL);

    /* fake an absolute file name which we can convert to an uri */
    p = g_strconcat("/", text, NULL);
    q = g_filename_to_uri(p, NULL, NULL);
    g_free(p);
    if (!q)
        return NULL;

    /* append to the existing uri and create the new object from it */
    p = g_strconcat(file->priv->file_uri, q + 8, NULL);
    g_free(q);
    retval = libbalsa_vfs_new_from_uri(p);
    g_free(p);
    return retval;
}


/* create a new LibbalsaVfs object by appending filename to the existing
 * object dir which describes a folder (note: filename is in utf8, not
 * escaped) */
LibbalsaVfs *
libbalsa_vfs_dir_append(const LibbalsaVfs * dir, const gchar * filename)
{   
    gchar * p;
    gchar * q;
    LibbalsaVfs * retval;

    g_return_val_if_fail(dir, NULL);
    g_return_val_if_fail(dir->priv, NULL);
    g_return_val_if_fail(dir->priv->file_uri, NULL);
    g_return_val_if_fail(filename, NULL);

    /* fake an absolute file name which we can convert to an uri */
    p = g_strconcat("/", filename, NULL);
    q = g_filename_to_uri(p, NULL, NULL);
    g_free(p);
    if (!q)
        return NULL;

    /* append to the existing uri and create the new object from it */
    p = g_strconcat(dir->priv->file_uri, q + 7, NULL);
    g_free(q);
    retval = libbalsa_vfs_new_from_uri(p);
    g_free(p);
    return retval;
}


/* return the text uri of the passed file, removing the last component */
const gchar *
libbalsa_vfs_get_folder(const LibbalsaVfs * file)
{
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, NULL);
    g_return_val_if_fail(file->priv, NULL);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, NULL);

    if (!priv->folder_uri) {
        gchar * p;

        if ((priv->folder_uri = g_strdup(priv->file_uri)) &&
            (p = g_utf8_strrchr(priv->folder_uri, -1, g_utf8_get_char("/"))))
            *p = '\0';
    }

    return priv->folder_uri;
}


/* return the text uri of the passed file */
const gchar *
libbalsa_vfs_get_uri(const LibbalsaVfs * file)
{
    g_return_val_if_fail(file, NULL);
    g_return_val_if_fail(file->priv, NULL);
    g_return_val_if_fail(file->priv->file_uri, NULL);

    return file->priv->file_uri;
}


/* return the text uri of the passed file as utf8 string (%xx replaced) */
const gchar *
libbalsa_vfs_get_uri_utf8(const LibbalsaVfs * file)
{
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, NULL);
    g_return_val_if_fail(file->priv, NULL);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, NULL);

    if (!priv->file_utf8) {
        gchar * p;
        gchar * q;

        if (!(p = priv->file_utf8 = g_malloc(strlen(priv->file_uri) + 1)))
            return NULL;
        q = priv->file_uri;
        while (*q != '\0') {
            if (*q == '%') {
                if (g_ascii_isxdigit(q[1]) && g_ascii_isxdigit(q[2])) {
                    gint val = (g_ascii_xdigit_value(q[1]) << 4) +
                        g_ascii_xdigit_value(q[2]);
                    *p++ = (gchar) val;
                    q += 3;
                } else
                    *p++ = *q++; /* hmmm - shouldn't happen! */
            } else
                *p++ = *q++;
        }
        *p = '\0';
    }

    return priv->file_utf8;
}


const gchar *
libbalsa_vfs_get_basename_utf8(const LibbalsaVfs * file)
{
    const gchar * uri_utf8 = libbalsa_vfs_get_uri_utf8(file);
    const gchar * p;

    if (uri_utf8 &&
        (p = g_utf8_strrchr(uri_utf8, -1, g_utf8_get_char("/"))))
        return p + 1;
    else
        return NULL;
}


const gchar *
libbalsa_vfs_get_mime_type(const LibbalsaVfs * file)
{
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, NULL);
    g_return_val_if_fail(file->priv, NULL);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, NULL);

    if (!priv->mime_type) {
        /* use GIO to determine the mime type of the file */
        g_return_val_if_fail(priv->gio_gfile, FALSE);

        if (!priv->info)
            priv->info = 
                g_file_query_info(priv->gio_gfile, GIO_INFO_ATTS,
                                  G_FILE_QUERY_INFO_NONE, NULL, NULL);
        if (priv->info)
            priv->mime_type =
                g_strdup(g_file_info_get_attribute_string(priv->info,
                                                          G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE));

        /* always fall back to application/octet-stream */
        if (!priv->mime_type)
            priv->mime_type = g_strdup("application/octet-stream");
    }

    return priv->mime_type;
}


const gchar *
libbalsa_vfs_get_charset(const LibbalsaVfs * file)
{
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, NULL);
    g_return_val_if_fail(file->priv, NULL);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, NULL);

    if (!priv->charset && priv->text_attr == (LibBalsaTextAttribute) -1) {
        libbalsa_vfs_get_text_attr(file);

        if (!(priv->text_attr & LIBBALSA_TEXT_HI_BIT))
            priv->charset = g_strdup("us-ascii");
        else if (priv->text_attr & LIBBALSA_TEXT_HI_UTF8)
            priv->charset = g_strdup("utf-8");
    }

    return priv->charset;
}


LibBalsaTextAttribute
libbalsa_vfs_get_text_attr(const LibbalsaVfs * file)
{
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, 0);
    g_return_val_if_fail(file->priv, 0);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, 0);

    if (priv->text_attr == (LibBalsaTextAttribute) -1) {
        GInputStream * stream;

        /* use GIO to determine the text attributes of the file */
        g_return_val_if_fail(priv->gio_gfile, 0);
        priv->text_attr = 0;

        /* read and check - see libbalsa_text_attr_file() */
        if ((stream = G_INPUT_STREAM(g_file_read(priv->gio_gfile, NULL, NULL))) != NULL) {
            gchar buf[1024];
            gchar *new_chars = buf;
            gboolean has_esc = FALSE;
            gboolean has_hi_bit = FALSE;
            gboolean has_hi_ctrl = FALSE;
            gboolean is_utf8 = TRUE;
            gssize bytes_read;

            while ((is_utf8 || (!has_esc || !has_hi_bit || !has_hi_ctrl)) &&
                   ((bytes_read = g_input_stream_read(stream,
                                                      new_chars,
                                                      (sizeof buf) - (new_chars - buf) - 1,
                                                      NULL,
                                                      NULL)) > 0)) {
                new_chars[bytes_read] = '\0';
                
                if (!has_esc || !has_hi_bit || !has_hi_ctrl) {
                    guchar * p;

                    for (p = (guchar *) new_chars; *p; p++)
                        if (*p == 0x1b)
                            has_esc = TRUE;
                        else if (*p >= 0x80) {
                            has_hi_bit = TRUE;
                            if (*p <= 0x9f)
                                has_hi_ctrl = TRUE;
                        }
                }

                if (is_utf8) {
                    const gchar *end;

                    new_chars = buf;
                    if (!g_utf8_validate(buf, -1, &end)) {
                        if (g_utf8_get_char_validated(end, -1) == (gunichar) (-1))
                            is_utf8 = FALSE;
                        else
                            /* copy any remaining bytes, including the
                             * terminating '\0', to start of buffer */
                            while ((*new_chars = *end++) != '\0')
                                new_chars++;
                    }
                }
            }

            g_object_unref(stream);

            if (has_esc)
                priv->text_attr |= LIBBALSA_TEXT_ESC;
            if (has_hi_bit)
                priv->text_attr |= LIBBALSA_TEXT_HI_BIT;
            if (has_hi_ctrl)
                priv->text_attr |= LIBBALSA_TEXT_HI_CTRL;
            if (is_utf8 && has_hi_bit)
                priv->text_attr |= LIBBALSA_TEXT_HI_UTF8;
        }
    }

    return priv->text_attr;
}


guint64
libbalsa_vfs_get_size(const LibbalsaVfs * file)
{
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, 0);
    g_return_val_if_fail(file->priv, 0);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, 0);

    /* use GIO to determine the size of the file */
    g_return_val_if_fail(priv->gio_gfile, 0);

    if (!priv->info)
        priv->info = 
            g_file_query_info(priv->gio_gfile, GIO_INFO_ATTS,
                              G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (priv->info)
        return g_file_info_get_attribute_uint64(priv->info,
                                                G_FILE_ATTRIBUTE_STANDARD_SIZE);

    return 0;
}


/* get a GMime stream for the passed file, either read-only or in
 * read-write mode */
GMimeStream *
libbalsa_vfs_create_stream(const LibbalsaVfs * file, mode_t mode, 
                           gboolean rdwr, GError ** err)
{
    struct _LibbalsaVfsPriv * priv;
    GMimeStream *stream;

    g_return_val_if_fail(file, NULL);
    g_return_val_if_fail(file->priv, NULL);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, NULL);

    /* use GIO to create a GMime stream */
    g_return_val_if_fail(priv->gio_gfile, NULL);

    stream = g_mime_stream_gio_new(priv->gio_gfile);
    g_mime_stream_gio_set_owner((GMimeStreamGIO *) stream, FALSE);

    return stream;
}


/* return TRUE if the passed file exists */
gboolean
libbalsa_vfs_file_exists(const LibbalsaVfs * file)
{
    gboolean result = FALSE;
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, FALSE);
    g_return_val_if_fail(file->priv, FALSE);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, FALSE);

    /* use GIO to get the file's attributes - fails if the file does not exist */
    g_return_val_if_fail(priv->gio_gfile, 0);

    if (!priv->info)
        priv->info = 
            g_file_query_info(priv->gio_gfile, GIO_INFO_ATTS,
                              G_FILE_QUERY_INFO_NONE, NULL, NULL);
    result = priv->info != NULL;

    return result;
}


gboolean
libbalsa_vfs_is_regular_file(const LibbalsaVfs * file, GError **err)
{
    gboolean result = FALSE;
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, FALSE);
    g_return_val_if_fail(file->priv, FALSE);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, FALSE);

    /* use GIO to check if the file is a regular one which can be read */
    g_return_val_if_fail(priv->gio_gfile, 0);

    if (!priv->info)
        priv->info = 
            g_file_query_info(priv->gio_gfile, GIO_INFO_ATTS,
                              G_FILE_QUERY_INFO_NONE, NULL, err);
    if (priv->info) {
        if (g_file_info_get_file_type(priv->info) != G_FILE_TYPE_REGULAR)
            g_set_error(err, LIBBALSA_VFS_ERROR_QUARK, -1,
                        _("not a regular file"));
        else if (!g_file_info_get_attribute_boolean(priv->info,
                                                    G_FILE_ATTRIBUTE_ACCESS_CAN_READ)) {
            /* the read flag may not be set for some remote file systems (like smb),
             * so try to actually open the file... */
            GFileInputStream * stream = g_file_read(priv->gio_gfile, NULL, err);
            if (stream) {
                g_object_unref(stream);
                result = TRUE;
            }
        } else
            result = TRUE;
    }

    return result;
}


/* unlink the passed file, return 0 on success and -1 on error */
gint
libbalsa_vfs_file_unlink(const LibbalsaVfs * file, GError **err)
{
    gint result = -1;
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, -1);
    g_return_val_if_fail(file->priv, -1);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, -1);

    /* use GIO to delete the file */
    g_return_val_if_fail(priv->gio_gfile, -1);
    if (g_file_delete(priv->gio_gfile, NULL, err))
        result = 0;

    return result;
}


gboolean
libbalsa_vfs_launch_app(const LibbalsaVfs * file, GObject * object, GError **err)
{
    GAppInfo *app;
    GList * args;
    gboolean result;

    g_return_val_if_fail(file != NULL, FALSE);
    g_return_val_if_fail(object != NULL, FALSE);

    app = G_APP_INFO(g_object_get_data(object, LIBBALSA_VFS_MIME_ACTION));
    if (!app) {
        g_set_error(err, LIBBALSA_VFS_ERROR_QUARK, -1,
                    _("Cannot launch, missing application"));
        return FALSE;
    }
    args = g_list_prepend(NULL, file->priv->gio_gfile);
    result = g_app_info_launch(app, args, NULL, err);
    g_list_free(args);
    return result;
}


gboolean
libbalsa_vfs_launch_app_for_body(LibBalsaMessageBody * mime_body,
                                 GObject * object, GError **err)
{
    gchar *uri;
    LibbalsaVfs * file;
    gboolean result;

    g_return_val_if_fail(mime_body != NULL, FALSE);
    g_return_val_if_fail(object != NULL, FALSE);

    if (!libbalsa_message_body_save_temporary(mime_body, err))
        return FALSE;

    uri = g_filename_to_uri(mime_body->temp_filename, NULL, NULL);
    file = libbalsa_vfs_new_from_uri(uri);
    g_free(uri);
    result = libbalsa_vfs_launch_app(file,object , err);
    g_object_unref(file);

    return result;
}


gchar *
libbalsa_vfs_content_description(const gchar * mime_type)
{
    g_return_val_if_fail(mime_type != NULL, NULL);

    return g_content_type_get_description(mime_type);
}

gchar *
libbalsa_vfs_content_type_of_buffer(const guchar * buffer,
                                    gsize length)
{
    gchar * retval;
    gboolean content_uncertain;

    g_return_val_if_fail(buffer != NULL, NULL);
    g_return_val_if_fail(length > 0, NULL);

    retval = g_content_type_guess(NULL, buffer, length, &content_uncertain);
    if (content_uncertain) {
        g_free(retval);
        retval = g_strdup("application/octet-stream");
    }
    return retval;
}


static void 
gio_add_vfs_menu_item(GtkMenu * menu, GAppInfo *app, GCallback callback,
                      gpointer data)
{
    gchar *menu_label =
        g_strdup_printf(_("Open with %s"), g_app_info_get_name(app));
    GtkWidget *menu_item = gtk_menu_item_new_with_label (menu_label);
    
    g_object_ref(G_OBJECT(app));
    g_object_set_data_full(G_OBJECT(menu_item), LIBBALSA_VFS_MIME_ACTION,
			   app, g_object_unref);
    g_signal_connect(G_OBJECT (menu_item), "activate", callback, data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_free(menu_label);
}


/* fill the passed menu with vfs items */
void
libbalsa_vfs_fill_menu_by_content_type(GtkMenu * menu,
				       const gchar * content_type,
				       GCallback callback, gpointer data)
{
    GList* list;
    GAppInfo *def_app;
    GList *app_list;
    
    g_return_if_fail(data != NULL);

    if ((def_app = g_app_info_get_default_for_type(content_type, FALSE)))
        gio_add_vfs_menu_item(menu, def_app, callback, data);

    app_list = g_app_info_get_all_for_type(content_type);
    for (list = app_list; list != NULL; list = list->next) {
        GAppInfo *app = G_APP_INFO(list->data);

        if (app && g_app_info_should_show(app) &&
            (!def_app || !g_app_info_equal(app, def_app)))
            gio_add_vfs_menu_item(menu, app, callback, data);
    }
    if (def_app)
        g_object_unref(def_app);
    if (app_list) {
        g_list_foreach(app_list, (GFunc) g_object_unref, NULL);
        g_list_free(app_list);
    }
}

GtkWidget *
libbalsa_vfs_mime_button(LibBalsaMessageBody * mime_body,
                         const gchar * content_type,
                         GCallback callback, gpointer data)
{
    GtkWidget *button = NULL;
    gchar *msg;
    GAppInfo *app = g_app_info_get_default_for_type(content_type, FALSE);

    if (app) {
	msg = g_strdup_printf(_("Open _part with %s"), g_app_info_get_name(app));
	button = gtk_button_new_with_mnemonic(msg);
	g_object_set_data_full(G_OBJECT(button), LIBBALSA_VFS_MIME_ACTION,
			       (gpointer) app, g_object_unref);
	g_free(msg);

	g_signal_connect(G_OBJECT(button), "clicked",
                         callback, data);
    }

    return button;
}
