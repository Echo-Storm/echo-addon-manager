// The HUD areas drawn on a picture of the game: a snapshot of the frame as it is shown (screenshot.h), and the protected areas over it, made and
// changed with the mouse. Drag on the picture to add an area, drag an area to move it, drag a corner to resize it, right-click an area to remove
// it. Without a snapshot the same editing works on an empty box of the frame's shape.
#pragma once
#include "engine/nr_engine.h"

namespace nr {

// Draws the editor at the full width of the panel. True when an area was added, moved, resized or removed (when the mouse was let go).
bool DrawHudEditor(NrParams& p);
void DropHudSnapshot();   // give the snapshot's image back (the addon is shutting down)

} // namespace nr
