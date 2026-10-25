# Znapshot Playerbot Enhancer

ZPE adds `.zpe fill [5|10|25|40] [level]` for groups and raids on AzerothCore servers
that run `mod-playerbots`.

The requested number is the final group size, including the player and any
members already present. The default is 5. If the level is omitted, ZPE uses the
rounded average level of connected human group members (or the command issuer's
level when there are none). If supplied, the level must be between 1 and the
server's maximum player level. ZPE selects existing class bots at that exact
level using a common tank/healer/DPS composition for the group size. A bot's
selected specialization can differ from the intended role. ZPE does not create
new bot characters; if too few existing bots are available at that level, the
group remains partially filled.

`.zpe status` shows the current group composition and ZPE's tracked bot count.
`.zpe dismiss` logs out bots added through ZPE. If a bot is still logging in,
ZPE logs it out after the login completes.

The module installs help text for `.help zpe fill`, `.help zpe status`, and
`.help zpe dismiss` through its world database update. Include the module's
`include.sh` when assembling module SQL and apply the assembled world updates.

True LFG queue groups, battleground groups, and battlefield groups are refused.
Use this command for a regular party or raid that you are assembling manually.

Automatic logout on group disband is off by default. Enable it in
`ZnapshotPlayerbotEnhancer.conf` with
`ZnapshotPlayerbotEnhancer.AutoDismissOnDisband = 1`. It only affects bots
tracked as having been added by ZPE.

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
.zpe fill
.zpe fill 10
.zpe fill 25
.zpe fill 40
.zpe fill 40 70
.zpe status
.zpe dismiss
```
