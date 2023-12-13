// Copyright (C) 2022, 2023 - Tillitis AB
// Copyright (C) 2023 - Daniel Lublin
// SPDX-License-Identifier: GPL-2.0-only

#include "app_proto.h"
#include <tkey/qemu_debug.h>

// Send reply frame with response status Not OK (NOK==1), shortest length
void appreply_nok(struct frame_header hdr)
{
	writebyte(genhdr(hdr.id, hdr.endpoint, 0x1, LEN_1));
	writebyte(0);
}

// Send app reply with frame header, response code, and LEN_X-1 bytes from buf
void appreply(struct frame_header hdr, enum appcmd rspcode, void *buf)
{
	enum cmdlen len = 0;
	size_t nbytes = 0;

	switch (rspcode) {
	case APP_RSP_GET_PUBKEY:
	case APP_RSP_DO_ECDH:
		len = LEN_128;
		nbytes = 128;
		break;
	case APP_RSP_GET_NAMEVERSION:
		len = LEN_32;
		nbytes = 32;
		break;
	case APP_RSP_UNKNOWN_CMD:
		len = LEN_1;
		nbytes = 1;
		break;
	default:
		qemu_puts("appreply(): Unknown response code: ");
		qemu_puthex(rspcode);
		qemu_lf();
		return;
	}

	// Frame Protocol Header
	writebyte(genhdr(hdr.id, hdr.endpoint, 0x0, len));

	// app protocol header is 1 byte response code
	writebyte(rspcode);
	nbytes--;

	write(buf, nbytes);
}
