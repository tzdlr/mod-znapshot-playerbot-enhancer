# ZPBE Toolbar

Small movable toolbar for the ZPBE chat commands on WoW 3.3.5a (interface 30300).

Copy the `ZPBE` folder to `World of Warcraft/Interface/AddOns/`, enable **ZPBE Toolbar** on the character selection screen, and use `/zpbeui` to show or hide it. Drag the panel to move it. Closing the panel leaves a small group icon at its last position; click it to reopen the toolbar.

- **Gruppe auffüllen** submits `.zpbe populate <size> [level]`; it uses suitable existing bots first, then may re-level or create bot characters in free slots on existing AddClass accounts.
- **Status** prepares `.zpbe status`.
- **Bots entlassen** prepares `.zpbe dismiss`.

The size dropdown offers 5, 10, 25, or 40. The optional level must be between 1 and 80. Buttons submit the selected ZPBE command directly through the client's chat edit box. If that client build does not expose the submit function, the command is left in the chat input for manual confirmation.
