/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */
#ifndef __UUID_H__
#define __UUID_H__

#include <stdbool.h>

#define UUID_LEN 16
#define UUID_STRLEN 37

/* UUID packed form */
struct uuid_t {
	uint32_t time_low;
	uint16_t time_mid;
	uint16_t time_hi_and_version;
	uint16_t clock_seq;
	uint8_t node[6];
};

bool uuid_is_null(const uint8_t *uu);
int parse_uuid(const char *in, uint8_t *uuid_gbl);
void uuid_unparse(const uint8_t *uu, char *out, size_t len);
#endif /* __UUID_H__ */
