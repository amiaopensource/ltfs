/*
 *************************************************************************************
 *
 *  unltfs.c - "unformat" an LTFS volume
 *
 *  Licensed Materials - Property of HP
 *  
 *  (C) Copyright 2015 Hewlett Packard Enterprise Development LP.
 *
 * Portions copyrighted by OSR provided to Hewlett Packard under
 * license.
 *
 *************************************************************************************
 *
 * Copyright (C) 2012 OSR Open Systems Resources, Inc.
 * 
 ************************************************************************************* 
 */

#ifdef HP_mingw_BUILD
#include "libltfs/arch/win/win_util.h"
#endif
 
#include <getopt.h>
#include "../libltfs/ltfs.h"
#include "../libltfs/pathname.h"
#include "../libltfs/plugin.h"
#include "../libltfs/tape.h"

#ifdef __APPLE__
#include "libltfs/arch/osx/osx_string.h"
#endif

/* 
 * OSR
 * 
 * In our MinGW environment, we dynamically link to the package 
 * data. 
 *  
 */
#if defined(mingw_PLATFORM) && !defined(HP_mingw_BUILD)
char *bin_mkltfs_dat;
#else
extern char bin_mkltfs_dat[];
#endif

struct other_format_opts {
    struct config_file *config; /* Configuration data read from the global LTFS config file */
    char *devname;              /* Device to unformat                                       */
    char *backend_path;         /* Path to backend shared library                           */
    bool quiet;                 /* Quiet mode indicator                                     */
    bool trace;                 /* Debug mode indicator                                     */
    bool fulltrace;             /* Full trace mode indicator                                */
    bool noask;                 /* Don't bother asking before starting to write             */
    bool eject;                 /* Eject on successful completion                           */
};

/* Forward declarations */
int unformat_tape(struct other_format_opts *opt);
int _unltfs_validate_options(char *prg_name, struct other_format_opts *opt);

/* Command line options */
static const char *short_options = "i:b:d:eqtxyh";
static struct option long_options[] = {
	{"config",          1, 0, 'i'},
	{"backend",         1, 0, 'b'},
	{"device",          1, 0, 'd'},
	{"eject",           0, 0, 'e'},
	{"quiet",           0, 0, 'q'},
	{"trace",           0, 0, 't'},
	{"fulltrace",       0, 0, 'x'},
	{"justdoit",        0, 0, 'y'},
	{"help",            0, 0, 'h'},
	{0, 0, 0, 0}
};

void show_usage(char *appname, struct config_file *config, bool full)
{
	struct libltfs_plugin backend;
	const char *default_backend;
	char *devname = NULL;

	default_backend = config_file_get_default_plugin("driver", config);
	if (default_backend && plugin_load(&backend, "driver", default_backend, config) == 0) {
		devname = strdup(ltfs_default_device_name(backend.ops));
		plugin_unload(&backend);
	}

	if (! devname)
		devname = strdup("<devname>");

	fprintf(stderr, "\n");
	fprintf(stderr, "Usage: %s <options>\n", appname);

	fprintf(stderr, "\nwhere:\n");
	fprintf(stderr, "\t-d, --device=<name> specifies the tape drive to use\n");
	fprintf(stderr, "\t-y, --justdoit      omits normal verification steps, reformats without further prompting\n");
	fprintf(stderr, "\t-e  --eject         eject tape after operation completes successfully\n");
	fprintf(stderr, "\t-q, --quiet         suppresses all progress output\n");
	fprintf(stderr, "\t-t, --trace         displays detailed progress\n");
	fprintf(stderr, "\t-h, --help          shows this help\n");
	fprintf(stderr, "\t-i, --config=<file> overrides the default config file\n");
	fprintf(stderr, "\t-b, --backend       specifies a different tape backend subsystem\n");
	fprintf(stderr, "\t-x, --fulltrace     displays debug information (verbose)\n");
	
	fprintf(stderr, "\n");
	free(devname);
}


int main(int argc, char **argv)
{
	struct other_format_opts opt;
	int ret, log_level;
	char cmd_input = 0;
	char *lang = NULL;
	const char *config_file = NULL;
	void *message_handle;

#ifdef HP_mingw_BUILD
	(void) lang;
#endif /* HP_mingw_BUILD */

#ifndef HP_mingw_BUILD
	/* Check for LANG variable and set it to en_US.UTF-8 if it is unset. */
	lang = getenv("LANG");
	if (! lang) {
		fprintf(stderr, "LTFS9015W Setting the locale to 'en_US.UTF-8'. If this is wrong, please set the LANG environment variable before starting unltfs.\n");
		ret = setenv("LANG", "en_US.UTF-8", 1);
		if (ret) {
			fprintf(stderr, "LTFS9016E Cannot set the LANG environment variable\n");
			return 1;
		}
	}
#endif /* HP_mingw_BUILD */

	/* Start up libltfs with the default logging level. */
	ret = ltfs_init(LTFS_INFO, true, false);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10000E", ret);
		return 1;
	}

	/* Register messages with libltfs */
	ret = ltfsprintf_load_plugin("bin_mkltfs", bin_mkltfs_dat, &message_handle);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10012E", ret);
		return 1;
	}

	/* Set up empty format options and load the configuration file. */
	memset(&opt, 0, sizeof(struct other_format_opts));
	opt.quiet = false;
	opt.noask = false;
	opt.eject = false;

	/* Check for a config file path given on the command line */
	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, short_options, long_options, &option_index);
		if (c == -1)
			break;
		if (c == 'i') {
			config_file = strdup(optarg);
			break;
		}
	}

	/* Load configuration file */
	ret = config_file_load(config_file, &opt.config);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10008E", ret);
		return 1;
	}

	/* Parse all command line arguments */
	optind = 1;
	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, short_options, long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			break;
		case 'e':
			opt.eject = true;
			break;
		case 'b':
			free(opt.backend_path);
			opt.backend_path = strdup(optarg);
			break;
		case 'd':
			opt.devname = strdup(optarg);
			break;
		case 'q':
			opt.quiet = true;
			break;
		case 't':
			opt.trace = true;
			break;
		case 'x':
			opt.fulltrace = true;
			break;
		case 'y':
			opt.noask = true;
			break;
		case 'h':
			show_usage(argv[0], opt.config, false);
			return 0;
		case '?':
		default:
			show_usage(argv[0], opt.config, false);
			return 1;
		}
	}

	if (optind < argc) {
		show_usage(argv[0], opt.config, false);
		return 1;
	}

	/* Pick up default backend if one wasn't specified before */
	if (! opt.backend_path) {
		const char *default_backend = config_file_get_default_plugin("driver", opt.config);
		if (! default_backend) {
			ltfsmsg(LTFS_ERR, "10009E");
			return 1;
		}
		opt.backend_path = strdup(default_backend);
	}

	/* Set the logging level */
	if (opt.quiet && (opt.trace || opt.fulltrace)) {
		ltfsmsg(LTFS_ERR, "9012E");
		show_usage(argv[0], opt.config, false);
		return 1;
	} else if (opt.quiet)
		log_level = LTFS_WARN;
	else if (opt.trace)
		log_level = LTFS_DEBUG;
	else
		log_level = LTFS_INFO;
	if (opt.fulltrace)
		log_level = LTFS_TRACE;
	ltfs_set_log_level(log_level);

	ltfsmsg(LTFS_INFO, "15495I");

	if (_unltfs_validate_options(argv[0], &opt)) {
		ltfsmsg(LTFS_ERR, "15002E");
		show_usage(argv[0], opt.config, false);
		return 1;
	}

	ret = ltfs_fs_init();
	if (ret)
		return MKLTFS_OPERATIONAL_ERROR;

	/* Statutory Warning before format/unformat */
	if (! opt.noask && !opt.quiet) {

		ltfsmsg(LTFS_INFO, "15492I");

		fflush(stdin);
		scanf("%c", &cmd_input);
		while (getchar() != '\n');

		switch (cmd_input) {
			case 'Y':
			case 'y':
				opt.noask = true;
				break;
			default:
				ltfsmsg(LTFS_INFO, "15493I");
				ret = 1;
				goto out_close;
		}
	}
	ret = unformat_tape(&opt);

out_close:
	free(opt.backend_path);
	free(opt.devname);
	config_file_free(opt.config);
	ltfsprintf_unload_plugin(message_handle);
	ltfs_finish();
	return ret;
}

int unformat_tape(struct other_format_opts *opt)
{
	int ret, ret2;
	struct libltfs_plugin backend;
	struct ltfs_volume *newvol;
	

	ret = ltfs_volume_alloc("mkltfs", &newvol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "15001E");
		return 1;
	}

	/* load the backend, open the tape device, and load a tape */
	ltfsmsg(LTFS_DEBUG, "15006D");
	ret = plugin_load(&backend, "driver", opt->backend_path, opt->config);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "15008E", opt->backend_path);
		return 1;
	}
	ret = ltfs_device_open(opt->devname, backend.ops, newvol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "15009E", opt->devname, ret);
		ret = -ENODEV;
		goto out_unload_backend;
	}
	ret = ltfs_setup_device(newvol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "15044E");
		goto out_close;
	}
	ltfsmsg(LTFS_DEBUG, "15007D");

	/* Make sure there's a tape loaded: */
	ret = tape_load_tape(newvol->device, newvol->kmi_handle);
	if (ret < 0) {
		if (ret == -LTFS_UNSUPPORTED_MEDIUM)
			ltfsmsg(LTFS_ERR, "11299E");
		else
			ltfsmsg(LTFS_ERR, "11093E", ret);
		goto out_close;
	}

	/* Make sure it's not logically write protected i.e. incompatible medium*/
	ret = tape_logically_read_only(newvol->device);
	if (ret == -LTFS_LOGICAL_WRITE_PROTECT) {
		ltfsmsg(LTFS_ERR, "11330E");
		goto out_close;
	}

	/* Make sure it's not Write-Protected: */
	ret = tape_read_only(newvol->device, 0);
	if (ret == -LTFS_WRITE_PROTECT) {
		ltfsmsg(LTFS_ERR, "11095E");
		goto out_close;
	}

	/* Donot unformat if tape is already unformated */
	if (! opt->noask) {
		ret = tape_check_unformat_ok(newvol->device);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "15494E");
			ret = 1;
			goto out_close;
		}
	}

	if (! opt->quiet) {
		ltfsmsg(LTFS_INFO, "15064I", opt->devname);
	}

	/* Here we go: do the deed: */
	if (! opt->quiet) {
		ltfsmsg(LTFS_INFO, "15065I");
	}

	ret = tape_unformat (newvol->device);

	/* If it worked, and we're configured to eject afterwards, this is a good time to do it: */
	if ((ret == 0) && (opt->eject == true)) {
		ret = tape_unload_tape (newvol->device);
	}

	/* close the tape device and unload the backend */
	ltfsmsg(LTFS_DEBUG, "15020D");

	out_close:
	ltfs_device_close(newvol);

	out_unload_backend:
	ret2 = plugin_unload(&backend);
	if (ret2 < 0) {
		ltfsmsg(LTFS_WARN, "15021W");
	}

	ltfs_volume_free (&newvol);

	if (ret < 0) {
		if (ret == -EROFS) {
			ltfsmsg(LTFS_ERR, "15063E");
			return 3;

		} else if (ret == -ENODEV) {
			ltfsmsg(LTFS_ERR, "15499E");
			return 4;

		} else {
			ltfsmsg(LTFS_ERR, "15498E");
			return 1;
		}

	} else if (ret == 1) {
		ltfsmsg(LTFS_INFO, "15493I");
		return 2;

	} else {
		ltfsmsg(LTFS_INFO, "15497I");
		return 0;
	}
}

int _unltfs_validate_options(char *prg_name, struct other_format_opts *opt)
{
	ltfsmsg(LTFS_DEBUG, "15025D");

	if (!opt->devname) {
		ltfsmsg(LTFS_ERR, "15026E", "-d");
		return 1;
	}

	ltfsmsg(LTFS_DEBUG, "15037D");
	return 0;
}
