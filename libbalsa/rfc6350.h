/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others, see the file AUTHORS for a list.
 *
 * This module provides a simple RFC 6350 (aka VCard 4.0, see https://tools.ietf.org/html/rfc6350) parser which extracts a single
 * VCard from a GDataInputStream and returns it as LibBalsaAddress.
 *
 * Written by Copyright (C) 2016 Albrecht Dreß <albrecht.dress@arcor.de>.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if not, see
 * <https://www.gnu.org/licenses/>.
 */

#ifndef LIBBALSA_RFC6350_H_
#define LIBBALSA_RFC6350_H_


#include "address.h"


/** \brief Extract a single VCard 4.0
 *
 * \param stream VCard input stream
 * \param eos filled with TRUE is the end of the input stream has been reached
 * \param error filled with error information on error
 * \return a new address object on success
 *
 * Read a VCard from the input stream and extract all relevant fields into a LibBalsaAddress.  A LibBalsaAddress is returned only if
 * it contains any email address.  The caller shall distinguish this case from an error by checking if the error argument is filled.
 * The input stream is positioned immediately after the card read.
 *
 * \note The caller shall unref the returned address object.
 */
LibBalsaAddress *rfc6350_parse_from_stream(GDataInputStream *stream,
										   gboolean			*eos,
										   GError			**error);

/** \brief Create a VCard from an address
 *
 * \param[in] address address object
 * \param[in] vcard4 \c TRUE to create a VCard version 4.0, \c FALSE to use version 3.0
 * \param[in] add_uuid \c TRUE to add a \c UID item
 * \return the VCard
 */
gchar *rfc6350_from_address(LibBalsaAddress *address,
							gboolean vcard4,
							gboolean add_uuid);


#endif /* LIBBALSA_RFC6350_H_ */
