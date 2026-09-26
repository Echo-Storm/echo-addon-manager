#pragma once
// The runtime files the addons use, as a short fixed list at the bottom of the addon list: one line each, a tick when the file is loaded
// (the addon runs on it now) and a cross when it is not; the name, the version, and "unsigned" or "modified" (signed, then changed) when
// the file is not as its maker signed it; + to choose another file. Hovering a line shows the rest (file, maker, signer, SHA-256, which
// addon uses it).
#include "../../addon/runtime_files.h"
#include <vector>

namespace eam {
namespace widgets {

struct RuntimeAction { int index = -1; enum Kind { None, Choose } kind = None; };

// The list at the bottom of the window it is drawn in (right under what came before when there is no room below), a separator above it.
RuntimeAction RuntimeListAtBottom(const std::vector<RuntimeFile>& rows);

} // namespace widgets
} // namespace eam
