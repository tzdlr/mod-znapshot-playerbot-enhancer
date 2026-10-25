DELETE FROM `command`
WHERE `name` = 'zpbe populate';

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('zpbe populate', 0, CONCAT(
    'Syntax: .zpbe populate [5|10|25|40] [level]\n\n',
    'Fill your group to the requested total size, using matching existing AddClass bots first. ',
    'If the pool has too few matching bots, ZPBE creates characters in free slots on existing AddClass accounts ',
    '(up to 10 new characters per command). If level is omitted, ZPBE uses the rounded average level of ',
    'connected human group members, or your level when no other human group members are connected.'));
