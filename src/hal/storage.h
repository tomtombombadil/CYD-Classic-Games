// LittleFS on the internal flash partition. Shared by every module that
// saves something (touch calibration, display fixes, games, settings).
#pragma once

// Mounts LittleFS on first call (formatting a blank partition). Safe to call
// repeatedly. Returns false if the filesystem is unavailable; callers should
// then carry on with defaults rather than fail.
bool storage_begin();

// Make sure a top-level directory exists (e.g. "/games"). True if it does.
bool storage_mkdir(const char* dir);
