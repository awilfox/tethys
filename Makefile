include extra.mk

SUBDIRS = $(LIBMOWGLI) src modules help
CLEANDIRS = $(SUBDIRS) include
DISTCLEAN = extra.mk \
	buildsys.mk \
	config.log \
	config.status

include buildsys.mk

src modules: $(LIBMOWGLI)
