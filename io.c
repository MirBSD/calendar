/*	$OpenBSD: io.c,v 1.29+backports from 1.49 2005/04/15 14:28:56 otto Exp $	*/

/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2019, 2021, 2023
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

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#ifdef UNICODE
#define _ALL_SOURCE
#include <iconv.h>
#include <langinfo.h>
#endif
#include <locale.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#ifdef MBSDPORT_H
#include MBSDPORT_H
#endif

#include "pathnames.h"
#include "calendar.h"

__COPYRIGHT("Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.");
__SCCSID("@(#)calendar.c  8.3 (Berkeley) 3/25/94");
__RCSID("$MirOS: src/usr.bin/calendar/io.c,v 1.49 2023/06/03 21:39:10 tg Exp $");

#ifndef ioweg
#define ioweg iovec /* cf. MirBSD writev(2) manpage */
#endif

static struct ioweg header[] = {
	{ "From: ", 6 },
	{ NULL, 0 },
	{ " (Reminder Service)\nTo: ", 24 },
	{ NULL, 0 },
	{ "\nSubject: ", 10 },
	{ NULL, 0 },
#ifdef UNICODE
	{ "'s Calendar\nMIME-Version: 1.0\n"
	  "Content-type: text/plain; charset=utf-8\n"
	  "Precedence: bulk\n", 87 },
#else
	{ "'s Calendar\nPrecedence: bulk\n", 29 },
#endif
	{ "Auto-Submitted: auto-generated\n\n", 32 },
};

#ifdef UNICODE
iconv_t s_conv = (iconv_t)-1;
#endif

#define EXTRAINFO_ZONELEN (64U - 5U * sizeof(int) - 1U)
struct extrainfo {
	unsigned int year;
	int begh;
	int begm;
	int endh;
	int endm;
	char zone[EXTRAINFO_ZONELEN];
	unsigned char var;
};

static void cvtextra(struct extrainfo *, char *, char **);
static void cvtmatch(struct extrainfo *, struct match *, const char *);

void
settimefml(char *dayname, size_t len)
{
	header[5].iov_base = dayname;
	header[5].iov_len = len;
}

#ifdef UNICODE
static void
openconv(const char *cs)
{
	if (!cs || *cs == '\0' || !strcasecmp(cs, "utf8") ||
	    !strcasecmp(cs, "UTF-8") || !strcasecmp(cs, "ASCII") ||
	    !strcasecmp(cs, "ANSI_X3.4-1968")) {
		s_conv = (iconv_t)-1;
		return;
	}
#ifdef USE_GLIBC_ICONV
	/* needed due to lack of __iconv() */
	s_conv = iconv_open("UTF-8//TRANSLIT", cs);
#else
	s_conv = iconv_open("UTF-8", cs);
#endif
}

char *
touni(char *np)
{
	static char cvtdstbuf[2048 * 4 + 1];

	return (toutf(np, cvtdstbuf, sizeof(cvtdstbuf)));
}

char *
toutf(char *np, char *bp, size_t blen)
{
#ifdef USE_GLIBC_ICONV
	char *src;
#else
	const char *src;
#endif
	char *dst;
	size_t slen, dlen;

	if (s_conv == (iconv_t)-1)
		return (np);

	src = np;
	slen = strlen(np);
	dst = bp;
	dlen = blen;
#ifdef USE_GLIBC_ICONV
	/* using //IGNORE (or //TRANSLIT) in iconv_open DTRT */
	iconv(s_conv, &src, &slen, &dst, &dlen);
#else
	__iconv(s_conv, &src, &slen, &dst, &dlen, 1, NULL);
#endif
	*dst = '\0';
	if (slen)
		strlcat(bp, src, blen);
	return (bp);
}
#endif

void
cal(void)
{
	int printing;
	char *p;
	FILE *fp;
	int ch, l, i, bodun = 0, bodun_maybe = 0;
	char buf[2048 + 1], *prefix = NULL;
	struct event *events, *cur_evt, *ev1 = NULL, *tmp;
	size_t nlen;
	const char *hfyear[3] = {
		"%1$d: %2$d year(s) ago",
		"%1$d: this year",
		"%1$d: in %2$d year(s)"
	};
	unsigned char anniv = 0;

	events = NULL;
	cur_evt = NULL;
	if ((fp = opencal()) == NULL)
		return;
	/* SHOULD be safe here */
	if (parsecvt)
		if (chdir("/usr/share/zoneinfo"))
			warn("cannot verify timezones");
#ifdef UNICODE
	s_conv = (iconv_t)-1;
#endif
	for (printing = 0; fgets(buf, sizeof(buf), stdin) != NULL;) {
		char *lp = buf;

		if ((p = strchr(lp, '\n')) != NULL)
			*p = '\0';
		else
			while ((ch = getchar()) != '\n' && ch != EOF);
		for (l = strlen(lp); l > 0 && isspace(lp[l - 1]); l--)
			;
		if (l == 0 && lp[0] == '\t')
			++l;
		lp[l] = '\0';
		if (lp[0] == '\0')
			continue;
		if (strncmp(lp, "LANG=", 5) == 0) {
			printing = 0;
#ifdef UNICODE
			{
				const char *s_charset;

				if (s_conv != (iconv_t)-1)
					iconv_close(s_conv);
				if ((s_charset = strchr(lp, '.')) == NULL)
					s_charset = lp + 5;
				else
					++s_charset;
				if (s_charset[0] == 'C' && s_charset[1] == '\0')
					++s_charset;
				openconv(s_charset);
			}
#endif
			setlocale(LC_ALL, lp + 5);
#ifdef UNICODE
			if (s_conv == (iconv_t)-1)
				openconv(nl_langinfo(CODESET));
#endif
			setnnames();
			if (!strcmp(lp + 5, "ru_RU.KOI8-R") ||
			    !strcmp(lp + 5, "uk_UA.KOI8-U") ||
			    !strcmp(lp + 5, "by_BY.KOI8-B") ||
			    !strcmp(lp + 5, "ru_RU.UTF-8") ||
			    !strcmp(lp + 5, "uk_UA.UTF-8") ||
			    !strcmp(lp + 5, "by_BY.UTF-8")) {
				bodun_maybe++;
				bodun = 0;
				free(prefix);
				prefix = NULL;
			} else
				bodun = bodun_maybe = 0;
			continue;
		}
#ifdef UNICODE
		lp = touni(lp);
#endif
		if (strncmp(lp, "CALENDAR=", 9) == 0) {
			char *ep;

			printing = 0;
			if (lp[9] == '\0')
				calendar = 0;
			else if (!strcasecmp(lp + 9, "julian")) {
				calendar = JULIAN;
				errno = 0;
				julian = strtoul(lp + 14, &ep, 10);
				if (lp[0] == '\0' || *ep != '\0')
					julian = 13;
				if ((errno == ERANGE && julian == ULONG_MAX) ||
				    julian > 14)
					errx(1, "Julian calendar offset is too large");
			} else if (!strcasecmp(lp + 9, "gregorian"))
				calendar = GREGORIAN;
			else if (!strcasecmp(lp + 9, "lunar"))
				calendar = LUNAR;
			continue;
		}
		if (strncmp(lp, "ANNIV=", 6) == 0) {
			printing = 0;
			anniv = 1;
			if (lp[6] == '1' && !lp[7])
				continue;
			p = lp + 6;
			for (i = 0; i < 3; ++i) {
				if (!*p)
					break;
				hfyear[i] = p;
				while (*p && *p != 0x1F) {
					if (*p++ != '%')
						continue;
					if (*p == '%') {
						++p;
						continue;
					}
					if ((*p == '1' || *p == '2') &&
					    p[1] == '$' && p[2] == 'd') {
						p += 3;
						continue;
					}
					/* double the % */
					memmove(p, p - 1,
					    sizeof(lp) - ((p - 1) - lp));
					++p;
				}
				l = *p;
				*p++ = '\0';
				if ((hfyear[i] = strdup(hfyear[i])) == NULL)
					err(1, NULL);
				if (!l)
					break;
			}
			continue;
		}
		if (strncmp(lp, "BODUN=", 6) == 0) {
			printing = 0;
			if (bodun_maybe) {
				bodun++;
				free(prefix);
				if ((prefix = strdup(lp + 6)) == NULL)
					err(1, NULL);
			} else
				fprintf(stderr, "W: no bodun locale: %s\n", lp);
			continue;
		}
		/* User defined names for special events */
		if ((p = strchr(lp, '='))) {
			for (i = 0; i < NUMEV; i++) {
				if (strncasecmp(lp, spev[i].name, spev[i].nlen) == 0 &&
				    ((size_t)(p - lp) == spev[i].nlen) &&
				    lp[spev[i].nlen + 1U]) {
					p++;
					if (spev[i].uname != NULL)
						free(spev[i].uname);
					if ((spev[i].uname = strdup(p)) == NULL)
						err(1, NULL);
					spev[i].ulen = strlen(p);
					i = NUMEV + 1;
				}
			}
			if (i > NUMEV) {
				printing = 0;
				continue;
			}
		}
		if (lp[0] != '\t') {
			struct match *m;
			struct extrainfo ei;

			if ((p = strchr(lp, '\t')) == NULL) {
				fprintf(stderr,
				    "W: tabless line: %s\n", lp);
				printing = 0;
				continue;
			}
			cvtextra(&ei, lp, &p);
			printing = (m = isnow(lp, bodun)) ? 1 : 0;
			if (printing) {
				struct match *foo;

				ev1 = NULL;
				while (m) {
					if (parsecvt && ev1)
						errx(1, "more than one match?");
					cur_evt = (struct event *) malloc(sizeof(struct event));
					if (cur_evt == NULL)
						err(1, NULL);

					cur_evt->when = m->when;
					cur_evt->year = m->year;
					snprintf(cur_evt->print_date,
					    sizeof(cur_evt->print_date), "%s%c",
					    m->print_date,
					    (ei.var || m->var) ? '*' : ' ');
					if (ev1) {
						cur_evt->desc = ev1->desc;
						cur_evt->ldesc = NULL;
					} else {
						if (parsecvt)
							cvtmatch(&ei, m, p);
						if (m->bodun && prefix) {
							int l1 = strlen(prefix);
							int l2 = strlen(p);
							int len = l1 + l2 + 2;
							if ((cur_evt->ldesc =
							    malloc(len)) == NULL)
								err(1, NULL);
							snprintf(cur_evt->ldesc, len,
							    "\t%s %s", prefix, p + 1);
						} else if ((cur_evt->ldesc =
						    strdup(p)) == NULL)
							err(1, NULL);
						cur_evt->desc = &(cur_evt->ldesc);
						ev1 = cur_evt;
					}
					insert(&events, cur_evt);
					foo = m;
					m = m->next;
					free(foo);
				}
			}
		} else if (printing) {
			size_t olen;

			if (parsecvt)
				puts(lp);
			olen = strlen(ev1->ldesc);
			nlen = strlen(lp);
			if (!(ev1->ldesc = realloc(ev1->ldesc,
			    olen + 1U + nlen + 1U)))
				err(1, NULL);
			ev1->ldesc[olen] = '\n';
			memcpy(ev1->ldesc + (olen + 1U), lp, nlen);
			ev1->ldesc[olen + 1U + nlen] = '\0';
		} else if (parsecvt)
			fprintf(stderr,
			    "W: non-printing continuation line: %s\n", lp);
	}
	tmp = parsecvt ? NULL : events;
	while (tmp) {
		if (!anniv) {
 noanniv:
			fprintf(fp, "%s%s\n", tmp->print_date, *(tmp->desc));
		} else {
			p = *(tmp->desc);
			if (*p++ != '\t')
				goto noanniv;
			if (isdigit(p[0]) && isdigit(p[1]) && isdigit(p[2]) &&
			    isdigit(p[3]) && p[4] == ',' && isspace(p[5])) {
				p[4] = '\0';
				i = atoi(p);
				p += 6;
				while (isspace(*p))
					++p;
			} else if ((nlen = strlen(p)) < 7 ||
			    (nlen -= (l = ((uint8_t)p[nlen - 1] == 0xB5U &&
			    (uint8_t)p[nlen - 2] == 0xD0U &&
			    p[nlen - 3] == '-' ? 3 :
			    (uint8_t)p[nlen - 1] == 0xB3U &&
			    (uint8_t)p[nlen - 2] == 0xD0U ? 2 : 0))) < 7) {
				goto noanniv;
			} else if (isdigit(p[nlen - 1]) &&
			    isdigit(p[nlen - 2]) &&
			    (i = (isdigit(p[nlen - 3]) &&
			    isdigit(p[nlen - 4]) &&
			    isspace(p[nlen - 5]) && p[nlen - 6] == ',' ? 6 :
			    l && isdigit(p[nlen - 3]) &&
			    isspace(p[nlen - 4]) && p[nlen - 5] == ',' ? 5 :
			    l &&
			    isspace(p[nlen - 3]) && p[nlen - 4] == ',' ? 4 :
			    0))) {
				p[nlen - i] = '\0';
				p[nlen] = '\0';
				i = atoi(p + (nlen - (i - 2)));
			} else
				goto noanniv;
			l = tmp->year - i;
			snprintf(buf, sizeof(buf), hfyear[1 - sgn(l)],
			    i, abs(l));
			fprintf(fp, "%s\t%s, %s\n", tmp->print_date, p, buf);
		}
		tmp = tmp->next;
	}
	tmp = events;
	while (tmp) {
		events = tmp;
		free(tmp->ldesc);
		tmp = tmp->next;
		free(events);
	}
	closecal(fp);
}

int
getfield(char *p, char **endp, int *flags)
{
	int val, var, i;
	char *start, savech;

	for (; !isdigit(*p) && !isalpha(*p) && *p != '*' && *p != '\t'; ++p)
		;
	if (*p == '*') {			/* `*' is every month */
		*flags |= F_ISMONTH;
		*endp = p+1;
		return (-1);	/* means 'every month' */
	}
	if (isdigit(*p)) {
		val = strtol(p, &p, 10);	/* if 0, it's failure */
		for (; !isdigit(*p) && !isalpha(*p) && *p != '*'; ++p)
			;
		*endp = p;
		return (val);
	}
	for (start = p; isalpha(*++p); /* nothing */)
		;

	/* Sunday-1 */
	if (*p == '+' || *p == '-')
		for (/* nothing */; isdigit(*++p); /* nothing */)
			;

	savech = *p;
	*p = '\0';

	if ((val = getmonth(start)) != 0)
		/* Month */
		*flags |= F_ISMONTH;
	else if ((val = getday(start)) != 0) {
		/* Day */
		*flags |= F_ISDAY;

		/* variable weekday */
		if ((var = getdayvar(start)) != 0) {
			if (var <= 5 && var >= -4)
				val += var * 10;
#ifdef DEBUG
			printf("var: %d\n", var);
#endif
		}
	} else {
		/* Try specials (Easter, Paskha, ...) */
		for (i = 0; i < NUMEV; i++) {
			if (strncasecmp(start, spev[i].name, spev[i].nlen) == 0) {
				start += spev[i].nlen;
				val = i + 1;
				i = NUMEV + 1;
			} else if (spev[i].uname != NULL &&
			    strncasecmp(start, spev[i].uname, spev[i].ulen) == 0) {
				start += spev[i].ulen;
				val = i + 1;
				i = NUMEV + 1;
			}
		}
		if (i > NUMEV) {
			switch (*start) {
			case '-':
			case '+':
				var = atoi(start);
				if (var > 365 || var < -365)
					/* someone is just being silly */
					return (0);
				val += (NUMEV + 1) * var;
				/*
				 * We add one to the matching event and
				 * multiply by (NUMEV + 1) so as not to
				 * return 0 if there's a match. val will
				 * overflow if there is an obscenely
				 * large number of special events.
				 */
				break;
			}
			*flags |= F_SPECIAL;
		}
		if (!(*flags & F_SPECIAL)) {
		/* undefined rest */
			*p = savech;
			return (0);
		}
	}
	for (*p = savech; !isdigit(*p) && !isalpha(*p) && *p != '*' && *p != '\t'; ++p)
		;
	*endp = p;
	return (val);
}


FILE *
opencal(void)
{
	int pdes[2];
	struct stat st;

	/* test where calendar file exists */
	if (calendarFile[0] == '-' && calendarFile[1] == '\0')
		/* pass on as-is */;
	else if (stat(calendarFile, &st) || !S_ISREG(st.st_mode)) {
		if (!doall) {
			char *home = getenv("HOME");
			if (home == NULL || *home == '\0')
				errx(1, "cannot get home directory");
			if (chdir(home) ||
			    chdir(calendarHome) ||
			    stat(calendarFile, &st) || !S_ISREG(st.st_mode))
				errx(1, "no calendar file: \"%s\" or \"~/%s/%s\"",
				    calendarFile, calendarHome, calendarFile);
		}
	}

	if (pipe(pdes) == -1) {
		return (NULL);
	}
	switch (fork()) {
	case -1:			/* error */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		return (NULL);
	case 0: {
		int fd;

		/* child -- set stdout to pipe input */
		if (pdes[1] != STDOUT_FILENO) {
			(void)dup2(pdes[1], STDOUT_FILENO);
			(void)close(pdes[1]);
		}
		(void)close(pdes[0]);
		/* set stdin to /dev/null unless it is the file */
		if ((fd = open(_PATH_DEVNULL, O_RDWR, 0)) == -1)
			err(1, _PATH_DEVNULL);
		if (calendarFile[0] != '-' || calendarFile[1]) {
			if (fd != STDIN_FILENO)
				dup2(fd, STDIN_FILENO);
		}
		/*
		 * Set stderr to /dev/null.  Necessary so that cron does not
		 * wait for cpp to finish if it's running calendar -a.
		 */
		if (doall) {
			if (fd != STDERR_FILENO) {
				dup2(fd, STDERR_FILENO);
				/* don't leak fd */
				if (fd != STDIN_FILENO)
					close(fd);
			}
		} else if (fd != STDIN_FILENO)
			close(fd);
		execv(_PATH_CPP, cppargv);
		err(1, _PATH_CPP);
	    }
	}
	/* parent -- set stdin to pipe output */
	(void)dup2(pdes[0], STDIN_FILENO);
	(void)close(pdes[0]);
	(void)close(pdes[1]);

	/* not reading all calendar files, just set output to stdout */
	if (!doall)
		return (stdout);

	/* set output to a temporary file, so if no output don't send mail */
	return (tmpfile());
}

void
closecal(FILE *fp)
{
	struct stat sbuf;
	int nread, pdes[2], status;
	char buf[1024];
	pid_t pid = -1;

	if (!doall)
		return;

	(void)rewind(fp);
	if (fstat(fileno(fp), &sbuf) || !sbuf.st_size)
		goto done;
	if (pipe(pdes) == -1)
		goto done;
	switch ((pid = vfork())) {
	case -1:			/* error */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		goto done;
	case 0:
		/* child -- set stdin to pipe output */
		if (pdes[0] != STDIN_FILENO) {
			(void)dup2(pdes[0], STDIN_FILENO);
			(void)close(pdes[0]);
		}
		(void)close(pdes[1]);
		execl(_PATH_SENDMAIL, "sendmail", "-i", "-t", "-F",
		    "\"Reminder Service\"", (char *)NULL);
		warn(_PATH_SENDMAIL);
		_exit(1);
	}
	/* parent -- write to pipe input */
	(void)close(pdes[0]);

	header[1].iov_base = header[3].iov_base = pw->pw_name;
	header[1].iov_len = header[3].iov_len = strlen(pw->pw_name);
	if (writev(pdes[1], header, 8) == -1)
		warn("writing eMail header");
	while ((nread = read(fileno(fp), buf, sizeof(buf))) > 0)
		if (write(pdes[1], buf, nread) == -1)
			warn("writing eMail body");
	(void)close(pdes[1]);
 done:
	(void)fclose(fp);
	if (pid != -1) {
		while (waitpid(pid, &status, 0) == -1) {
			if (errno != EINTR)
				break;
		}
	}
}


void
insert(struct event **head, struct event *cur_evt)
{
	struct event *tmp, *tmp2;

	if (*head) {
		/* Insert this one in order */
		tmp = *head;
		tmp2 = NULL;
		while (tmp->next && tmp->when <= cur_evt->when) {
			tmp2 = tmp;
			tmp = tmp->next;
		}
		if (tmp->when > cur_evt->when) {
			cur_evt->next = tmp;
			if (tmp2)
				tmp2->next = cur_evt;
			else
				*head = cur_evt;
		} else {
			cur_evt->next = tmp->next;
			tmp->next = cur_evt;
		}
	} else {
		*head = cur_evt;
		cur_evt->next = NULL;
	}
}

static void
cvtextra(struct extrainfo *ei, char *lp, char **p)
{
	unsigned char *cp = (unsigned char *)*p;

#define U(c)		((unsigned int)(unsigned char)(c))
#define fromdigit(o)	(U(cp[(o)]) - U('0'))
#define fromdigits(o)	(fromdigit(o) * 10U + fromdigit((o) + 1))
#define is(o,c)		(U(cp[(o)]) == U(c))

	/* catch hardwired "variable" dates */
	ei->var = !!(*p > lp && is(-1, '*'));

	/* all the other extra stuff is for -P mode only */
	if (!parsecvt)
		return;

	/* -PP mode quotes first line verbatim, too */
	if (parsecvt > 1) {
		putchar('>');
		puts(lp);
	}

	/* tab */
	++cp;
	/* year */
	if ((is(0, '1') || is(0, '2')) && isdigit(cp[1]) &&
	    isdigit(cp[2]) && isdigit(cp[3]) && is(4, ',') &&
	    is(5, ' ') && cp[6] != '\0') {
		ei->year = fromdigits(0) * 100U + fromdigits(2);
		cp += 6;
	} else
		ei->year = 0;
	/* hours */
	if (is(2, ':') && isdigit(cp[0]) && isdigit(cp[1]) &&
	    isdigit(cp[3]) && isdigit(cp[4])) {
		size_t n;

		switch (U(cp[5])) {
		case U(0xE2):
			if (!is(6, 0x80) || !is(7, 0x93))
				goto nohours;
			n = 5 + 3;
			break;
		case U('-'):
			n = 5 + 1;
			break;
		case U(' '):
			/* ensure remaining Subject is not empty */
			if (!cp[6])
				goto nohours;
			/* FALLTHROUGH */
		case U('['):
			n = 5 + 0;
			break;
		default:
			goto nohours;
		}
		ei->begh = fromdigits(0);
		ei->begm = fromdigits(3);
		if (ei->begh == 24 && ei->begm == 0) {
			ei->begh = 23;
			ei->begm = 59;
		} else if (ei->begh > 23 || ei->begm > 59)
			goto nohours;
		if (is(n + 2, ':') && isdigit(cp[n]) && isdigit(cp[n + 1]) &&
		    isdigit(cp[n + 3]) && isdigit(cp[n + 4]) &&
		    (is(n + 5, ' ') || is(n + 5, '['))) {
			ei->endh = fromdigits(n);
			ei->endm = fromdigits(n + 3);
			if (ei->endh == 24 && ei->endm == 0) {
				ei->endh = 23;
				ei->endm = 59;
			} else if (ei->endh > 23 || ei->endm > 59)
				goto nohours;
			n += 5;
		} else if (n != 5 && !is(n, ' ') && !is(n, '[')) {
			/* dash, no valid hours */
			goto nohours;
		} else {
			ei->endh = -1;
			ei->endm = -1;
		}
		if (is(n, '[')) {
			unsigned char *bp;
			unsigned char *ep;
			struct stat sb;

			bp = cp + n + 1;
			ep = (unsigned char *)strchr((char *)bp, ']');
			if (!ep || U(ep[1]) != U(' ') || !ep[2])
				goto nohours;
			n = ep - bp;
			if (n >= EXTRAINFO_ZONELEN) {
				warnx("timezone too long: %s", *p);
				goto nohours;
			}
			memcpy(ei->zone, bp, n);
			ei->zone[n] = '\0';
			if (stat(ei->zone, &sb))
				warn("unknown timezone: %s", *p);
			else
				cp = ep + 2;
		} else if (!cp[n + 1]) {
			/* no Subject after this */
			goto nohours;
		} else {
			ei->zone[0] = '\0';
			cp += n + 1;
		}
	} else {
 nohours:
		ei->begh = -1;
		ei->begm = -1;
		ei->endh = -1;
		ei->endm = -1;
		ei->zone[0] = '\0';
	}
	/* and give back the tab from the beginning */
	*--cp = '\t';
	/* return current input position */
	*p = (char *)cp;
	/* initialise DTSTART year */
	setyear(ei->year);
}

static void
cvtmatch(struct extrainfo *ei, struct match *m, const char *s)
{
	struct tm tm;
	int ofs, d;
	const char *ccp;
#define WDAYS(x) wdays[((x) + 7) % 7]
	static const char * const wdays[7] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};

#ifdef EBADRPC
	errno = EBADRPC;
#else
	errno = EBADMSG;
#endif
	if (gmtime_r(&m->when, &tm) != &tm)
		err(1, "gmtime");
	/* fix or variable; first occurrence (day) */
	printf("%c%04lld-%02d-%02d ", (ei->var || m->var) ? '*' : '=',
	    (long long)tm.tm_year + 1900LL, tm.tm_mon + 1, tm.tm_mday);
	/* recurrence type; recurs on */
	switch (m->isspecial) {
	case 1:
		ccp = "Pesach";
		if (0)
			/* FALLTHROUGH */
	case 2:
		  ccp = "Easter";
		if (0)
			/* FALLTHROUGH */
	case 3:
		  ccp = "Paskha";
		if (0)
			/* FALLTHROUGH */
	case 4:
		  ccp = "Advent";
		if (0)
			/* FALLTHROUGH */
	default:
		  ccp = "BAD";
		printf("special %s%+d", ccp, m->vwd);
		break;
	case 0:
		/* cf. day.c:variable_weekday() */
		if (m->vwd < 0) {
			ofs = m->vwd / 10 - 1;
			d = 10 + (m->vwd % 10);
		} else {
			ofs = m->vwd / 10;
			d = m->vwd % 10;
		}
		switch (m->interval) {
		case YEARLY:
			printf("%s -%02d%c", ei->year ? "once" : "yearly",
			    tm.tm_mon + 1, m->vwd ? ',' : '-');
			if (0)
				/* FALLTHROUGH */
		case MONTHLY:
			  fputs("monthly ", stdout);
			if (m->vwd)
				printf("%s%+d", WDAYS(d - 1), ofs);
			else
				printf("%02d", tm.tm_mday);
			break;
		case WEEKLY:
			fputs("weekly ", stdout);
			if (m->vwd)
				putchar('?');
			else
				fputs(WDAYS(tm.tm_wday), stdout);
			break;
		default:
			fputs("? ?", stdout);
			break;
		}
		break;
	}
	putchar(' ');
	/* year */
	if (ei->year)
		printf("%04u", ei->year);
	else
		putchar('*');
	putchar(' ');
	/* start and end times */
	if (ei->begh != -1) {
		printf("%02d:%02d", ei->begh, ei->begm);
		putchar(' ');
		if (ei->endh != -1)
			printf("%02d:%02d", ei->endh, ei->endm);
		else
			putchar('@');
		if (ei->zone[0]) {
			putchar(' ');
			fputs(ei->zone, stdout);
		}
	} else
		fputs("whole-day", stdout);
	putchar('\n');

	/* for now */
	puts(s);
}
