dist_sysconf_DATA = ltfs.conf

ltfs.conf: ltfs.conf.linux
	rm -f ltfs.conf
	sed -e "s!__LIBDIR__!@libdir@!" ltfs.conf.linux >ltfs.conf
	mv ltfs.conf ltfs.conf.tmp
	sed -e "s!__DEFAULT_DRIVER__!$(DEFAULT_DRIVER)!" ltfs.conf.tmp >ltfs.conf
	mv ltfs.conf ltfs.conf.tmp
	sed -e "s!__DEFAULT_IOSCHED__!$(DEFAULT_IOSCHED)!" ltfs.conf.tmp >ltfs.conf
	mv ltfs.conf ltfs.conf.tmp
	sed -e "s!__DEFAULT_KMI__!$(DEFAULT_KMI)!" ltfs.conf.tmp >ltfs.conf
	mv ltfs.conf ltfs.conf.tmp
	sed -e "s!__CONFDIR__!@sysconfdir@!" ltfs.conf.tmp >ltfs.conf
	rm ltfs.conf.tmp

install-data-local:
	if [ ! -f "$(sysconfdir)/ltfs.conf.local" ]; then \
		cp ltfs.conf.local "$(sysconfdir)/ltfs.conf.local"; \
	fi

clean-local:
	rm -f ltfs.conf
