UPDATE `command`
SET `help` = CONCAT(
    'Syntax: .zpbe populate [5|10|25|40] [level]\n\n',
    'Fill your group to the requested total size, using matching existing AddClass bots first. ',
    'If needed, offline AddClass bots of a suitable class can be re-leveled and fully randomized. ',
    'If no suitable characters are available, ZPBE creates characters in free slots on existing AddClass accounts ',
    '(up to 10 new characters per command). Re-leveling resets quest progress, skills, spells, talents, and inventory. ',
    'If level is omitted, ZPBE uses the rounded average level of connected human group members, ',
    'or your level when no other human group members are connected.')
WHERE `name` = 'zpbe populate';
