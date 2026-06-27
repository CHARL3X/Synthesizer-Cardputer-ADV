// The SD patch-library browser — recall the sounds you saved to the card.
// A modal screen (owns the canvas while open, like the Help screen). Lists the
// .gpat files in the library, loads one into the live sound on select, and can
// delete one. Loading lands live (not onto a slot) so it auditions immediately
// and the player can then shift-save it onto fn+q..p to keep it fast.
#pragma once
#include <M5Cardputer.h>

namespace sdbrowser {

// Run the browser modal. Returns true if the player loaded a patch (its name is
// copied into loadedName, capacity cap). Handles its own delete confirm and the
// SD-unavailable / empty-library messaging. Returns false on plain back-out.
bool run(M5Canvas& canvas, char* loadedName, int cap);

}  // namespace sdbrowser
