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

#ifndef GMIME_FILTER_HEADER_H__
#define GMIME_FILTER_HEADER_H__

#include <gmime/gmime-filter.h>

G_BEGIN_DECLS

#define GMIME_TYPE_FILTER_HEADER g_mime_filter_header_get_type()

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GMimeFilter, g_object_unref)

G_DECLARE_FINAL_TYPE(GMimeFilterHeader,
                     g_mime_filter_header,
                     GMIME,
                     FILTER_HEADER,
                     GMimeFilter);

GMimeFilter *g_mime_filter_header_new(void);

G_END_DECLS

#endif /* GMIME_FILTER_HEADER_H__ */
