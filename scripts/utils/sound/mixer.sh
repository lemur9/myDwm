#!/bin/sh
# Kept for old launchers: the device page now shares the native audio panel.
dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd) || exit 1
exec "$dir/audioctl" panel devices
