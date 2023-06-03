/*	$OpenBSD: calendar.c,v 1.23 2004/12/10 15:31:01 mickey Exp $	*/

/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2021, 2023
 *	mirabilos <m@mirbsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef MBSDPORT_H
#include MBSDPORT_H
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <locale.h>
#ifndef USE_CUSTOM_USERSWITCH
#include <login_cap.h>
#endif
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "pathnames.h"
#include "calendar.h"

#ifndef USE_CUSTOM_USERSWITCH
#define userswitch(pw)	setusercontext(NULL, (pw), (pw)->pw_uid, \
			    LOGIN_SETALL ^ LOGIN_SETLOGIN)
#endif

__IDSTRING(pathnames_h, PATHNAMES_H);
__IDSTRING(calendar_h, CALENDAR_H);

__COPYRIGHT("Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.");
__SCCSID("@(#)calendar.c  8.3 (Berkeley) 3/25/94");
__RCSID("$MirOS: src/usr.bin/calendar/calendar.c,v 1.21 2023/06/03 21:33:37 tg Exp $");

const char *calendarFile = "calendar";  /* default calendar file */
const char calendarHome[] = ".etc/calendar"; /* $HOME */
static const char *calendarNoMail = "nomail";  /* don't sent mail if this file exists */

struct passwd *pw;
unsigned char doall = 0;
unsigned char bodun_always = 0;
unsigned char parsecvt = 0;
time_t f_time = 0;

int f_dayAfter = 0; /* days after current date */
int f_dayBefore = 0; /* days before current date */

struct specialev spev[NUMEV];

char *cppargv[NCPPARGV];

static void childsig(int);
static void addcppargv(const char *, int);

int
main(int argc, char *argv[])
{
	int ch;
	char *caldir;
	int daysAB = 0;

	(void)setlocale(LC_ALL, "");

	addcppargv("cpp", 2);
	addcppargv("-traditional", 2);
	addcppargv("-undef", 2);
	addcppargv("-U__GNUC__", 2);
#ifdef UNICODE
	addcppargv("-DUNICODE", 2);
#endif
	addcppargv("-P", 2);
	addcppargv("-I.", 2);
	addcppargv(_PATH_INCLUDE, 2);

	while ((ch = getopt(argc, argv, "A:aB:bD:f:I:Pt:-")) != -1)
		switch (ch) {
		case '-':		/* backward contemptible */
		case 'a':
			if (getuid())
				errx(1, "%s", strerror(EPERM));
			doall = 1;
			break;

		case 'b':
			bodun_always = 1;
			break;

		case 'D':
		case 'I':
			addcppargv(optarg, ch);
			break;

		case 'f': /* other calendar file */
			calendarFile = optarg;
			break;

		case 'P': /* parse/convert mode */
			++parsecvt;
			break;

		case 't': /* other date, undocumented, for tests */
			if ((f_time = Mktime(optarg)) <= 0)
				errx(1, "specified date is outside allowed range");
			break;

		case 'A': /* days after current date */
			f_dayAfter = atoi(optarg);
			daysAB = 1;
			break;

		case 'B': /* days before current date */
			f_dayBefore = atoi(optarg);
			if (f_dayBefore)
				daysAB = 1;
			break;

		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	addcppargv(calendarFile, 1);
	addcppargv(NULL, 0);

	/* use current time */
	if (f_time <= 0)
		(void)time(&f_time);

	if (f_dayBefore) {
		/* Move back in time and only look forwards */
		f_dayAfter += f_dayBefore;
		f_time -= 86400 * f_dayBefore;
		f_dayBefore = 0;
	}
	settime(daysAB);

	if (doall) {
		pid_t kid, deadkid;
		int kidstat, kidreaped, runningkids;
		int acstat;
		struct stat sbuf;
		time_t t;
		unsigned int sleeptime;

		signal(SIGCHLD, childsig);
		runningkids = 0;
		t = time(NULL);
		while ((pw = getpwent()) != NULL) {
			acstat = 0;
			/* Avoid unnecessary forks.  The calendar file is only
			 * opened as the user later; if it can't be opened,
			 * it's no big deal.  Also, get to correct directory.
			 * Note that in an NFS environment root may get EACCES
			 * on a chdir(), in which case we have to fork.  As long as
			 * we can chdir() we can stat(), unless the user is
			 * modifying permissions while this is running.
			 */
			if (chdir(pw->pw_dir)) {
				if (errno == EACCES)
					acstat = 1;
				else
					continue;
			}
			if (stat(calendarFile, &sbuf) != 0) {
				if (chdir(calendarHome)) {
					if (errno == EACCES)
						acstat = 1;
					else
						continue;
				}
				if (stat(calendarNoMail, &sbuf) == 0 ||
				    stat(calendarFile, &sbuf) != 0)
					continue;
			}
			sleeptime = USERTIMEOUT;
			switch ((kid = fork())) {
			case -1:	/* error */
				warn("fork");
				continue;
			case 0:	/* child */
				(void)setlocale(LC_ALL, "");
				if (userswitch(pw))
					err(1, "unable to set user context (uid %u)",
					    pw->pw_uid);
				caldir = NULL; /* for bitching compilers */
				if (acstat && !(caldir = strdup(pw->pw_dir)))
					err(1, NULL);
				endpwent();
				if (acstat) {
					if (chdir(caldir) ||
					    stat(calendarFile, &sbuf) != 0 ||
					    chdir(calendarHome) ||
					    stat(calendarNoMail, &sbuf) == 0 ||
					    stat(calendarFile, &sbuf) != 0)
						exit(0);
				}
				cal();
				exit(0);
			}
			/* parent: wait a reasonable time, then kill child if
			 * necessary.
			 */
			runningkids++;
			kidreaped = 0;
			do {
				sleeptime = sleep(sleeptime);
				/* Note that there is the possibility, if the sleep
				 * stops early due to some other signal, of the child
				 * terminating and not getting detected during the next
				 * sleep.  In that unlikely worst case, we just sleep
				 * too long for that user.
				 */
				for (;;) {
					deadkid = waitpid(-1, &kidstat, WNOHANG);
					if (deadkid <= 0)
						break;
					runningkids--;
					if (deadkid == kid) {
						kidreaped = 1;
						sleeptime = 0;
					}
				}
			} while (sleeptime);

			if (!kidreaped) {
				/* It doesn't _really_ matter if the kill fails, e.g.
				 * if there's only a zombie now.
				 */
				(void)kill(kid, SIGTERM);
				warnx("uid %u did not finish in time", pw->pw_uid);
			}
			if (time(NULL) - t >= 86400)
				errx(2, "'calendar -a' took more than a day; stopped at uid %u",
				    pw->pw_uid);
		}
		endpwent();
		for (;;) {
			deadkid = waitpid(-1, &kidstat, WNOHANG);
			if (deadkid <= 0)
				break;
			runningkids--;
		}
		if (runningkids)
			warnx("%d child processes still running when 'calendar -a' finished",
			    runningkids);
	} else {
		if ((caldir = getenv("CALENDAR_DIR")) != NULL &&
		    chdir(caldir))
			err(1, "chdir");
		cal();
	}

	exit(0);
}


void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: calendar [-abP] [-A num] [-B num] [-f calendarfile] "
	    "[-t [[[cc]yy][mm]]dd]\n");
	exit(1);
}


void
childsig(int signo __unused)
{
}


static void
addcppargv(const char *fn, int ch)
{
	char *what;
	size_t len;
	static size_t ncppargv = 0;

	if (ncppargv == NCPPARGV)
		errx(1, "too many cpp(1) flags");
	switch (ch) {
	case 0:
		/* append sentinel nil */
		what = NULL;
		break;
	case 1:
		/* escape calendarFile */
		if (!(fn[0] == '/' || (fn[0] == '-' && !fn[1]))) {
			/* if it is not absolute / stdin */
			len = strlen(fn) + 1U;
			if (!(what = malloc(2U + len)))
				err(1, "malloc");
			what[0] = '.';
			what[1] = '/';
			memcpy(what + 2, fn, len);
			break;
		}
		/* if it is absolute / stdin, there is no need to escape */
		/* FALLTHROUGH */
	case 2: {
		/* direct assign ignoring const */
		union {
			char *cp;
			const char *ccp;
		} p;

		p.ccp = fn;
		what = p.cp;
		break;
	}
	default:
		/* pass on flag */
		len = strlen(fn) + 1U;
		if (!(what = malloc(2U + len)))
			err(1, "malloc");
		what[0] = '-';
		what[1] = ch;
		memcpy(what + 2, fn, len);
		break;
	}
	cppargv[ncppargv++] = what;
}
