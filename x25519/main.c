// Copyright (C) 2022, 2023 - Tillitis AB
// Copyright (C) 2023 - Daniel Lublin
// SPDX-License-Identifier: GPL-2.0-only

#include <lib.h>
#include <monocypher/monocypher.h>
#include <tk1_mem.h>

#include "app_proto.h"

// clang-format off
static volatile uint32_t *cdi =   (volatile uint32_t *)TK1_MMIO_TK1_CDI_FIRST;
static volatile uint32_t *led =   (volatile uint32_t *)TK1_MMIO_TK1_LED;
static volatile uint32_t *touch = (volatile uint32_t *)TK1_MMIO_TOUCH_STATUS;
static volatile uint32_t *cpu_mon_ctrl  = (volatile uint32_t *) TK1_MMIO_TK1_CPU_MON_CTRL;
static volatile uint32_t *cpu_mon_first = (volatile uint32_t *) TK1_MMIO_TK1_CPU_MON_FIRST;
static volatile uint32_t *cpu_mon_last  = (volatile uint32_t *) TK1_MMIO_TK1_CPU_MON_LAST;

#define LED_BLACK 0
#define LED_RED   (1 << TK1_MMIO_TK1_LED_R_BIT)
#define LED_GREEN (1 << TK1_MMIO_TK1_LED_G_BIT)
#define LED_BLUE  (1 << TK1_MMIO_TK1_LED_B_BIT)
// clang-format on

const uint8_t app_name0[4] = "x255";
const uint8_t app_name1[4] = "19  ";
const uint32_t app_version = 0x00000001;

// lengths of parameters from client on host
#define DOMAIN_LEN 78
#define USER_SECRET_LEN 16
#define REQUIRE_TOUCH_LEN 1

// 8 words * 4 bytes == 32 bytes
#define CDI_WORDS 8
// total 127 bytes
#define SECRET_INPUT_LEN                                                       \
	DOMAIN_LEN + USER_SECRET_LEN + REQUIRE_TOUCH_LEN + CDI_WORDS * 4

void make_secret(uint8_t *output, uint8_t *domain, uint8_t *user_secret,
		 uint8_t require_touch)
{
	uint8_t input[SECRET_INPUT_LEN] = {0};
	uint32_t local_cdi[CDI_WORDS] = {0};

	memcpy(input, domain, DOMAIN_LEN);
	memcpy(input + DOMAIN_LEN, user_secret, USER_SECRET_LEN);
	input[DOMAIN_LEN + USER_SECRET_LEN] = require_touch;

	wordcpy(local_cdi, (void *)cdi, CDI_WORDS);
	memcpy(input + DOMAIN_LEN + USER_SECRET_LEN + REQUIRE_TOUCH_LEN,
	       local_cdi, CDI_WORDS * 4);

	blake2s_ctx b2s_ctx;
	blake2s(output, 32, NULL, 0, input, SECRET_INPUT_LEN, &b2s_ctx);
}

void wait_touch_ledflash(int ledvalue, int loopcount)
{
	int led_on = 0;
	// first a write, to ensure no stray touch
	*touch = 0;
	for (;;) {
		*led = led_on ? ledvalue : 0;
		for (int i = 0; i < loopcount; i++) {
			if (*touch & (1 << TK1_MMIO_TOUCH_STATUS_EVENT_BIT)) {
				// write, confirming we read the touch event
				*touch = 0;
				return;
			}
		}
		led_on = !led_on;
	}
}

int main(void)
{
	struct frame_header hdr; // Used in both directions
	uint8_t cmd[CMDLEN_MAXBYTES];
	uint8_t rsp[CMDLEN_MAXBYTES];
	uint8_t in;

	// Use Execution Monitor on RAM after app
	*cpu_mon_first = TK1_MMIO_TK1_APP_ADDR + TK1_MMIO_TK1_APP_SIZE;
	*cpu_mon_last = TK1_RAM_BASE + TK1_RAM_SIZE;
	*cpu_mon_ctrl = 1;

	for (;;) {
		*led = LED_GREEN | LED_BLUE;
		in = readbyte();
		qemu_puts("Read byte: ");
		qemu_puthex(in);
		qemu_lf();

		if (parseframe(in, &hdr) == -1) {
			qemu_puts("Couldn't parse header\n");
			continue;
		}

		memset(cmd, 0, CMDLEN_MAXBYTES);
		// Read app command, blocking
		read(cmd, hdr.len);

		if (hdr.endpoint == DST_FW) {
			appreply_nok(hdr);
			qemu_puts("Responded NOK to message meant for fw\n");
			continue;
		}

		// Is it for us?
		if (hdr.endpoint != DST_SW) {
			qemu_puts("Message not meant for app. endpoint was 0x");
			qemu_puthex(hdr.endpoint);
			qemu_lf();
			continue;
		}

		// Reset response buffer
		memset(rsp, 0, CMDLEN_MAXBYTES);

		// Min length is 1 byte so this should always be here
		switch (cmd[0]) {
		case APP_CMD_GET_PUBKEY: {
			qemu_puts("APP_CMD_GET_PUBKEY\n");
			if (hdr.len != 128) {
				rsp[0] = STATUS_BAD;
				appreply(hdr, APP_RSP_GET_PUBKEY, rsp);
				break;
			}
			uint8_t *data = cmd + 1;

			uint8_t secret[32] = {0};
			// output, domain, user_secret, require_touch
			make_secret(secret, data, data + DOMAIN_LEN,
				    data[DOMAIN_LEN + USER_SECRET_LEN]);

			rsp[0] = STATUS_OK;
			crypto_x25519_public_key(rsp + 1, secret);

			appreply(hdr, APP_RSP_GET_PUBKEY, rsp);
			break;
		}

		case APP_CMD_COMPUTE_SHARED: {
			qemu_puts("APP_CMD_COMPUTE_SHARED\n");
			if (hdr.len != 128) {
				rsp[0] = STATUS_BAD;
				appreply(hdr, APP_RSP_COMPUTE_SHARED, rsp);
				break;
			}
			uint8_t *data = cmd + 1;

			uint8_t secret[32] = {0};
			// output, domain, user_secret, require_touch
			make_secret(secret, data, data + DOMAIN_LEN,
				    data[DOMAIN_LEN + USER_SECRET_LEN]);

			if (data[DOMAIN_LEN + USER_SECRET_LEN]) {
				wait_touch_ledflash(LED_GREEN | LED_BLUE,
						    350000);
			}

			rsp[0] = STATUS_OK;
			// output, secret, their_pubkey
			crypto_x25519(rsp + 1, secret,
				      data + DOMAIN_LEN + USER_SECRET_LEN +
					  REQUIRE_TOUCH_LEN);

			appreply(hdr, APP_RSP_COMPUTE_SHARED, rsp);
			break;
		}

		case APP_CMD_GET_NAMEVERSION: {
			qemu_puts("APP_CMD_GET_NAMEVERSION\n");
			// only zeroes if unexpected cmdlen bytelen
			if (hdr.len == 1) {
				memcpy(rsp, app_name0, 4);
				memcpy(rsp + 4, app_name1, 4);
				memcpy(rsp + 8, &app_version, 4);
			}
			appreply(hdr, APP_RSP_GET_NAMEVERSION, rsp);
			break;
		}

		default:
			qemu_puts("Received unknown command: ");
			qemu_puthex(cmd[0]);
			qemu_lf();
			appreply(hdr, APP_RSP_UNKNOWN_CMD, rsp);
		}
	}
}
