# Znapshot Playerbot Enhancer

ZPBE adds `.zpbe fill` and `.zpbe populate` commands for groups and raids on AzerothCore servers
that run `mod-playerbots`.

The requested number is the final group size, including the player and any
members already present. The default is 5. If the level is omitted, ZPBE uses the
rounded average level of connected human group members (or the command issuer's
level when there are none). If supplied, the level must be between 1 and the
server's maximum player level. Both commands use a common tank/healer/DPS
composition for the group size. A bot's selected specialization can differ from
the intended role.

`.zpbe fill [5|10|25|40] [level]` uses only existing AddClass bots. If too few
matching bots are available, the group remains partially filled.

`.zpbe populate [5|10|25|40] [level]` first uses existing matching AddClass
bots, then creates missing bot characters in free slots on existing AddClass
accounts and logs them in. It does not create new accounts and creates at most
10 characters per command. Newly generated characters may be the wrong faction;
those characters remain in the AddClass pool for later use by that faction.

`.zpbe status` shows the current group composition and ZPBE's tracked bot count.
`.zpbe dismiss` logs out bots added through ZPBE. If a bot is still logging in,
ZPBE logs it out after the login completes.

The module installs help text for `.help zpbe fill`, `.help zpbe populate`, `.help zpbe status`, and
`.help zpbe dismiss` through its world database update. Include the module's
`include.sh` when assembling module SQL and apply the assembled world updates.

True LFG queue groups, battleground groups, and battlefield groups are refused.
Use this command for a regular party or raid that you are assembling manually.

Automatic logout on group disband is off by default. Enable it in
`ZnapshotPlayerbotEnhancer.conf` with
`ZnapshotPlayerbotEnhancer.AutoDismissOnDisband = 1`. It only affects bots
tracked as having been added by ZPBE.

## Dependency and build mode

This module depends on `mod-playerbots`, including its AddClass account pool and
bot login APIs. Keep both modules enabled and build them as static modules, for
example with `MODULES=static`. Then rebuild and restart `worldserver`.

Copy this directory into `modules/` alongside `mod-playerbots`, enable it in the
AzerothCore module selection, and rebuild the server. Include `include.sh` when
assembling module SQL, apply the world updates, then run `.reload command` or
restart `worldserver` to load the help text.

## Use

```text
.zpbe fill
.zpbe fill 10
.zpbe fill 25
.zpbe fill 40
.zpbe fill 40 70
.zpbe populate
.zpbe populate 5 70
.zpbe populate 25 70
.zpbe status
.zpbe dismiss
```
