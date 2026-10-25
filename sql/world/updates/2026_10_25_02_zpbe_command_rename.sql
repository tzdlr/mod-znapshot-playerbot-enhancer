DELETE FROM `command`
WHERE `name` IN (
    'zpe fill', 'zpe status', 'zpe dismiss',
    'zpbe fill', 'zpbe status', 'zpbe dismiss');

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('zpbe fill', 0, CONCAT(
    'Syntax: .zpbe fill [5|10|25|40] [level]\n\n',
    'Fill your group to the requested total size. If level is omitted, ZPBE uses the rounded average level ',
    'of connected human group members, or your level when no other human group members are connected. ',
    'If specified, bots must have exactly that level. Only existing bots are used.')),
('zpbe status', 0, CONCAT(
    'Syntax: .zpbe status\n\n',
    'Show your current group size and role counts, the number of bots tracked by ZPBE, pending bot logins, ',
    'and the auto-dismiss setting.')),
('zpbe dismiss', 0, CONCAT(
    'Syntax: .zpbe dismiss\n\n',
    'Queue logout for bots added through ZPBE. Bots still logging in will log out after login completes.'));
