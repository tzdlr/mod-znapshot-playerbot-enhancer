DELETE FROM `command`
WHERE `name` IN ('zpe fill', 'zpe status', 'zpe dismiss');

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('zpe fill', 0, CONCAT(
    'Syntax: .zpe fill [5|10|25|40] [level]\n\n',
    'Fill your group to the requested total size. If level is omitted, ZPE uses the rounded average level ',
    'of connected human group members, or your level when no other human group members are connected. ',
    'If specified, bots must have exactly that level. Only existing bots are used.')),
('zpe status', 0, CONCAT(
    'Syntax: .zpe status\n\n',
    'Show your current group size and role counts, the number of bots tracked by ZPE, pending bot logins, ',
    'and the auto-dismiss setting.')),
('zpe dismiss', 0, CONCAT(
    'Syntax: .zpe dismiss\n\n',
    'Queue logout for bots added through ZPE. Bot logins still in progress are cancelled.'));
