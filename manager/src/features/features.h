#pragma once
#include <string>

namespace eam {
namespace features {

// The features that are part of the manager itself: things a person switches on if they need them, not separate addons. Each has one switch
// and, when it is on, a few options. The switch is kept in config.json under the feature's id, the same place the old standalone addons kept
// theirs, so a setting made before they were built in still applies.

struct Info {
    const char* id;
    const char* title;
    const char* summary;    // one line under the title
    const char* tooltip;    // the longer explanation, on the switch
};

int Count();
const Info& At(int index);

bool IsOn(int index);                   // the user's switch
void SetOn(int index, bool on);         // switches it and saves; starts or stops it right away where that can be done while running
bool NeedsRestart(int index);           // it is switched on but cannot start until Lossless Scaling does
std::string Status(int index);          // a short live line ("Passthrough ON"), or empty
void RenderOptions(int index);          // the feature's own options, drawn in the manager's window

void MoveOldSettings();                 // settings saved under a feature's id up to 0.7.4 move to its id now (once; Start does it)
void Start();                           // Lossless Scaling start-up: move settings saved under an old name, then start every feature that is switched on
void Stop();                            // shutdown

// The folders of the standalone addons these features once were. Such a folder in the addons folder is not listed or loaded, so the old
// and the built-in version can never both run.
bool IsRetiredAddonId(const std::string& folderName);

} // namespace features
} // namespace eam
