UPDATE `command`
SET `help` = CONCAT(
    'Syntax: .zpe dismiss\n\n',
    'Queue logout for bots added through ZPE. Bots still logging in will log out after login completes.')
WHERE `name` = 'zpe dismiss';
