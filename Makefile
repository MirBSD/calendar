# $MirOS: src/usr.bin/calendar/Makefile,v 1.4 2021/10/29 02:35:32 tg Exp $
# $OpenBSD: Makefile,v 1.9 2004/12/10 20:47:30 mickey Exp $

.include <bsd.own.mk>

.if ${NOPIC:L} == "no"
UNICODE?=yes
.else
UNICODE:=no
.endif

PROG=	calendar
SRCS=   calendar.c io.c day.c pesach.c ostern.c paskha.c
INTER=	de_DE.ISO_8859-1 hr_HR.ISO_8859-2 ru_RU.KOI8-R fr_FR.ISO8859-1

beforeinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${.CURDIR}/calendars/calendar.* ${DESTDIR}/usr/share/calendar/
.for lang in ${INTER}
	@test -d ${DESTDIR}/usr/share/calendar/${lang} || \
	    mkdir ${DESTDIR}/usr/share/calendar/${lang}
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${.CURDIR}/calendars/${lang}/calendar.* \
	    ${DESTDIR}/usr/share/calendar/${lang}/
.endfor

.if ${UNICODE:L} == "yes"
DPADD+=		${LIBICONV}
LDADD+=		-liconv
CPPFLAGS+=	-DUNICODE
.endif

.include <bsd.prog.mk>
