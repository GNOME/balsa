/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* GMime message header filter
 * Written/Copyright (c) by Albrecht Dreß <albrecht.dress@arcor.de> 2017
 * This module remove RFC 822 massage headers which shall not be sent to the MTA from from a message stream, including Bcc, Status,
 * X-Status and X-Balsa-*.
 *
 * The basic structure of this file has been shamelessly stolen from the gmime-filter-* files, written by Jeffrey Stedfast.
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library; if not, write to the Free
 * Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <string.h>
#include "gmime-filter-header.h"

static void g_mime_filter_header_finalize(GObject *object);

static GMimeFilter *filter_copy(GMimeFilter *filter);
static void filter_filter(GMimeFilter *filter, char *in, size_t len, size_t prespace, char **out, size_t *outlen,
						  size_t *outprespace);
static void filter_complete(GMimeFilter *filter, char *in, size_t len, size_t prespace, char **out, size_t *outlen,
							size_t *outprespace);
static void filter_reset(GMimeFilter *filter);

struct _GMimeFilterHeader {
	GMimeFilter parent_object;
	gboolean headers_done;
	gboolean drop_header;
};

G_DEFINE_TYPE(GMimeFilterHeader, g_mime_filter_header, GMIME_TYPE_FILTER)


static void
g_mime_filter_header_class_init(GMimeFilterHeaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GMimeFilterClass *filter_class = GMIME_FILTER_CLASS(klass);

	object_class->finalize = g_mime_filter_header_finalize;

	filter_class->copy = filter_copy;
	filter_class->filter = filter_filter;
	filter_class->complete = filter_complete;
	filter_class->reset = filter_reset;
}

static void
g_mime_filter_header_init(GMimeFilterHeader *self)
{
	self->headers_done = FALSE;
	self->drop_header = FALSE;
}

static void
g_mime_filter_header_finalize(GObject *object)
{
	G_OBJECT_CLASS(g_mime_filter_header_parent_class)->finalize(object);
}

static GMimeFilter *
filter_copy(GMimeFilter *filter)
{
	return g_mime_filter_header_new();
}

static void
filter_filter(GMimeFilter *filter, char *inbuf, size_t inlen, size_t prespace,
	       	  char **outbuf, size_t *outlen, size_t *outprespace)
{
	GMimeFilterHeader *self = GMIME_FILTER_HEADER(filter);

	g_mime_filter_set_size(filter, inlen, FALSE);

	if (self->headers_done) {
		memcpy(filter->outbuf, inbuf, inlen);
		*outlen = inlen;
	} else {
		gchar *newline;
		gchar *outptr;

		outptr = filter->outbuf;
		*outlen = 0U;
		newline = memchr(inbuf, '\n', inlen);
		while ((newline != NULL) && !self->headers_done) {
			size_t count;

			/* number of chars in this line */
			count = (newline - inbuf) + 1U;

			/* check for for folded header continuation */
			if ((inbuf[0] == ' ') || (inbuf[0] == '\t')) {
				/* nothing to do, do not change the state */
			} else if (newline == inbuf) {
				self->headers_done = TRUE;
			} else if (((count >= 4U) && (g_ascii_strncasecmp(inbuf, "Bcc:", 4U) == 0)) ||
				   	   ((count >= 7U) && (g_ascii_strncasecmp(inbuf, "Status:", 7U) == 0)) ||
					   ((count >= 9U) && (g_ascii_strncasecmp(inbuf, "X-Status:", 9U) == 0)) ||
					   ((count >= 8U) && (g_ascii_strncasecmp(inbuf, "X-Balsa-", 8U) == 0))) {
				self->drop_header = TRUE;
			} else {
				self->drop_header = FALSE;
			}

			/* copy if we want to keep this header */
			if (!self->drop_header) {
				memcpy(outptr, inbuf, count);
				outptr = &outptr[count];
				*outlen += count;
			}

			/* adjust */
			inbuf = &inbuf[count];
			inlen -= count;
			if (!self->headers_done && (inlen > 0)) {
				newline = memchr(inbuf, '\n', inlen);
			} else {
				newline = NULL;
			}
		}

		/* back up left-over data */
		if (inlen > 0U) {
			g_mime_filter_backup(filter, inbuf, inlen);
		}
	}
	*outprespace = filter->outpre;
	*outbuf = filter->outbuf;
}

static void
filter_complete(GMimeFilter *filter, char *inbuf, size_t inlen, size_t prespace,
		 	 	char **outbuf, size_t *outlen, size_t *outprespace)
{
	filter_filter(filter, inbuf, inlen, prespace, outbuf, outlen, outprespace);
}

static void
filter_reset(GMimeFilter *filter)
{
}

GMimeFilter *
g_mime_filter_header_new()
{
	GMimeFilterHeader *header;

	header = g_object_new(GMIME_TYPE_FILTER_HEADER, NULL);

	return (GMimeFilter *) header;
}
