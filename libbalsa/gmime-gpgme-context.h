/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifndef __GMIME_GPGME_CONTEXT_H__
#define __GMIME_GPGME_CONTEXT_H__

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <gmime/gmime-cipher-context.h>
#include <gpgme.h>

#define GMIME_TYPE_GPGME_CONTEXT            (g_mime_gpgme_context_get_type ())
#define GMIME_GPGME_CONTEXT(obj)            (GMIME_CHECK_CAST ((obj), GMIME_TYPE_GPGME_CONTEXT, GMimeGpgMEContext))
#define GMIME_GPGME_CONTEXT_CLASS(klass)    (GMIME_CHECK_CLASS_CAST ((klass), GMIME_TYPE_GPGME_CONTEXT, GMimeGpgMEContextClass))
#define GMIME_IS_GPGME_CONTEXT(obj)         (GMIME_CHECK_TYPE ((obj), GMIME_TYPE_GPGME_CONTEXT))
#define GMIME_IS_GPGME_CONTEXT_CLASS(klass) (GMIME_CHECK_CLASS_TYPE ((klass), GMIME_TYPE_GPGME_CONTEXT))
#define GMIME_GPGME_CONTEXT_GET_CLASS(obj)  (GMIME_CHECK_GET_CLASS ((obj), GMIME_TYPE_GPGME_CONTEXT, GMimeGpgMEContextClass))

typedef struct _GMimeGpgMEContext GMimeGpgMEContext;
typedef struct _GMimeGpgMEContextClass GMimeGpgMEContextClass;


struct _GMimeGpgMEContext {
	GMimeCipherContext parent_object;
	
	GpgmeCtx gpgme_ctx;
	GpgmeSigStat sig_status;
	GpgmeKey key;
	char *fingerprint;
	time_t sign_time;
	char *micalg;
};

struct _GMimeGpgMEContextClass {
	GMimeCipherContextClass parent_class;
	
};


GType g_mime_gpgme_context_get_type (void);


GMimeCipherContext *g_mime_gpgme_context_new (GMimeSession *session);

gboolean g_mime_gpgme_context_get_always_trust (GMimeGpgMEContext *ctx);
void g_mime_gpgme_context_set_always_trust (GMimeGpgMEContext *ctx, gboolean always_trust);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GMIME_GPGME_CONTEXT_H__ */
