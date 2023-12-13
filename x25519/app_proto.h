// Copyright (C) 2022 - Tillitis AB
// Copyright (C) 2023 - Daniel Lublin
// SPDX-License-Identifier: GPL-2.0-only

#ifndef APP_PROTO_H
#define APP_PROTO_H

#include <tkey/lib.h>
#include <tkey/proto.h>

// clang-format off
enum appcmd {
	APP_CMD_GET_NAMEVERSION = 0x01,
	APP_RSP_GET_NAMEVERSION = 0x02,
	APP_CMD_GET_PUBKEY      = 0x03,
	APP_RSP_GET_PUBKEY      = 0x04,
	APP_CMD_DO_ECDH         = 0x05,
	APP_RSP_DO_ECDH         = 0x06,

	APP_RSP_UNKNOWN_CMD     = 0xff,
};
// clang-format on

void appreply_nok(struct frame_header hdr);
void appreply(struct frame_header hdr, enum appcmd rspcode, void *buf);

#endif
