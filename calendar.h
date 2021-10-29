/*	$OpenBSD: calendar.h,v 1.11 2004/12/10 20:47:30 mickey Exp $	*/

/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2021
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

#ifndef CALENDAR_H
#define CALENDAR_H "$MirOS: src/usr.bin/calendar/calendar.h,v 1.8 2021/10/29 02:32:41 tg Exp $"

extern struct passwd *pw;
extern unsigned char doall;
extern unsigned char bodun_always;
extern unsigned char parsecvt;
extern time_t f_time;
extern struct ioweg header[];
extern const char *calendarFile;
extern const char *calendarHome;

struct fixs {
	char *name;
	int len;
};

struct event {
	time_t	when;
	char	print_date[31];
	char	**desc;
	char	*ldesc;
	struct event	*next;
	short	year;
};

struct match {
	time_t	when;
	struct match	*next;
	int	vwd;
	short	year;
	unsigned char bodun;
	unsigned char interval;
	unsigned char isspecial;
	unsigned char var;
	char	print_date[31];
};

struct specialev {
	char *name;
	int nlen;
	char *uname;
	int ulen;
	int (*getev)(int);
};

void	 cal(void);
void	 closecal(FILE *);
int	 getday(char *);
int	 getdayvar(char *);
int	 getfield(char *, char **, int *);
int	 getmonth(char *);
int	 advent(int);
int	 pesach(int);
int	 easter(int);
int	 paskha(int);
void	 insert(struct event **, struct event *);
struct match	*isnow(char *, int);
FILE	*opencal(void);
void	 settime(void);
void	 setyear(unsigned int);
time_t	 Mktime(char *);
void	 usage(void) __dead;
int	 foy(int);
void	 variable_weekday(int *, int, int);
void	 setnnames(void);

/* some flags */
#define	F_ISMONTH	0x01 /* month (Januar ...) */
#define	F_ISDAY		0x02 /* day of week (Sun, Mon, ...) */
/*#define	F_ISDAYVAR	0x04  variables day of week, like SundayLast */
#define	F_SPECIAL	0x08 /* Events that occur once a year but don't track
			      * calendar time--e.g.  Easter or easter depending
			      * days */

/* intervals */
#define WEEKLY		1
#define MONTHLY		2
#define YEARLY		3

extern int f_dayAfter;	/* days after current date */
extern int f_dayBefore;	/* days before current date */

/* Special events; see also setnnames() in day.c */
/* '=' is not a valid character in a special event name */
#define PESACH "pesach"
#define PESACHLEN (sizeof(PESACH) - 1)
#define EASTER "easter"
#define EASTERNAMELEN (sizeof(EASTER) - 1)
#define PASKHA "paskha"
#define PASKHALEN (sizeof(PASKHA) - 1)
#define ADVENT "advent"
#define ADVENTLEN (sizeof(ADVENT) - 1)

/* total number of such special events */
#define NUMEV 4
extern struct specialev spev[NUMEV];

/* calendars */
extern enum calendars { GREGORIAN = 0, JULIAN, LUNAR } calendar;
extern u_long julian;

/* For calendar -a, specify a maximum time (in seconds) to spend parsing
 * each user's calendar files.  This prevents them from hanging calendar
 * (e.g. by using named pipes)
 */
#define USERTIMEOUT 20

#define sgn(x) (((x) > 0) - ((x) < 0))
#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

#endif
