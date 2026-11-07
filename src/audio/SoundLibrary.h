#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio/SamplerProgram.h"

namespace sls
{
// The sampled sound library built into the program (0.22.0): recorded instruments and drum kits from free libraries, trimmed and
// encoded as Ogg Vorbis by the scripts in tools/sounds (which say exactly what was changed), in resources/sounds/library. The build
// packs the folder into one zip archive that is linked into the program, so nothing has to be installed next to it.
//
// A sound of the library has the id "lib:<path in the library without .ogg>" ("lib:piano/n060-3") and, like every built-in sound,
// the source "builtin:<id>". A program (a multi-sample instrument, programs/<id>.json in the library) has the source
// "builtin:program:<id>". Decoded audio is shared while something uses it and decoded again after that (so a piano nobody plays
// does not keep 200 MB of memory).

struct LibraryProgramInfo
{
    std::string id;     // "grand-piano"
    std::string name;   // "Grand Piano"
    std::string credit; // where the recordings come from and their license
    std::string category; // "Keys", "Bass", ...: where the browser files it
};

struct LibraryPad
{
    std::string name;
    std::vector<std::pair<std::string, int>> layers; // (library id, lowest velocity), softest first
    int chokeGroup = 0;
    float pan = 0.0f, volume = 1.0f;
};

struct LibraryKitInfo
{
    std::string id, name, credit;
    std::vector<LibraryPad> pads; // at most 16
};

// False only when the program was built without the library (the archive is empty or unreadable).
bool soundLibraryAvailable();

bool isLibraryId (const std::string& id);                                   // "lib:..."
std::shared_ptr<const SampleData> makeLibrarySample (const std::string& id); // nullptr if unknown or unreadable
std::string librarySampleTitle (const std::string& id);                     // "Kick 4" style name of a sound

const std::vector<LibraryProgramInfo>& libraryPrograms();
const std::vector<LibraryKitInfo>& libraryKits();

constexpr const char* kProgramSourcePrefix = "builtin:program:";
bool isLibraryProgramSource (const std::string& source);

// The program with every zone decoded (shared while in use), or nullptr and a message. `settings` (if not null) receives the
// sampler settings the program is meant to be played with (envelope, release).
std::shared_ptr<const SamplerProgram> makeLibraryProgram (const std::string& id, SamplerSettings* settings, std::string& error);
SamplerSettings libraryProgramSettings (const std::string& id);
} // namespace sls
