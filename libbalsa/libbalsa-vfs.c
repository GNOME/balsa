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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gmime/gmime.h>
#include "gmime-stream-gnome-vfs.h"
#include "misc.h"
#include "libbalsa-vfs.h"


#ifdef HAVE_GNOME
#  include <libgnomevfs/gnome-vfs.h>
#  define USE_GNOME_VFS  1
#else
#  undef USE_GNOME_VFS
#endif


#define LIBBALSA_VFS_ERROR_QUARK (g_quark_from_static_string("libbalsa-vfs"))


struct _LibbalsaVfsPriv {
    gchar * file_uri;
    gchar * file_utf8;
    gchar * folder_uri;
    gchar * mime_type;
    gchar * charset;
    LibBalsaTextAttribute text_attr;
#ifdef USE_GNOME_VFS
    GnomeVFSURI * gvfs_uri;
    GnomeVFSFileInfo * info;
#else
    gchar * local_name;
#endif
};


static GObjectClass *libbalsa_vfs_parent_class = NULL;


static void libbalsa_vfs_class_init(LibbalsaVfsClass * klass);
static void libbalsa_vfs_init(LibbalsaVfs * self);
static void libbalsa_vfs_finalize(LibbalsaVfs * self);


gboolean
libbalsa_vfs_local_only(void)
{
#ifdef USE_GNOME_VFS
    return FALSE;
#else
    return TRUE;
#endif
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
#ifdef USE_GNOME_VFS
        if (priv->gvfs_uri)
            gnome_vfs_uri_unref(priv->gvfs_uri);
        if (priv->info)
            gnome_vfs_file_info_unref(priv->info);
#else
        g_free(priv->local_name);
#endif
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
#ifdef USE_GNOME_VFS
    retval->priv->gvfs_uri = gnome_vfs_uri_new(uri);
    if (!retval->priv->gvfs_uri)
        g_message(_("Failed to convert %s to a Gnome VFS URI"), uri);
#else
    retval->priv->local_name = g_filename_from_uri(uri, NULL, NULL);
#endif

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
#ifdef USE_GNOME_VFS
        /* use GnomeVFS to determine the mime type of the file */
        g_return_val_if_fail(priv->gvfs_uri, FALSE);

        if (!priv->info)
            priv->info = gnome_vfs_file_info_new();

        if (priv->info) {
            if ((priv->info->valid_fields &
                 GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE) == 0)
                gnome_vfs_get_file_info_uri(priv->gvfs_uri, priv->info,
                                            GNOME_VFS_FILE_INFO_GET_MIME_TYPE |
                                            GNOME_VFS_FILE_INFO_FORCE_SLOW_MIME_TYPE |
                                            GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
            if ((priv->info->valid_fields &
                 GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE) != 0)
                priv->mime_type = g_strdup(gnome_vfs_file_info_get_mime_type(priv->info));
        }

        /* always fall back to application/octet-stream */
        if (!priv->mime_type)
#endif
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
#ifdef USE_GNOME_VFS
        GnomeVFSHandle *handle;

        /* use GnomeVFS to determine the text attributes of the file */
        g_return_val_if_fail(priv->gvfs_uri, 0);
        priv->text_attr = 0;

        /* read and check - see libbalsa_text_attr_file() */
        if (gnome_vfs_open_uri(&handle, priv->gvfs_uri, GNOME_VFS_OPEN_READ) ==
            GNOME_VFS_OK) {
            gchar buf[1024];
            gchar *new_chars = buf;
            gboolean has_esc = FALSE;
            gboolean has_hi_bit = FALSE;
            gboolean has_hi_ctrl = FALSE;
            gboolean is_utf8 = TRUE;
            GnomeVFSFileSize bytes_read;

            while ((is_utf8 || (!has_esc || !has_hi_bit || !has_hi_ctrl)) &&
                   gnome_vfs_read(handle, new_chars, (sizeof buf) - (new_chars - buf) - 1,
                                  &bytes_read) == GNOME_VFS_OK) {
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

            gnome_vfs_close(handle);

            if (has_esc)
                priv->text_attr |= LIBBALSA_TEXT_ESC;
            if (has_hi_bit)
                priv->text_attr |= LIBBALSA_TEXT_HI_BIT;
            if (has_hi_ctrl)
                priv->text_attr |= LIBBALSA_TEXT_HI_CTRL;
            if (is_utf8 && has_hi_bit)
                priv->text_attr |= LIBBALSA_TEXT_HI_UTF8;
        }
#else
        /* use function from misc to get the text attributes */
        g_return_val_if_fail(priv->local_name, 0);

        priv->text_attr = libbalsa_text_attr_file(priv->local_name);
#endif
    }

    return priv->text_attr;
}


gsize
libbalsa_vfs_get_size(const LibbalsaVfs * file)
{
#ifndef USE_GNOME_VFS
    struct stat s;
#endif
    gsize retval = 0;
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, 0);
    g_return_val_if_fail(file->priv, 0);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, 0);

#ifdef USE_GNOME_VFS
    /* use GnomeVFS to determine the size of the file */
    g_return_val_if_fail(priv->gvfs_uri, 0);

    if (!priv->info)
        priv->info = gnome_vfs_file_info_new();

    if (priv->info) {
        if ((priv->info->valid_fields &
             GNOME_VFS_FILE_INFO_FIELDS_SIZE) == 0)
            gnome_vfs_get_file_info_uri(priv->gvfs_uri, priv->info,
                                        GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS |
                                       GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
        if ((priv->info->valid_fields &
             GNOME_VFS_FILE_INFO_FIELDS_SIZE) != 0)
            retval = (gsize) priv->info->size;
    }
#else
    /* call stat on the file to get the size */
    g_return_val_if_fail(priv->local_name, 0);

    if (g_stat(priv->local_name, &s) == 0)
        retval = (gsize) s.st_size;
#endif

    return retval;
}


/* get a GMime stream for the passed file, either read-only or in
 * read-write mode */
GMimeStream *
libbalsa_vfs_create_stream(const LibbalsaVfs * file, mode_t mode, 
                           gboolean rdwr, GError ** err)
{
#ifdef USE_GNOME_VFS
    GnomeVFSOpenMode openmode = GNOME_VFS_OPEN_RANDOM | GNOME_VFS_OPEN_READ;
    GnomeVFSHandle * handle;
    GnomeVFSResult result;
#else
    int fd;
    int flags = O_EXCL;
#endif
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, NULL);
    g_return_val_if_fail(file->priv, NULL);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, NULL);

#ifdef USE_GNOME_VFS
    /* use GnomeVFS to create a GMime Gnome VFS stream */
    g_return_val_if_fail(priv->gvfs_uri, NULL);

    if (rdwr) {
        openmode |= GNOME_VFS_OPEN_WRITE;
        result = gnome_vfs_create_uri(&handle, priv->gvfs_uri,
                                      openmode, TRUE, mode);
    } else
        result = gnome_vfs_open_uri(&handle, priv->gvfs_uri, openmode);
    if (result != GNOME_VFS_OK) {
        g_set_error(err, LIBBALSA_ERROR_QUARK, result,
                    "%s", gnome_vfs_result_to_string(result));
        return NULL;
    }

    return g_mime_stream_gvfs_new(handle);
#else
    /* use libc to create a GMime file system stream */
    g_return_val_if_fail(priv->local_name, NULL);

    flags |= rdwr ? O_CREAT | O_RDWR : O_RDONLY;

#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif

    if ((fd = libbalsa_safe_open(priv->local_name, flags, mode, err)) < 0)
	return NULL;

    return g_mime_stream_fs_new(fd);
#endif
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

#ifdef USE_GNOME_VFS
    /* use GnomeVFS to check if the file exists */
    g_return_val_if_fail(priv->gvfs_uri, FALSE);

    if (!priv->info)
        priv->info = gnome_vfs_file_info_new();

    if (priv->info) {
        if ((priv->info->valid_fields &
             GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS) == 0)
            gnome_vfs_get_file_info_uri(priv->gvfs_uri, priv->info,
                                        GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS |
                                        GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
        if ((priv->info->valid_fields &
             GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS))
            result = TRUE;
    }
#else
    /* use g_access to check if the (local) file exists */
    g_return_val_if_fail(priv->local_name, FALSE);

    result = (g_access(priv->local_name, F_OK) == 0);
#endif

    return result;
}


gboolean
libbalsa_vfs_is_regular_file(const LibbalsaVfs * file, GError **err)
{
#ifndef USE_GNOME_VFS
    struct stat s;
#endif
    gboolean result = FALSE;
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, FALSE);
    g_return_val_if_fail(file->priv, FALSE);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, FALSE);

#ifdef USE_GNOME_VFS
    /* use GnomeVFS to check if the file is a regular one which can be read */
    g_return_val_if_fail(priv->gvfs_uri, FALSE);

    if (!priv->info)
        priv->info = gnome_vfs_file_info_new();

    if (priv->info) {
        if ((priv->info->valid_fields &
             (GNOME_VFS_FILE_INFO_FIELDS_TYPE |
              GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS)) == 0)
            gnome_vfs_get_file_info_uri(priv->gvfs_uri, priv->info,
                                        GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS |
                                        GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
        g_message("fields = %x", priv->info->valid_fields);
        if ((priv->info->valid_fields &
             (GNOME_VFS_FILE_INFO_FIELDS_TYPE |
              GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS)) !=
            (GNOME_VFS_FILE_INFO_FIELDS_TYPE |
             GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS))
            g_set_error(err, LIBBALSA_VFS_ERROR_QUARK, -1,
                        _("cannot read file information"));
        else if (priv->info->type != GNOME_VFS_FILE_TYPE_REGULAR)
            g_set_error(err, LIBBALSA_VFS_ERROR_QUARK, -1,
                        _("not a regular file"));
        else if ((priv->info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_ACCESS) &&
                 !(priv->info->permissions & GNOME_VFS_PERM_ACCESS_READABLE))
            g_set_error(err, LIBBALSA_VFS_ERROR_QUARK, -1, _("cannot read"));
        else
            result = TRUE;
    }
#else
    /* use libc to check if the file is a regular one which can be read */
    g_return_val_if_fail(priv->local_name, FALSE);

    if (g_stat(priv->local_name, &s) != 0)
        g_set_error(err, LIBBALSA_VFS_ERROR_QUARK, errno,
                    g_strerror(errno));
    else if (!S_ISREG(s.st_mode))
        g_set_error(err, LIBBALSA_VFS_ERROR_QUARK, -1, _("not a regular file"));
    else if (g_access(priv->local_name, R_OK) != 0)
        g_set_error(err, LIBBALSA_VFS_ERROR_QUARK, -1, _("cannot read"));
    else
        result = TRUE;
#endif

    return result;
}


/* unlink the passed file, return 0 on success and -1 on error */
gint
libbalsa_vfs_file_unlink(const LibbalsaVfs * file, GError **err)
{
#ifdef USE_GNOME_VFS
    GnomeVFSResult vfs_res;
#endif
    gint result = -1;
    struct _LibbalsaVfsPriv * priv;

    g_return_val_if_fail(file, -1);
    g_return_val_if_fail(file->priv, -1);
    priv = file->priv;
    g_return_val_if_fail(priv->file_uri, -1);

#ifdef USE_GNOME_VFS
    /* use GnomeVFS to unlink the file */
    g_return_val_if_fail(priv->gvfs_uri, -1);

    if ((vfs_res = gnome_vfs_unlink_from_uri(priv->gvfs_uri)) != GNOME_VFS_OK)
        g_set_error(err, LIBBALSA_VFS_ERROR_QUARK, vfs_res,
                    "%s", gnome_vfs_result_to_string(vfs_res));
    else
        result = 0;
#else
    /* use g_unlink to unlink the (local) file */
    g_return_val_if_fail(priv->local_name, -1);

    result = g_unlink(priv->local_name);
    if (result != 0)
        g_set_error(err, LIBBALSA_VFS_ERROR_QUARK, errno,
                    g_strerror(errno));
#endif

    return result;
}
