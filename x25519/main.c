// Copyright (C) 2022, 2023 - Tillitis AB
// Copyright (C) 2023 - Daniel Lublin
// SPDX-License-Identifier: GPL-2.0-only

#include <monocypher/monocypher.h>
#include <tkey/blake2s.h>
#include <tkey/lib.h>
#include <tkey/qemu_debug.h>
#include <tkey/tk1_mem.h>

#include "app_proto.h"

// clang-format off
static volatile uint32_t *cpu_mon_first =   (volatile uint32_t *)TK1_MMIO_TK1_CPU_MON_FIRST;
static volatile uint32_t *cpu_mon_last =    (volatile uint32_t *)TK1_MMIO_TK1_CPU_MON_LAST;
static volatile uint32_t *cpu_mon_ctrl =    (volatile uint32_t *)TK1_MMIO_TK1_CPU_MON_CTRL;
static volatile uint32_t *app_addr =        (volatile uint32_t *)TK1_MMIO_TK1_APP_ADDR;
static volatile uint32_t *app_size =        (volatile uint32_t *)TK1_MMIO_TK1_APP_SIZE;
static volatile uint32_t *const cdi =             (volatile uint32_t *)TK1_MMIO_TK1_CDI_FIRST;
static volatile uint32_t *const led =             (volatile uint32_t *)TK1_MMIO_TK1_LED;
static volatile uint32_t *const touch_status =    (volatile uint32_t *)TK1_MMIO_TOUCH_STATUS;
static volatile uint32_t *const timer_timer =     (volatile uint32_t *)TK1_MMIO_TIMER_TIMER;
static volatile uint32_t *const timer_prescaler = (volatile uint32_t *)TK1_MMIO_TIMER_PRESCALER;
static volatile uint32_t *const timer_status =    (volatile uint32_t *)TK1_MMIO_TIMER_STATUS;
static volatile uint32_t *const timer_ctrl =      (volatile uint32_t *)TK1_MMIO_TIMER_CTRL;

#define LED_BLACK 0
#define LED_RED   (1 << TK1_MMIO_TK1_LED_R_BIT)
#define LED_GREEN (1 << TK1_MMIO_TK1_LED_G_BIT)
#define LED_BLUE  (1 << TK1_MMIO_TK1_LED_B_BIT)
// clang-format on

const uint8_t app_name0[4] = "x255";
const uint8_t app_name1[4] = "19  ";
const uint32_t app_version = 0x00000002;

#define LED_COLOR (LED_RED | LED_GREEN) // yellow
#define TOUCH_TIMEOUT_SECS 10

#define STATUS_OK 0
#define STATUS_WRONG_CMDLEN 1
#define STATUS_TOUCH_TIMEOUT 2

#define CDI_WORDS 8
#define CDI_LEN (CDI_WORDS * 4)
#define DOMAIN_LEN 32
#define USER_SECRET_LEN 32
#define REQUIRE_TOUCH_LEN 1
#define SECRET_INPUT_LEN                                                       \
	(CDI_LEN + DOMAIN_LEN + USER_SECRET_LEN + REQUIRE_TOUCH_LEN)
#define SECRET_LEN 32

void make_secret(uint8_t secret[SECRET_LEN], const uint8_t domain[DOMAIN_LEN],
		 const uint8_t user_secret[USER_SECRET_LEN],
		 const uint8_t require_touch[REQUIRE_TOUCH_LEN])
{
	uint8_t input[SECRET_INPUT_LEN] = {0};
	uint32_t local_cdi[CDI_WORDS] = {0};

	// must read out CDI word (32-bit) by word
	wordcpy(local_cdi, (void *)cdi, CDI_WORDS);

	uint8_t offset = 0;
	memcpy(input + offset, local_cdi, CDI_LEN);
	offset += CDI_LEN;
	memcpy(input + offset, domain, DOMAIN_LEN);
	offset += DOMAIN_LEN;
	memcpy(input + offset, user_secret, USER_SECRET_LEN);
	offset += USER_SECRET_LEN;
	memcpy(input + offset, require_touch, REQUIRE_TOUCH_LEN);

	blake2s_ctx b2s_ctx = {0};
	// Call the shim which in turn calls the blake2s impl that
	// resides in TKey firmware (see libcommon/lib.c in
	// tkey-libs). It returns non-zero only if (outlen == 0 ||
	// outlen > 32 || keylen > 32), which we rightly ignore.
	//
	// We're not using keyed hashing; input is the concatenation of our
	// fixed-length parameters.
	blake2s(secret, SECRET_LEN, NULL, 0, input, SECRET_INPUT_LEN, &b2s_ctx);
	memset(input, 0, SECRET_INPUT_LEN);
}

int wait_touched(void)
{
	// make sure timer is stopped
	*timer_ctrl = (1 << TK1_MMIO_TIMER_CTRL_STOP_BIT);
	// match 18 MHz TKey device clock, giving us timer in seconds
	*timer_prescaler = 18 * 1000 * 1000;
	*timer_timer = TOUCH_TIMEOUT_SECS;
	// start the timer
	*timer_ctrl = (1 << TK1_MMIO_TIMER_CTRL_START_BIT);

	// first a write, confirming any stray touch
	*touch_status = 0;

	const int loopcount = 300 * 1000;
	int led_on = 1;
	for (;;) {
		led_on = !led_on;
		*led = led_on ? LED_COLOR : LED_BLACK;
		for (int i = 0; i < loopcount; i++) {
			if ((*timer_status &
			     (1 << TK1_MMIO_TIMER_STATUS_RUNNING_BIT)) == 0) {
				// timed out before touched
				*led = LED_BLACK;
				return 0;
			}
			if (*touch_status &
			    (1 << TK1_MMIO_TOUCH_STATUS_EVENT_BIT)) {
				// confirm we read the touch event
				*touch_status = 0;
				*led = LED_BLACK;
				return 1;
			}
		}
	}
}

int main(void)
{
	uint8_t hdr_byte = 0;
	struct frame_header hdr = {0};
	uint8_t cmd[CMDLEN_MAXBYTES] = {0};
	uint8_t rsp[CMDLEN_MAXBYTES] = {0};

	// stop cpu from executing code on our stack (all RAM above app)
	*cpu_mon_first = *app_addr + *app_size;
	*cpu_mon_last = TK1_RAM_BASE + TK1_RAM_SIZE;
	*cpu_mon_ctrl = 1;

	for (;;) {
		*led = LED_COLOR;

		memset(&hdr, 0, sizeof hdr);
		memset(cmd, 0, CMDLEN_MAXBYTES);
		memset(rsp, 0, CMDLEN_MAXBYTES);

		hdr_byte = readbyte();
		qemu_puts("Read byte: ");
		qemu_puthex(hdr_byte);
		qemu_lf();

		if (parseframe(hdr_byte, &hdr) == -1) {
			qemu_puts("Couldn't parse header\n");
			continue;
		}

		read(cmd, hdr.len);

		if (hdr.endpoint == DST_FW) {
			appreply_nok(hdr);
			qemu_puts("Responded NOK to message meant for fw\n");
			continue;
		}

		if (hdr.endpoint != DST_SW) {
			qemu_puts("Message not meant for app. endpoint was 0x");
			qemu_puthex(hdr.endpoint);
			qemu_lf();
			continue;
		}

		switch (cmd[0]) {
		case APP_CMD_GET_PUBKEY: {
			qemu_puts("APP_CMD_GET_PUBKEY\n");

			if (hdr.len != 128) {
				rsp[0] = STATUS_WRONG_CMDLEN;
				appreply(hdr, APP_RSP_GET_PUBKEY, rsp);
				break;
			}

			uint8_t offset = 1; // skip cmd byte
			uint8_t *domain = cmd + offset;
			offset += DOMAIN_LEN;
			uint8_t *user_secret = cmd + offset;
			offset += USER_SECRET_LEN;
			uint8_t *require_touch = cmd + offset;

			uint8_t secret[SECRET_LEN] = {0};
			make_secret(secret, domain, user_secret, require_touch);

			rsp[0] = STATUS_OK;
			crypto_x25519_public_key(rsp + 1, secret);
			memset(secret, 0, SECRET_LEN);
			appreply(hdr, APP_RSP_GET_PUBKEY, rsp);
			break;
		}

		case APP_CMD_DO_ECDH: {
			qemu_puts("APP_CMD_DO_ECDH\n");

			if (hdr.len != 128) {
				rsp[0] = STATUS_WRONG_CMDLEN;
				appreply(hdr, APP_RSP_DO_ECDH, rsp);
				break;
			}

			uint8_t offset = 1; // skip cmd byte
			uint8_t *domain = cmd + offset;
			offset += DOMAIN_LEN;
			uint8_t *user_secret = cmd + offset;
			offset += USER_SECRET_LEN;
			uint8_t *require_touch = cmd + offset;
			offset += REQUIRE_TOUCH_LEN;
			uint8_t *their_pubkey = cmd + offset;

			if (*require_touch && (wait_touched() == 0)) {
				rsp[0] = STATUS_TOUCH_TIMEOUT;
				appreply(hdr, APP_RSP_DO_ECDH, rsp);
				break;
			}

			uint8_t secret[SECRET_LEN] = {0};
			make_secret(secret, domain, user_secret, require_touch);

			rsp[0] = STATUS_OK;
			crypto_x25519(rsp + 1, secret, their_pubkey);
			memset(secret, 0, SECRET_LEN);
			appreply(hdr, APP_RSP_DO_ECDH, rsp);
			break;
		}

		case APP_CMD_GET_NAMEVERSION: {
			qemu_puts("APP_CMD_GET_NAMEVERSION\n");

			if (hdr.len != 1) {
				// returning all zeroes if wrong cmdlen
				appreply(hdr, APP_RSP_GET_NAMEVERSION, rsp);
				break;
			}

			memcpy(rsp + 0, app_name0, 4);
			memcpy(rsp + 4, app_name1, 4);
			memcpy(rsp + 8, &app_version, 4);
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
