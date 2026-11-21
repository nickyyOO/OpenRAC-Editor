@tool
extends RefCounted
## The Collision dock's help page (its "How to edit collision" button). editor/LEVEL_MODDING.md,
## "Collision", says the same for the web.

# =============================================================================
# CONSTANTS
# =============================================================================

## The page, in BBCode.
const TEXT: String = """[b]How to edit collision[/b]

Collision is what Ratchet stands on and bumps into, and what shots hit.

[b]Look.[/b] Turn on [i]Show Collision[/i]. Each colour is a ground type. Yellow walls block only Ratchet.

[b]Pick.[/b] Turn on [i]Pick Faces[/i] and click a face. Shift-click adds or removes one. Alt-click picks the whole patch. Drag to pick everything inside a box.

[b]Change.[/b] Choose a ground type and footstep sound, then press [i]Repaint[/i]. [i]Delete[/i] removes the picked faces. [i]Move[/i] shifts them.

[b]Add.[/b] Click where it goes, set a size, then press [i]Add Floor[/i] or [i]Add Box[/i]. To make your own model solid, select it and press [i]Collision From Model[/i].

[b]Object collision.[/b] Select an object (a crate, a door, an enemy) and press [i]Edit Its Type[/i]. A change there changes every object of that type in this level.

[b]Try it.[/b] Every change can be undone with Ctrl+Z and is saved with Ctrl+S. Press [i]Play Level[/i] to try it in the game. [i]Check Limits[/i] tells you if something is too big."""
