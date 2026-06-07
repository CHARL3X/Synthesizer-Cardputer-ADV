#pragma once

namespace splash {
// Wordmark + a boot chime that IS the instrument: a single gliding note,
// played through the synth engine itself. Call after audio::begin().
// Returns true if BACKSPACE was pressed during the splash — the factory
// reset request. (Must be a press DURING the window: the ADV's TCA8418
// keyboard is event-driven, so a key held from power-on is invisible.)
bool run();
}
