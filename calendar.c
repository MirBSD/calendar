/**	$MirOS: src/usr.bin/calendar/calendar.c,v 1.4 2016/01/02 21:33:07 tg Exp $ */
/*	$OpenBSD: calendar.c,v 1.23 2004/12/10 15:31:01 mickey Exp $	*/

/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n");

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <login_cap.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tzfile.h>
#include <unistd.h>

#include "pathnames.h"
#include "calendar.h"

__SCCSID("@(#)calendar.c  8.3 (Berkeley) 3/25/94");
__RCSID("$MirOS: src/usr.bin/calendar/calendar.c,v 1.4 2016/01/02 21:33:07 tg Exp $");

char *calendarFile = "calendar";  /* default calendar file */
char *calendarHome = ".etc/calendar"; /* HOME */
char *calendarNoMail = "nomail";  /* don't sent mail if this file exists */

struct passwd *pw;
int doall = 0;
time_t f_time = 0;
int bodun_always = 0;

int f_dayAfter = 0; /* days after current date */
int f_dayBefore = 0; /* days before current date */

struct specialev spev[NUMEV];

void childsig(int);

int
main(int argc, char *argv[])
{
	int ch;
	char *caldir;

	(void)setlocale(LC_ALL, "");

	while ((ch = getopt(argc, argv, "abf:t:A:B:-")) != -1)
		switch (ch) {
		case '-':		/* backward contemptible */
		case 'a':
			if (getuid())
				errx(1, "%s", strerror(EPERM));
			doall = 1;
			break;

		case 'b':
			bodun_always++;
			break;

		case 'f': /* other calendar file */
		        calendarFile = optarg;
			break;

		case 't': /* other date, undocumented, for tests */
			if ((f_time = Mktime(optarg)) <= 0)
				errx(1, "specified date is outside allowed range");
			break;

		case 'A': /* days after current date */
			f_dayAfter = atoi(optarg);
			break;

		case 'B': /* days before current date */
			f_dayBefore = atoi(optarg);
			break;

		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	/* use current time */
	if (f_time <= 0)
	    (void)time(&f_time);

	if (f_dayBefore) {
		/* Move back in time and only look forwards */
		f_dayAfter += f_dayBefore;
		f_time -= SECSPERDAY * f_dayBefore;
		f_dayBefore = 0;
	}
	settime(&f_time);

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
				if (setusercontext(NULL, pw, pw->pw_uid,
				    LOGIN_SETALL ^ LOGIN_SETLOGIN))
					err(1, "unable to set user context (uid %u)",
					    pw->pw_uid);
				if (acstat) {
					if (chdir(pw->pw_dir) ||
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
			if (time(NULL) - t >= SECSPERDAY)
				errx(2, "'calendar -a' took more than a day; stopped at uid %u",
				    pw->pw_uid);
		}
		for (;;) {
			deadkid = waitpid(-1, &kidstat, WNOHANG);
			if (deadkid <= 0)
				break;
			runningkids--;
		}
		if (runningkids)
			warnx(
"%d child processes still running when 'calendar -a' finished", runningkids);
	}
	else if ((caldir = getenv("CALENDAR_DIR")) != NULL) {
		if(!chdir(caldir))
			cal();
	} else
		cal();

	exit(0);
}


void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: calendar [-ab] [-A num] [-B num] [-f calendarfile] "
	    "[-t [[[cc]yy][mm]]dd]\n");
	exit(1);
}


void
childsig(int signo)
{
}
