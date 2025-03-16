//* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2024 Stuart Parmenter and others, see the file AUTHORS for a list.
 *
 * This module implements DMARC/DKIM signature checks, according to the following standards:
 * - RFC 6376: DomainKeys Identified Mail (DKIM) Signatures
 * - RFC 7489: Domain-based Message Authentication, Reporting, and Conformance (DMARC)
 * - RFC 8463: A New Cryptographic Signature Method for DomainKeys Identified Mail (DKIM)
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

#ifndef LIBBALSA_DKIM_H_
#define LIBBALSA_DKIM_H_


#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif


#include "message.h"


G_BEGIN_DECLS


/** The message does not include any @c DKIM-Signature headers. */
#define DKIM_NONE							-1
/** At least one valid DKIM signature is present, matching the required DMARC mode if available. */
#define DKIM_SUCCESS						0
/** At least one valid DKIM signature is present, but the verification produced a warning. */
#define DKIM_WARNING						1
/** No valid DKIM signature is present, or it does not match the required DMARC mode if available. */
#define DKIM_FAILED							2


#define LIBBALSA_TYPE_DKIM					(libbalsa_dkim_get_type())
G_DECLARE_FINAL_TYPE(LibBalsaDkim, libbalsa_dkim, LIBBALSA, DKIM, GObject)


/** @brief Check the DKIM and DMARC status of a message
 *
 * @param[in] message message
 *
 * Check the DKIM and DMARC status of the message and of all embedded message parts (MIME type <c>message/rfc822</c>) and assign
 * the resulting DKIM objects to the respective @ref LibBalsaMessageBody::dkim fields.
 */
void libbalsa_dkim_message(LibBalsaMessage *message);


/** @brief Get the DKIM/DMARC status value
 *
 * @param[in] dkim DKIM status object, may be NULL
 * @return the status code (@ref DKIM_SUCCESS, etc)
 */
gint libbalsa_dkim_status_code(LibBalsaDkim *dkim);

/** @brief Get the short DKIM/DMARC status message
 *
 * @param[in] dkim DKIM status object
 * @return a status message suitable for display in the headers box
 */
const gchar *libbalsa_dkim_status_str_short(LibBalsaDkim *dkim);

/** @brief Get the long DKIM/DMARC status message
 *
 * @param[in] dkim DKIM status object
 * @return the verbose DKIM/DMARC status message
 */
const gchar *libbalsa_dkim_status_str_long(LibBalsaDkim *dkim);


G_END_DECLS


#endif /* LIBBALSA_DKIM_H_ */
