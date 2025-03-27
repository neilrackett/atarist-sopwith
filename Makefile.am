AUX_DIST_GEN =                          \
        $(ac_aux_dir)/install-sh        \
        $(ac_aux_dir)/missing

# The `make distcheck` command does a simulated install to check that
# `make install` works correctly; we need to pick a different path for
# the high scores directory, otherwise we will likely get permissions
# errors with the default path:
AM_DISTCHECK_CONFIGURE_FLAGS = \
	"--with-hiscores-path=$$dc_install_base/hiscores"

DOC_FILES=            \
        COPYING.md    \
        FAQ.md        \
        NEWS.md       \
        README.md     \
        PHILOSOPHY.md

EXTRA_DIST = $(AUX_DIST_GEN) $(DOC_FILES) \
             embuild.sh \
             icon.png \
             maps/README.md maps/original.sop maps/empty.sop \
             maps/tank_strike.sop maps/triplane.sop maps/bogey.sop \
             maps/new_targets.sop maps/doghouse.sop

MAINTAINERCLEANFILES = $(AUX_DIST_GEN)

docdir=$(prefix)/share/doc/@PACKAGE@
SUBDIRS=src doc pkg

