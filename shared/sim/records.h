#pragma once
// The player's records on disk: one small text file both builds read and
// write the same way (the Web build in $HOME, the Mac build in the project's
// Saved folder).
#include "sim.h"

// The records in `path`, or none if it cannot be read. Fields were appended
// over time; a file missing the later ones reads them as 0.
Records loadRecords(const char *path);
// False if `path` cannot be written.
bool saveRecords(const char *path, const Records &b);
