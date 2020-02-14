/* libimap library.
 * Copyright (C) 2003-2016 Pawel Salek.
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "siobuf-nc.h"
#include "imap-handle.h"
#include "imap_private.h"

/** Enables COMPRESS extension if available. Assumes that the handle
    is already locked. */
ImapResponse
imap_compress(ImapMboxHandle *handle)
{
	if (!handle->enable_compress ||
		!imap_mbox_handle_can_do(handle, IMCAP_COMPRESS_DEFLATE))
		return IMR_NO;

	if (imap_cmd_exec(handle, "COMPRESS DEFLATE") != IMR_OK)
		return IMR_NO;

	if (net_client_start_compression(NET_CLIENT(handle->sio), NULL)) {
		return IMR_OK;
	} else {
		g_warning("%s: SIO not set!", __func__);
		return IMR_NO;
	}
}
