#pragma once
// The runtime files the addons use, as a short fixed list at the bottom of the addon list: one line each, a green tick for a file signed by
// its maker, amber for one that is not signed or was changed after signing, grey for one that is not there; the name and version; + to
// choose another file and a reset arrow to go back to the one the addon ships with. Hovering a line shows the rest (file, maker, signer,
// SHA-256, which addon uses it).
#include "../../addon/runtime_files.h"
#include <vector>

namespace eam {
namespace widgets {

struct RuntimeAction { int index = -1; enum Kind { None, Choose, Reset } kind = None; };

// The list at the bottom of the window it is drawn in (right under what came before when there is no room below), a separator above it.
RuntimeAction RuntimeListAtBottom(const std::vector<RuntimeFile>& rows);

} // namespace widgets
} // namespace eam
