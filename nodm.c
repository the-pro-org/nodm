/*
 * Copyright 1989 - 1994, Julianne Frances Haugh
 * Copyright 2009, Enrico Zini
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Julianne F. Haugh nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JULIE HAUGH AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL JULIE HAUGH OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/* Some parts substantially derived from an ancestor of: */
/* su for GNU.  Run a shell with substitute user and group IDs.
   Copyright (C) 1992-2003 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */


#define NAME "nodm"
#ifndef NODM_SESSION
#define NODM_SESSION "/usr/sbin/nodm"
#endif
/* #define DEBUG_NODM */


#include "config.h"
#include "common.h"
#include "session.h"
#include "log.h"
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/vt.h>


#define _(...) (__VA_ARGS__)

/*
 * Assorted #defines to control su's behavior
 */
/*
 * Global variables
 */

/* Program name used in error messages */
static char *Prog;

/*
 * External identifiers
 */

extern char **newenvp;
extern char **environ;
extern size_t newenvc;

/* local function prototypes */

static char *Basename (char *str)
{
	char *cp = strrchr (str, '/');

	return cp ? cp + 1 : str;
}


static int try_vtstate(const char* dev, struct vt_stat* vtstat)
{
	int res = 0;
	int fd = open(dev, O_WRONLY | O_NOCTTY, 0);
	if (fd < 0)
		goto cleanup;
	if (ioctl (fd, VT_GETSTATE, vtstat) < 0)
		goto cleanup;
	res = 1;

cleanup:
	if (fd >= 0) close(fd);
	return res;
}

static int get_vtstate(struct vt_stat* vtstat)
{
	if (try_vtstate("/dev/tty", vtstat)) return 1;
	if (try_vtstate("/dev/tty0", vtstat)) return 1;
	if (try_vtstate("/dev/console", vtstat)) return 1;
	return 0;
}

/*
 * Allocate a new vt, open it and return the file descriptor and the vt number.
 *
 * Searching the vt starts at the initial value of vtnum.
 */
int open_vt(int *vtnum)
{
	char vtname[15];
	int res = -1;
	struct vt_stat vtstat;
	unsigned short vtmask;

	if (!get_vtstate(&vtstat))
	{
		fprintf (stderr, _("%s: cannot find or open the console\n"), Prog);
		goto cleanup;
	}

	for (vtmask = 1 << *vtnum; vtstat.v_state & vtmask; ++*vtnum, vtmask <<= 1)
		;
	if (!vtmask) {
		fprintf (stderr, _("%s: all VTs seem to be busy\n"), Prog);
		goto cleanup;
	}

	snprintf(vtname, 15, "/dev/tty%d", *vtnum);

	res = open(vtname, O_RDWR | O_NOCTTY, 0);
	if (res < 0) {
		fprintf (stderr, _("%s: cannot open %s: %m\n"), Prog, vtname);
		goto cleanup;
	}

cleanup:
	return res;
}

/*
 * Run the X session
 *
 * @param xsession
 *   The path to the X session
 * @param xoptions
 *   X options (can be NULL if no options are to be passed)
 * @param mst
 *   The minimum time (in seconds) that a session should last to be considered
 *   successful
 */
void run_and_restart(const char* xsession, const char* xoptions, int mst)
{
	static int retry_times[] = { 0, 0, 30, 30, 60, 60, -1 };
	int restart_count = 0;
    /*
	char command[BUFSIZ];
	const char* args[4];

	if (xoptions != NULL)
		snprintf(command, BUFSIZ, "exec %s %s -- %s", xinit, xsession, xoptions);
	else
		snprintf(command, BUFSIZ, "exec %s %s", xinit, xsession);
	command[BUFSIZ-1] = 0;

	args[0] = "/bin/sh";
	args[1] = "-c";
	args[2] = command;
	args[3] = 0;
    */

	while (1)
	{
		/* Run the X server */
		time_t begin = time(NULL);
		time_t end;
        int status = nodm_x_with_session_cmdline(xoptions);
        log_info("X session exited with status %d", status);
		end = time(NULL);

		/* Check if the session was too short */
		if (end - begin < mst)
		{
            log_warn("session was shorter than %d seconds: possible problems", mst);
			if (retry_times[restart_count+1] != -1)
				++restart_count;
		}
		else
			restart_count = 0;

		/* Sleep a bit if the session was too short */
		sleep(retry_times[restart_count]);
        log_info("restarting session");
	}
}

/*
 * Copy from the environment the value of $name into dest.
 *
 * If $name is not in the environment, use def.
 *
 * @param destination buffer, should be at least BUFSIZ long
 * @param name name of the environment variable to look up
 * @param def default value to use if $name is not found
 */
static void string_from_env(char* dest, const char* name, const char* def)
{
	char* cp = getenv(name);
	if (cp != NULL)
		strncpy(dest, cp, BUFSIZ-1);
	else
		strncpy(dest, def, BUFSIZ-1);
	dest[BUFSIZ-1] = 0;
}

static void monitor_cmdline_help(int argc, char** argv, FILE* out)
{
	fprintf(out, "Usage: %s [options]\n\n", argv[0]);
	fprintf(out, "Options:\n");
	fprintf(out, " --help         print this help message\n");
	fprintf(out, " --version      print %s's version number\n", NAME);
	fprintf(out, " --session=cmd  run cmd instead of %s\n", NODM_SESSION);
	fprintf(out, "                (use for testing)\n");
}

/*
 * Start the monitor, that will continue to rerun xinit with appropriate delays
 */
static int nodm_monitor(int argc, char **argv)
{
	static int opt_help = 0;
	static int opt_version = 0;
	static struct option options[] =
	{
		/* These options set a flag. */
		{"help",    no_argument,       &opt_help, 1},
		{"version", no_argument,       &opt_version, 1},
		{"session", required_argument, 0, 's'},
		{0, 0, 0, 0}
	};
	const char* opt_session = NODM_SESSION;
	char xoptions[BUFSIZ];
	char xoptions1[BUFSIZ];
	char* cp;
	int mst;
	int vt_fd = -1;
	int vt_num;

	/* Parse command line options */
	while (1)
	{
		int option_index = 0;
		int c = getopt_long(argc, argv, "s:", options, &option_index);
		if (c == -1) break;
		switch (c)
		{
			case 0: break;
			case 's': opt_session = optarg; break;
			default:
				  fprintf(stderr, "Invalid command line option\n");
				  monitor_cmdline_help(argc, argv, stderr);
				  return 1;
		}
	}
	if (opt_help)
	{
		monitor_cmdline_help(argc, argv, stdout);
		return 0;
	}
	if (opt_version)
	{
		printf("%s version %s\n", NAME, VERSION);
		return 0;
	}

	/* We only run if we are root */
	if (getuid() != 0)
	{
		fprintf (stderr, _("%s: can only be run by root\n"), Prog);
		return E_NOPERM;
	}

	log_info("starting nodm monitor");

	{
		char startvt[BUFSIZ];
		string_from_env(startvt, "NODM_FIRST_VT", "7");
		vt_num = strtoul(startvt, NULL, 10);
	}
	if ((vt_fd = open_vt(&vt_num)) == -1)
	{
		fprintf (stderr, _("%s: cannot allocate a virtual terminal\n"), Prog);
		return 1;
	}

	/* Read the configuration from the environment */
	cp = getenv("NODM_MIN_SESSION_TIME");
	mst = cp ? atoi(cp) : 60;
	string_from_env(xoptions, "NODM_X_OPTIONS", "");

	if (xoptions[0] == 0)
		snprintf(xoptions1, BUFSIZ, "vt%d", vt_num);
	else
		snprintf(xoptions1, BUFSIZ, "vt%d %s", vt_num, xoptions);

	setenv("NODM_RUN_SESSION", "1", 1);
	run_and_restart(opt_session, xoptions1, mst);

	close(vt_fd);

	return 0;
}


/*
 * nodm - start X with autologin to a given user
 *
 * First, X is started as root, and nodm itself is used as the session.
 *
 * When run as the session, nodm performs a proper login to a given user and
 * starts the X session.
 */
int main (int argc, char **argv)
{
	/*
	 * Get the program name. The program name is used as a prefix to
	 * most error messages.
	 */
	Prog = Basename (argv[0]);

    // TODO: move these after command line parsing, so we can implement
    // --verbose --no-syslog --no-stderr and the like
    struct log_config cfg = {
        .program_name = Prog,
        .log_to_syslog = true,
        .log_to_stderr = true,
        .info_to_stderr = false,
    };
    log_start(&cfg);

    /*
     * Process the command line arguments. 
     */

    int ret = nodm_monitor(argc, argv);

    log_end();
    return ret;
}
