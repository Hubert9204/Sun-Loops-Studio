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
// Windows (0.75.0): the installer registers the .slp file type itself. A copy started from the zip (or one whose registered
// program file is gone - moved or uninstalled) registers it for the user at start-up, in HKEY_CURRENT_USER\Software\Classes:
// .slp -> SunLoopsStudio.Project, its icon (the program's) and "open" (the program with the file). A file type that already opens
// with an existing program - the installed copy, or another program the user chose - is left alone.
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

// Windows (0.75.0): the "open" command of the .slp file type for a program ("C:\...\Sun Loops Studio.exe" "%1"), and the program
// file an "open" command starts (quoted or not; empty when there is none).
juce::String windowsOpenCommand (const juce::String& executable);
juce::String executableOfCommand (const juce::String& command);
} // namespace sls::desktop
