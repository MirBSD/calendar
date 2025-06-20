/*	$OpenBSD: day.c,v 1.18 2004/12/10 20:47:30 mickey Exp $	*/

/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2021, 2023, 2025
 *	mirabilos <m$(date +%Y)@mirbsd.de>
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

#include <sys/cdefs.h>
__COPYRIGHT("Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.");
__SCCSID("@(#)calendar.c  8.3 (Berkeley) 3/25/94");
__RCSID("$MirOS: src/usr.bin/calendar/day.c,v 1.28 2025/06/20 01:20:39 tg Exp $");

#include <sys/types.h>
#include <sys/uio.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef ioweg
#define ioweg iovec /* cf. MirBSD writev(2) manpage; do NOT move! */
#endif

#include "pathnames.h"
#include "calendar.h"

static struct tm tb;
static time_t d_time;
static const int *cumdays1;
int offset1;
char dayname[10];
enum calendars calendar;
u_long julian;


/* 1-based month, 0-based days, cumulative */
const int daytab[][14] = {
	{ 0, -1, 30, 58, 89, 119, 150, 180, 211, 242, 272, 303, 333, 364 },
	{ 0, -1, 30, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
};

static const char * const days[] = {
	"sun", "mon", "tue", "wed", "thu", "fri", "sat", NULL,
};

static const char * const months[] = {
	"jan", "feb", "mar", "apr", "may", "jun",
	"jul", "aug", "sep", "oct", "nov", "dec", NULL,
};

static struct fixs fndays[8];         /* full national days names */
static struct fixs ndays[8];          /* short national days names */

static struct fixs fnmonths[13];      /* full national months names */
static struct fixs nmonths[13];       /* short national month names */


void
setnnames(void)
{
	char buf[80];
	int i, l;
	struct tm tm;

	memcpy(&tm, &tb, sizeof(struct tm));

	for (i = 0; i < 7; i++) {
		tm.tm_wday = i;
		l = strftime(buf, sizeof(buf), "%a", &tm);
		for (; l > 0 && isspace((int)buf[l - 1]); l--)
			;
		buf[l] = '\0';
		if (ndays[i].name != NULL)
			free(ndays[i].name);
		if ((ndays[i].name = strdup(touni(buf))) == NULL)
			err(1, NULL);
		ndays[i].len = strlen(ndays[i].name);

		l = strftime(buf, sizeof(buf), "%A", &tm);
		for (; l > 0 && isspace((int)buf[l - 1]); l--)
			;
		buf[l] = '\0';
		if (fndays[i].name != NULL)
			free(fndays[i].name);
		if ((fndays[i].name = strdup(touni(buf))) == NULL)
			err(1, NULL);
		fndays[i].len = strlen(fndays[i].name);
	}

	for (i = 0; i < 12; i++) {
		tm.tm_mon = i;
		l = strftime(buf, sizeof(buf), "%b", &tm);
		for (; l > 0 && isspace((int)buf[l - 1]); l--)
			;
		buf[l] = '\0';
		if (nmonths[i].name != NULL)
			free(nmonths[i].name);
		if ((nmonths[i].name = strdup(touni(buf))) == NULL)
			err(1, NULL);
		nmonths[i].len = strlen(nmonths[i].name);

		l = strftime(buf, sizeof(buf), "%B", &tm);
		for (; l > 0 && isspace((int)buf[l - 1]); l--)
			;
		buf[l] = '\0';
		if (fnmonths[i].name != NULL)
			free(fnmonths[i].name);
		if ((fnmonths[i].name = strdup(touni(buf))) == NULL)
			err(1, NULL);
		fnmonths[i].len = strlen(fnmonths[i].name);
	}
	/* Hardwired special events */
	spev[0].name = strdup(PESACH);
	spev[0].nlen = PESACHLEN;
	spev[0].getev = pesach;
	spev[1].name = strdup(EASTER);
	spev[1].nlen = EASTERNAMELEN;
	spev[1].getev = easter;
	spev[2].name = strdup(PASKHA);
	spev[2].nlen = PASKHALEN;
	spev[2].getev = paskha;
	spev[3].name = strdup(ADVENT);
	spev[3].nlen = ADVENTLEN;
	spev[3].getev = advent;
	for (i = 0; i < NUMEV; i++) {
		if (spev[i].name == NULL)
			err(1, NULL);
		spev[i].uname = NULL;
	}
}

void
setyear(unsigned int y)
{
	if (y) {
		tb.tm_year = y - 1900U;
		tb.tm_mon = 0;
		tb.tm_mday = 1;
	} else if (localtime_r(&f_time, &tb) != &tb)
		err(1, "localtime_r");
	tb.tm_sec = 0;
	tb.tm_min = 0;
	/* Avoid getting caught by a timezone shift; set time to noon */
	tb.tm_isdst = 0;
	tb.tm_hour = 12;
	d_time = mktime(&tb);
	if (isleap(tb.tm_year + 1900))
		cumdays1 = daytab[1];
	else
		cumdays1 = daytab[0];
}

/* called only once, near beginning */
void
settime(int offset_specified)
{
	setyear(0);
	f_time = d_time;
	/* Friday displays Monday's events */
	offset1 = tb.tm_wday == 5 ? 3 : 1;
	if (offset_specified)
		offset1 = 0;	/* Except not when range is set explicitly */

	setlocale(LC_ALL, "C");
	settimefml(dayname, strftime(dayname, sizeof(dayname), "%A", &tb));
	setlocale(LC_ALL, "");

	setnnames();
}

/* convert [[Year]Month]Day into unix time (since 1970)
 * Year: two or four digits, Month: two digits, Day: two digits
 */
time_t
Mktime(char *date)
{
	time_t t;
	int len;
	struct tm tm;

	(void)time(&t);
	if (localtime_r(&t, &tb) != &tb)
		err(1, "localtime_r");

	len = strlen(date);
	if (len < 2 || len == 3 || len == 5 || len == 7)
		return ((time_t)-1);
	tm.tm_sec = 0;
	tm.tm_min = 0;
	/* avoid getting caught by a timezone shift; set time to noon */
	tm.tm_isdst = 0;
	tm.tm_hour = 12;
	tm.tm_wday = 0;
	tm.tm_mday = tb.tm_mday;
	tm.tm_mon = tb.tm_mon;
	tm.tm_year = tb.tm_year;

	/* Day */
	tm.tm_mday = atoi(date + len - 2);

	/* Month */
	if (len >= 4) {
		*(date + len - 2) = '\0';
		tm.tm_mon = atoi(date + len - 4) - 1;
	}

	/* Year */
	if (len >= 6) {
		*(date + len - 4) = '\0';
		tm.tm_year = atoi(date);

		/* tm_year up TM_YEAR_BASE ... */
		if (len == 6 && tm.tm_year >= 0) {
			/* 1969‥2068 */
			if (tm.tm_year < 69)
				tm.tm_year += 2000 - 1900;
		} else
			tm.tm_year -= 1900;
	}

#ifdef DEBUG
	printf("Mktime: %d %d %d %s\n", (int)mktime(&tm), (int)t, len,
	    asctime(&tm));
#endif
	return (mktime(&tm));
}

static void
adjust_calendar(int *day, int *month)
{
	switch (calendar) {
	case GREGORIAN:
		break;

	case JULIAN:
		*day += julian;
		if (*day > (cumdays1[*month + 1] - cumdays1[*month])) {
			*day -= (cumdays1[*month + 1] - cumdays1[*month]);
			if (++*month > 12)
				*month = 1;
		}
		break;

	case LUNAR:
		/* not yet implemented */
		break;
	}
}

static void
print_date(struct match *dst, struct tm *tp)
{
	char buf[32];
#ifdef UNICODE
	char conv[32];
#endif

	memset(buf, '\0', sizeof(buf));
	strftime(buf, sizeof(buf),
	/*  "%a %b %d" but skip weekdays */
	    "%b %d", tp);
	buf[sizeof(buf) - 1] = '\0';
	memcpy(dst->print_date, toutf(buf, conv, sizeof(conv)),
	    sizeof(dst->print_date));
	dst->print_date[sizeof(dst->print_date) - 1] = '\0';
}

/*
 * Possible date formats include any combination of:
 *	3-charmonth			(January, Jan, Jan)
 *	3-charweekday			(Friday, Monday, mon.)
 *	numeric month or day		(1, 2, 04)
 *
 * Any character except \t or '*' may separate them, or they may not be
 * separated.  Any line following a line that is matched, that starts
 * with \t, is shown along with the matched line.
 */
struct match *
isnow(char *endp, int bodun)
{
	int day = 0, flags = 0, month = 0, v1, v2, i;
	int monthp, dayp, varp = 0;
	struct match *matches = NULL, *tmp, *tmp2;
	int interval = YEARLY;	/* how frequently the event repeats. */
	int vwd = 0;	/* Variable weekday */
	time_t tdiff, ttmp;
	struct tm tmtmp;
	const char *lp = endp;

	/*
	 * CONVENTION
	 *
	 * Month:     1-12
	 * Monthname: Jan .. Dec
	 * Day:       1-31
	 * Weekday:   Mon-Sun
	 *
	 */

	/* read first field */
	/* didn't recognize anything, skip it */
	if (!(v1 = getfield(endp, &endp, &flags))) {
		fprintf(stderr, "W: unrecognised first field: %s\n", lp);
		return (NULL);
	}

	/* adjust bodun rate */
	if (bodun && !bodun_always)
		bodun = calendar_bodunratecheck();
	if (parsecvt)
		bodun = 0;

	if (flags & F_SPECIAL)
		/* Advent/Easter/etc. or days depending on these */
		vwd = v1;
	else if (flags & F_ISDAY || v1 > 12) {
		/*
		 * 1. {Weekday,Day} XYZ ...
		 *
		 *    where Day is > 12
		 */

		/* found a day; day: 13-31 or weekday: 1-7 */
		day = v1;

		/* {Day,Weekday} {Month,Monthname} ... */
		/* if no recognizable month, assume just a day alone -- this is
		 * very unlikely and can only happen after the first 12 days.
		 * --find month or use current month */
		if (!(month = getfield(endp, &endp, &flags))) {
			month = tb.tm_mon + 1;
			/* F_ISDAY is set only if a weekday was spelled out */
			/* F_ISDAY must be set if 0 < day < 8 */
			if ((day <= 7) && (day >= 1))
				interval = WEEKLY;
			else
				interval = MONTHLY;
		} else if ((day <= 7) && (day >= 1))
			day += 10;
			/* it's a weekday; make it the first one of the month */
		if (month == -1) {
			month = tb.tm_mon + 1;
			interval = MONTHLY;
		} else if (calendar)
			adjust_calendar(&day, &month);
		if ((month > 12) || (month < 1)) {
			fprintf(stderr, "W: out-of-bounds month: %s\n", lp);
			return (NULL);
		}
	} else if (flags & F_ISMONTH) {
		/* 2. {Monthname} XYZ ... */
		month = v1;
		if (month == -1) {
			month = tb.tm_mon + 1;
			interval = MONTHLY;
		}
		/* Monthname {day,weekday} */
		/* if no recognizable day, assume the first day in month */
		if (!(day = getfield(endp, &endp, &flags)))
			day = 1;
		/* If a weekday was spelled out without an ordering,
		 * assume the first of that day in the month */
		if ((flags & F_ISDAY)) {
			if ((day >= 1) && (day <=7))
				day += 10;
		} else if (calendar)
			adjust_calendar(&day, &month);
	} else {
		/* Hm ... */
		v2 = getfield(endp, &endp, &flags);

		if (flags & F_ISMONTH) {
			/*
			 * {Day} {Monthname} ...
			 * where Day <= 12
			 */
			day = v1;
			month = v2;
			if (month == -1) {
				month = tb.tm_mon + 1;
				interval = MONTHLY;
			} else if (calendar)
				adjust_calendar(&day, &month);
		} else {
			/* {Month} {Weekday,Day} ...  */

			/* F_ISDAY set, v2 > 12, or no way to tell */
			month = v1;
			/* if no recognizable day, assume the first */
			day = v2 ? v2 : 1;
			if ((flags & F_ISDAY)) {
				if ((day >= 1) && (day <= 7))
					day += 10;
			} else
				adjust_calendar(&day, &month);
		}
	}

	/* convert Weekday into *next* Day,
	 * e.g.: 'Sunday' -> 22
	 *       'SundayLast' -> ??
	 */
	if (flags & F_ISDAY) {
#ifdef DEBUG
		fprintf(stderr, "\nday: %d %s month %d\n", day, endp, month);
#endif

		varp = 1;
		/* variable weekday, SundayLast, MondayFirst ... */
		if (day < 0 || day >= 10)
			vwd = day;
		else {
			day = tb.tm_mday + (((day - 1) - tb.tm_wday + 7) % 7);
			interval = WEEKLY;
		}
	} else if (!(flags & F_SPECIAL) &&
	    (day > (cumdays1[month + 1] - cumdays1[month]) || day < 1)) {
		/* Check for silliness.  Note we still catch Feb 29 */
		if (!((month == 2 && day == 29) ||
		    (interval == MONTHLY && day <= 31))) {
			fprintf(stderr, "W: silly day: %s\n", lp);
			return (NULL);
		}
	}

	if (!(flags & F_SPECIAL)) {
		monthp = month;
		dayp = day;
		day = cumdays1[month] + day;
#ifdef DEBUG
		fprintf(stderr, "day2: day %d(%d) yday %d\n", dayp, day, tb.tm_yday);
#endif
	/* Speed up processing for the most common situation:  yearly events
	 * when the interval being checked is less than a month or so (this
	 * could be less than a year, but then we have to start worrying about
	 * leap years).  Only one event can match, and it's easy to find.
	 * Note we can't check special events, because they can wander widely.
	 */
		if (!parsecvt && ((v1 = offset1 + f_dayAfter) < 50) &&
		    (interval == YEARLY)) {
			memcpy(&tmtmp, &tb, sizeof(struct tm));
			tmtmp.tm_mday = dayp;
			tmtmp.tm_mon = monthp - 1;
			if (vwd) {
			/* We want the event next year if it's late now
			 * this year.  The 50-day limit means we don't have to
			 * worry if next year is or isn't a leap year.
			 */
				if (tb.tm_yday > 300 && tmtmp.tm_mon <= 1)
					variable_weekday(&vwd, tmtmp.tm_mon + 1,
					    tmtmp.tm_year + 1900 + 1);
				else
					variable_weekday(&vwd, tmtmp.tm_mon + 1,
					    tmtmp.tm_year + 1900);
				day = cumdays1[tmtmp.tm_mon + 1] + vwd;
				tmtmp.tm_mday = vwd;
			}
			v2 = day - tb.tm_yday;
			if ((v2 > v1) || (v2 < 0)) {
				v2 += isleap(tb.tm_year + 1900) ? 366 : 365;
				if (v2 <= v1)
					tmtmp.tm_year++;
				else if (!bodun || (day - tb.tm_yday) != -1)
					return (NULL);
			}
			if ((tmp = calloc(1, sizeof(struct match))) == NULL)
				err(1, NULL);

			if (bodun && (day - tb.tm_yday) == -1) {
				tmp->when = d_time - 1 * 86400;
				tmtmp.tm_mday++;
				tmp->bodun = 1;
			} else {
				tmp->when = d_time + v2 * 86400;
				tmp->bodun = 0;
			}

			(void)mktime(&tmtmp);
			print_date(tmp, &tmtmp);
			tmp->year  = tmtmp.tm_year + 1900;
			tmp->var   = varp;
			tmp->next  = NULL;
			return (tmp);
		}
	} else {
		/* flags has F_SPECIAL set */

		varp = 1;
		/* Set up v1 to the event number and ... */
		v1 = vwd % (NUMEV + 1) - 1;
		vwd /= (NUMEV + 1);
		if (v1 < 0) {
			v1 += NUMEV + 1;
			vwd--;
		}
		dayp = monthp = 1;	/* Why not */
	}

	/* Compare to past and coming instances of the event.  The i == 0 part
	 * of the loop corresponds to this specific instance.  Note that we
	 * can leave things sort of higgledy-piggledy since a mktime() happens
	 * on this before anything gets printed.  Also note that even though
	 * we've effectively gotten rid of f_dayBefore, we still have to check
	 * the one prior event for situations like "the 31st of every month"
	 * and "yearly" events which could happen twice in one year but not in
	 * the next */
	tmp2 = matches;
	i = -1;
	while (parsecvt || i < 2) {
		memcpy(&tmtmp, &tb, sizeof(struct tm));
		tmtmp.tm_mday = dayp;
		tmtmp.tm_mon = month = monthp - 1;
		do {
			v2 = 0;
			switch (interval) {
			case WEEKLY:
				tmtmp.tm_mday += 7 * i;
				break;
			case MONTHLY:
				month += i;
				tmtmp.tm_mon = month;
				switch (tmtmp.tm_mon) {
				case -1:
					tmtmp.tm_mon = month = 11;
					tmtmp.tm_year--;
					break;
				case 12:
					tmtmp.tm_mon = month = 0;
					tmtmp.tm_year++;
					break;
				}
				if (vwd) {
					v1 = vwd;
					variable_weekday(&v1, tmtmp.tm_mon + 1,
					    tmtmp.tm_year + 1900);
					tmtmp.tm_mday = v1;
				} else
					tmtmp.tm_mday = dayp;
				break;
			case YEARLY:
			default:
				tmtmp.tm_year += i;
				if (flags & F_SPECIAL) {
					tmtmp.tm_mon = 0;	/* Gee, mktime() is nice */
					tmtmp.tm_mday = spev[v1].getev(tmtmp.tm_year +
					    1900) + vwd;
				} else if (vwd) {
					v1 = vwd;
					variable_weekday(&v1, tmtmp.tm_mon + 1,
					    tmtmp.tm_year + 1900);
					tmtmp.tm_mday = v1;
				} else {
					/* Need the following to keep Feb 29 from
					 * becoming Mar 1 */
					tmtmp.tm_mday = dayp;
					tmtmp.tm_mon = monthp - 1;
				}
				break;
			}
			/* How many days apart are we */
			if ((ttmp = mktime(&tmtmp)) == -1) {
				warnx("time out of range: %s", endp);
				continue;
			}
			tdiff = difftime(ttmp, d_time) / 86400;
			if (parsecvt) {
				if (tdiff >= 0)
					goto doit;
				/* out of the v2 loop into the i one */
				break;
			}
			if (!(tdiff <= offset1 + f_dayAfter ||
			    (bodun && tdiff == -1))) {
				/* no point checking in the future */
				i = 2;
				continue;
			}
			if (((tmtmp.tm_mon == month) ||
			     (flags & F_SPECIAL) ||
			     (interval == WEEKLY)) &&
			    (tdiff >= 0 ||
			    (bodun && tdiff == -1))) {
 doit:
				if ((tmp = calloc(1, sizeof(struct match))) == NULL)
					err(1, NULL);
				tmp->when = ttmp;
				print_date(tmp, &tmtmp);
				tmp->year  = tmtmp.tm_year + 1900;
				tmp->bodun = bodun && tdiff == -1;
				tmp->var   = varp;
				tmp->vwd   = vwd;
				tmp->next  = NULL;
				if (flags & F_SPECIAL)
					tmp->isspecial = v1 + 1;
				tmp->interval = interval;
				if (tmp2)
					tmp2->next = tmp;
				else
					matches = tmp;
				if (parsecvt)
					return (matches);
				tmp2 = tmp;
				v2 = (i == 1) ? 1 : 0;
			}
		} while (v2 != 0);
		++i;
	}
	return (matches);
}


int
getmonth(char *s)
{
	const char * const *p;
	struct fixs *n;

	for (n = fnmonths; n->name; ++n)
		if (!strncasecmp(s, n->name, n->len))
			return ((n - fnmonths) + 1);
	for (n = nmonths; n->name; ++n)
		if (!strncasecmp(s, n->name, n->len))
			return ((n - nmonths) + 1);
	for (p = months; *p; ++p)
		if (!strncasecmp(s, *p, 3))
			return ((p - months) + 1);
	return (0);
}


int
getday(char *s)
{
	const char * const *p;
	struct fixs *n;

	for (n = fndays; n->name; ++n)
		if (!strncasecmp(s, n->name, n->len))
			return ((n - fndays) + 1);
	for (n = ndays; n->name; ++n)
		if (!strncasecmp(s, n->name, n->len))
			return ((n - ndays) + 1);
	for (p = days; *p; ++p)
		if (!strncasecmp(s, *p, 3))
			return ((p - days) + 1);
	return (0);
}

/* return offset for variable weekdays
 * -1 -> last weekday in month
 * +1 -> first weekday in month
 * ... etc ...
 */
int
getdayvar(char *s)
{
	int offset2;

	offset2 = strlen(s);

	/* Sun+1 or Wednesday-2
	 *    ^              ^   */

	/* printf ("x: %s %s %d\n", s, s + offset2 - 2, offset2); */
	switch (*(s + offset2 - 2)) {
	case '-':
	case '+':
		return (atoi(s + offset2 - 2));
		break;
	}

	/*
	 * some aliases: last, first, second, third, fourth
	 */

	/* last */
	if      (offset2 > 4 && !strcasecmp(s + offset2 - 4, "last"))
		return (-1);
	else if (offset2 > 5 && !strcasecmp(s + offset2 - 5, "first"))
		return (+1);
	else if (offset2 > 6 && !strcasecmp(s + offset2 - 6, "second"))
		return (+2);
	else if (offset2 > 5 && !strcasecmp(s + offset2 - 5, "third"))
		return (+3);
	else if (offset2 > 6 && !strcasecmp(s + offset2 - 6, "fourth"))
		return (+4);

	/* no offset detected */
	return (0);
}


int
foy(int year)
{
	/* 0-6; what weekday Jan 1 is */
	year--;
	return ((1 - year/100 + year/400 + (int)(365.25 * year)) % 7); /* 365.2425 days per year */
}



void
variable_weekday(int *day, int month, int year)
{
	int v1, v2;
	const int *cumdays2;
	int day1;

	if (isleap(year))
		cumdays2 = daytab[1];
	else
		cumdays2 = daytab[0];
	day1 = foy(year);
	if (*day < 0) {
		/* negative offset; last, -4 .. -1 */

		v1 = *day/10 - 1;          /* offset -4 ... -1 */
		*day = 10 + (*day % 10);    /* day 1 ... 7 */

		/* which weekday the end of the month is (1-7) */
		v2 = (cumdays2[month + 1] + day1) % 7 + 1;

		/* and subtract enough days */
		*day = cumdays2[month + 1] - cumdays2[month] +
		    (v1 + 1) * 7 - (v2 - *day + 7) % 7;
#ifdef DEBUG
		fprintf(stderr, "\nMonth %d ends on weekday %d\n", month, v2);
#endif
	} else {
		/* first, second ... +1 ... +5 */

		v1 = *day/10;        /* offset */
		*day = *day % 10;

		/* which weekday the first of the month is (1-7) */
		v2 = (cumdays2[month] + 1 + day1) % 7 + 1;

		/* and add enough days */
		*day = 1 + (v1 - 1) * 7 + (*day - v2 + 7) % 7;
#ifdef DEBUG
		fprintf(stderr, "\nMonth %d starts on weekday %d\n", month, v2);
#endif
	}
}


int
advent(int /* NOT 1900-biased */ year)
{
	struct tm tmtmp;

	memcpy(&tmtmp, &tb, sizeof(struct tm));
	tmtmp.tm_mday = 25;
	tmtmp.tm_mon = 11;
	tmtmp.tm_year = year - 1900;
	errno = EDOM;
	if (mktime(&tmtmp) == -1)
		err(1, "cannot calculate Advent for %d", year);
	tmtmp.tm_mday -= tmtmp.tm_wday ? tmtmp.tm_wday : /* Sunday */ 7;
	errno = EINVAL;
	if (mktime(&tmtmp) == -1)
		err(1, "cannot calculate Advent for %d", year);
	return (tmtmp.tm_yday + 1 - /* three weeks */ 21);
}
