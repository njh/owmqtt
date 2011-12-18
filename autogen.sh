#!/bin/sh
#

run_cmd() {
    echo running $* ...
    if ! $*; then
			echo failed!
			exit 1
    fi
}

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

cd "$srcdir"
DIE=0

# Because git doesn't support empty directories
if [ ! -d "$srcdir/build-scripts" ]; then
	mkdir "$srcdir/build-scripts"
fi 

run_cmd aclocal
run_cmd autoheader
run_cmd automake --add-missing --copy
run_cmd autoconf


echo
echo "Now type './configure' to configure owmqtt"
echo
