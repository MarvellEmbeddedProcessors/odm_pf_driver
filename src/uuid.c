/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

#include "uuid.h"

uint8_t uuid_gbl[UUID_LEN];

static void
uuid_pack(const struct uuid_t *uu, uint8_t *uuid_gbl)
{
	uint8_t *out = uuid_gbl;
	uint32_t tmp;

	tmp = uu->time_low;
	out[3] = (uint8_t) tmp;
	tmp >>= 8;
	out[2] = (uint8_t) tmp;
	tmp >>= 8;
	out[1] = (uint8_t) tmp;
	tmp >>= 8;
	out[0] = (uint8_t) tmp;

	tmp = uu->time_mid;
	out[5] = (uint8_t) tmp;
	tmp >>= 8;
	out[4] = (uint8_t) tmp;

	tmp = uu->time_hi_and_version;
	out[7] = (uint8_t) tmp;
	tmp >>= 8;
	out[6] = (uint8_t) tmp;

	tmp = uu->clock_seq;
	out[9] = (uint8_t) tmp;
	tmp >>= 8;
	out[8] = (uint8_t) tmp;

	memcpy(out+10, uu->node, 6);
}

static void
uuid_unpack(const uint8_t *ptr, struct uuid_t *uu)
{
	uint32_t tmp;

	tmp = *ptr++;
	tmp = (tmp << 8) | *ptr++;
	tmp = (tmp << 8) | *ptr++;
	tmp = (tmp << 8) | *ptr++;
	uu->time_low = tmp;

	tmp = *ptr++;
	tmp = (tmp << 8) | *ptr++;
	uu->time_mid = tmp;

	tmp = *ptr++;
	tmp = (tmp << 8) | *ptr++;
	uu->time_hi_and_version = tmp;

	tmp = *ptr++;
	tmp = (tmp << 8) | *ptr++;
	uu->clock_seq = tmp;

	memcpy(uu->node, ptr, 6);
}

bool
uuid_is_null(const uint8_t *uu)
{
	int i;

	for (i = 0; i < 16; i++) {
		if (*uu++)
			return false;
	}
	return true;
}

int
parse_uuid(const char *in, uint8_t *uuid_gbl)
{
	struct uuid_t uuid;
	const char *cp;
	char buf[3];
	int i;

	if (strlen(in) != 36)
		return -1;

	for (i = 0, cp = in; i <= 36; i++, cp++) {
		if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
			if (*cp == '-')
				continue;
			else
				return -1;
		}
		if (i == 36) {
			if (*cp == 0)
				continue;
		}
		if (!isxdigit(*cp))
			return -1;
	}

	uuid.time_low = strtoul(in, NULL, 16);
	uuid.time_mid = strtoul(in + 9, NULL, 16);
	uuid.time_hi_and_version = strtoul(in + 14, NULL, 16);
	uuid.clock_seq = strtoul(in + 19, NULL, 16);
	cp = in + 24;
	buf[2] = 0;

	for (i = 0; i < 6; i++) {
		buf[0] = *cp++;
		buf[1] = *cp++;
		uuid.node[i] = strtoul(buf, NULL, 16);
	}

	uuid_pack(&uuid, uuid_gbl);
	return 0;
}

void
uuid_unparse(const uint8_t *uu, char *out, size_t len)
{
	struct uuid_t uuid;

	uuid_unpack(uu, &uuid);

	snprintf(out, len,
		 "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid.time_low, uuid.time_mid, uuid.time_hi_and_version,
		uuid.clock_seq >> 8, uuid.clock_seq & 0xFF,
		uuid.node[0], uuid.node[1], uuid.node[2],
		uuid.node[3], uuid.node[4], uuid.node[5]);
}
