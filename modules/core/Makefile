MODULE = core

SRCS = \
	away.c \
	bmask.c \
	cap.c \
	euid.c \
	help.c \
	hunted.c \
	invite.c \
	join.c \
	kick.c \
	kill.c \
	list.c \
	map.c \
	message.c \
	mkpass.c \
	mode.c \
	module.c \
	names.c \
	nick.c \
	numeric.c \
	oper.c \
	part.c \
	pass.c \
	ping.c \
	quit.c \
	sid.c \
	sjoin.c \
	squit.c \
	stats.c \
	su.c \
	tb.c \
	ts6init.c \
	topic.c \
	user.c \
	userhost.c \
	who.c \
	whois.c

include ../../buildsys.mk
include ../../buildsys.module.mk
include ../../extra.mk

CFLAGS += -I../../include -I../../src

CFLAGS += $(MOWGLI_CFLAGS)
LIBS += $(LDFLAGS_RPATH)
LIBS += $(MOWGLI_LIBS)
