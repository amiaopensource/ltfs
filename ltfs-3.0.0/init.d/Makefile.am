
all:

install-data-local:
	if [ ! -d "@datadir@/ltfs" ]; then \
		mkdir -p "@datadir@/ltfs"; \
	fi ;\
	if [ ! -f "@datadir@/ltfs/ltfs" ]; then \
		install -m 755 ltfs "@datadir@/ltfs/ltfs"; \
	fi

clean-local:
