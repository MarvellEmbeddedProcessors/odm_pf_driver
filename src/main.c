/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"
#include "odm_pf.h"
#include "odm_pf_selftest.h"
#include "pmem.h"
#include "uuid.h"
#include "vfio_pci.h"

static volatile sig_atomic_t quit_signal;

enum {
	OPT_LONG_MIN_NUM = 256,
	OPT_VFIO_VF_TOKEN_NUM,
	OPT_NUM_VFS,
	OPT_LONG_MAX_NUM
};

const struct option long_options[] = {
	{"vfio-vf-token",     1, NULL, OPT_VFIO_VF_TOKEN_NUM},
	{"num_vfs",           1, NULL, OPT_NUM_VFS},
	{0,                   0, NULL, 0                    }
};

void
signal_handler(int sig_num)
{
	if (sig_num == SIGTERM) {
		log_write(LOG_WARNING, "Received SIGTERM, exiting...\n");
		quit_signal = 1;
	}
}

void
print_usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [-c] [-l log_level] [-s] [-e eng_sel] --vfio-vf-token uuid\n"
		"--num_vfs n\n", prog_name);
	fprintf(stderr, "  -c             Enable console logging (default disabled)\n");
	fprintf(stderr, "  -l log_level   Set global log level (0-7) (default LOG_INFO)\n");
	fprintf(stderr, "  -s             Run self test\n");
	fprintf(stderr, "  --vfio-vf-token uuid  Randomly generated vf token to be used by both PF"
		"and VF\n");
	fprintf(stderr, "  -e eng_sel     Set the internal DMA engine to queue mapping\n");
	fprintf(stderr, "  --num_vfs n    Create n number of VFs. Valid values are: 2,4,8,16"
		"Default value is 4\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	bool do_self_test = false, console_logging_enabled = false;
	struct odm_dev_config dev_cfg;
	struct odm_dev *odm_pf;
	int log_lvl = LOG_INFO;
	int option_index;
	int opt, rc = 0;
	char **argvopt;
	int num_vfs;

	/* Initialize the config with default values */
	dev_cfg.eng_sel = 0xAAAAAAAA;
	dev_cfg.num_vfs = 4;

	argvopt = argv;
	while ((opt = getopt_long(argc, argvopt, "csl:e:",
				  long_options, &option_index)) != EOF) {
		switch (opt) {
		case 'c':
			console_logging_enabled = true;
			break;
		case 'l':
			log_lvl = atoi(optarg);
			if (log_lvl < 0 || log_lvl > 7) {
				fprintf(stderr, "Invalid log level: %d\n", log_lvl);
				print_usage(argv[0]);
			}
			break;
		case 's':
			do_self_test = true;
			break;
		case OPT_VFIO_VF_TOKEN_NUM:
			if (parse_uuid(optarg, dev_cfg.uuid_gbl) < 0) {
				fprintf(stderr, "invalid parameters for --vfio-vf-token");
				print_usage(argv[0]);
			}
			break;
		case 'e':
			dev_cfg.eng_sel = strtoul(optarg, NULL, 16);
			break;
		case OPT_NUM_VFS:
			num_vfs = atoi(optarg);
			if (num_vfs > ODM_MAX_VFS || (num_vfs & (num_vfs - 1))) {
				fprintf(stderr, "Invalid number of VFs: %d\n", num_vfs);
				print_usage(argv[0]);
			}
			dev_cfg.num_vfs = num_vfs;
			break;
		default:
			print_usage(argv[0]);
		}
	}

	log_init("odm_pf", log_lvl, console_logging_enabled);

	if (do_self_test)
		odm_pf_selftest(&dev_cfg);

	odm_pf = odm_pf_probe(&dev_cfg);
	if (!odm_pf) {
		log_write(LOG_ERR, "Failed to probe ODM PF\n");
		rc = -1;
		goto exit;
	}

	signal(SIGTERM, signal_handler);

	while (!quit_signal)
		sleep(10);

exit:
	odm_pf_release(odm_pf);
	log_fini();

	return rc;
}
