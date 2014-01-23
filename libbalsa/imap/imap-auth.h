#ifndef __IMAP_AUTH_H__
#define __IMAP_AUTH_H__ 1
/* libimap library.
 * Copyright (C) 2003-2004 Pawel Salek.
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

#include "libimap.h"
#include "imap-handle.h"

typedef ImapResult (*ImapAuthenticator)(ImapMboxHandle* handle);
ImapResult imap_authenticate(ImapMboxHandle* handle);
ImapResult imap_auth_cram(ImapMboxHandle* handle);
ImapResult imap_auth_login(ImapMboxHandle* handle);
ImapResult imap_auth_gssapi(ImapMboxHandle* handle);

#endif
