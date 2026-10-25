#!/usr/bin/env bash

MOD_ZNAPSHOT_PLAYERBOT_ENHANCER_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )/" && pwd )"

source "$MOD_ZNAPSHOT_PLAYERBOT_ENHANCER_ROOT/conf/conf.sh.dist"

if [ -f "$MOD_ZNAPSHOT_PLAYERBOT_ENHANCER_ROOT/conf/conf.sh" ]; then
    source "$MOD_ZNAPSHOT_PLAYERBOT_ENHANCER_ROOT/conf/conf.sh"
fi
