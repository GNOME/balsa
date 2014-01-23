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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LIBBALSA_MIME_STREAM_SHARED_H__
#define __LIBBALSA_MIME_STREAM_SHARED_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#if BALSA_USE_THREADS

#include <gmime/gmime-stream-fs.h>

#define LIBBALSA_TYPE_MIME_STREAM_SHARED                           \
    (libbalsa_mime_stream_shared_get_type ())
#define LIBBALSA_MIME_STREAM_SHARED(obj)                           \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                             \
                                LIBBALSA_TYPE_MIME_STREAM_SHARED,  \
                                LibBalsaMimeStreamShared))
#define LIBBALSA_MIME_STREAM_SHARED_CLASS(klass)                   \
    (G_TYPE_CHECK_CLASS_CAST((klass),                              \
                             LIBBALSA_TYPE_MIME_STREAM_SHARED,     \
                             LibBalsaMimeStreamSharedClass))
#define LIBBALSA_IS_MIME_STREAM_SHARED(obj)                        \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),                             \
                                LIBBALSA_TYPE_MIME_STREAM_SHARED))
#define LIBBALSA_IS_MIME_STREAM_SHARED_CLASS(klass)                \
    (G_TYPE_CHECK_CLASS_TYPE((klass),                              \
                             LIBBALSA_TYPE_MIME_STREAM_SHARED))
#define LIBBALSA_MIME_STREAM_SHARED_GET_CLASS(obj)                 \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                              \
                               LIBBALSA_TYPE_MIME_STREAM_SHARED,   \
                               LibBalsaMimeStreamSharedClass))

typedef struct _LibBalsaMimeStreamShared      LibBalsaMimeStreamShared;
typedef struct _LibBalsaMimeStreamSharedClass LibBalsaMimeStreamSharedClass;

GType libbalsa_mime_stream_shared_get_type(void);

GMimeStream *libbalsa_mime_stream_shared_new(int fd);

void libbalsa_mime_stream_shared_lock  (GMimeStream * stream);
void libbalsa_mime_stream_shared_unlock(GMimeStream * stream);

#else                           /* BALSA_USE_THREADS */

#define libbalsa_mime_stream_shared_new(fd) g_mime_stream_fs_new(fd)
#define libbalsa_mime_stream_shared_lock(stream)
#define libbalsa_mime_stream_shared_unlock(stream)

#endif                          /* BALSA_USE_THREADS */

#endif                          /* __LIBBALSA_MIME_STREAM_SHARED_H__ */
