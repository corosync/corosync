# to build official release tarballs, handle tagging and publish.

# signing key
gpgsignkey=

project=corosync

all: checks setup tag tarballs sha256 sign

checks:
ifeq (,$(version))
	@echo ERROR: need to define version=
	@exit 1
endif
	@if [ ! -d .git ]; then \
		echo This script needs to be executed from top level cluster git tree; \
		exit 1; \
	fi

setup: checks
	./autogen.sh
	./configure
	make maintainer-clean

tag: setup ./tag-$(version)

tag-$(version):
ifeq (,$(release))
	@echo Building test release $(version), no tagging
else
	git tag -a -m "v$(version) release" v$(version) HEAD
	@touch $@
endif

tarballs: tag
	./autogen.sh
	./configure
	make distcheck

sha256: tarballs $(project)-$(version).sha256

$(project)-$(version).sha256:
ifeq (,$(release))
	@echo Building test release $(version), no sha256
else
	sha256sum $(project)-$(version)*tar* | sort -k2 > $@
endif

sign: sha256 $(project)-$(version).sha256.asc

$(project)-$(version).sha256.asc: $(project)-$(version).sha256
ifeq (,$(gpgsignkey))
	@echo No GPG signing key defined
else
ifeq (,$(release))
	@echo Building test release $(version), no sign
else
	gpg --default-key $(gpgsignkey) \
		--detach-sign \
		--armor \
		$<
endif
endif

publish:
ifeq (,$(release))
	@echo Building test release $(version), no publishing!
else
	@echo CHANGEME git push --tags origin
	@echo CHANGEME scp $(project)-$(version).* \
		fedorahosted.org:$(project)
endif

clean:
	rm -rf $(project)-* tag-*
