#pragma once

#include <filesystem>

#include <juce_core/juce_core.h>

// Desktop integration on Linux (0.68.1): the project files (.slp) get the program's icon in file managers and open it on a
// double-click, also when the program runs as an AppImage (which installs nothing by itself). At start-up the program writes,
// in the user's own data folder (~/.local/share, or $XDG_DATA_HOME), only what is missing or out of date:
//   mime/packages/sun-loops-studio.xml                       the "Sun Loops Studio project" file type (*.slp)
//   icons/hicolor/<size>/mimetypes/application-x-sun-loops-studio-project.png and .../apps/sun-loops-studio.png
//   applications/sun-loops-studio.desktop                    the program, started from where it is now (the AppImage's path)
// then refreshes the desktop's caches (update-mime-database, update-desktop-database, gtk-update-icon-cache, kbuildsycoca) and,
// the first time only, makes the program the default for .slp files when nothing else is. A copy installed with
// `cmake --install` already has all of this, so it is left alone.
namespace sls::desktop
{
struct Setup
{
    std::filesystem::path dataHome; // ~/.local/share
    juce::String executable;        // what the menu entry starts (the AppImage file, or the program)
};

struct Result
{
    bool changed = false;           // something was written
    bool desktopFileCreated = false; // the menu entry did not exist before
    juce::StringArray written;      // the files written (for the log)
    juce::String error;
};

// Writes the files that are missing or different. Plain file work: no command is run (the tests call it).
Result install (const Setup& setup);

// Linux: works out the setup and runs install() and the cache refreshes on a background thread. Elsewhere: nothing.
void integrateInBackground();

// The .desktop file's text for an executable (quoted as the Desktop Entry specification asks).
juce::String desktopEntry (const juce::String& executable);
} // namespace sls::desktop
