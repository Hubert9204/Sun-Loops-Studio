#include "support/DesktopIntegration.h"

#include <juce_graphics/juce_graphics.h>

#include <spdlog/spdlog.h>

#include "IconData.h"

namespace sls::desktop
{
namespace
{
constexpr const char* kMimeType = "application/x-sun-loops-studio-project";
constexpr const char* kMimeIcon = "application-x-sun-loops-studio-project";
constexpr const char* kAppIcon = "sun-loops-studio";
constexpr int kSizes[] { 16, 22, 24, 32, 48, 64, 128, 256 };

const char* mimeXml()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
           "  <mime-type type=\"application/x-sun-loops-studio-project\">\n"
           "    <comment>Sun Loops Studio project</comment>\n"
           "    <comment xml:lang=\"it\">Progetto di Sun Loops Studio</comment>\n"
           "    <glob pattern=\"*.slp\"/>\n"
           "    <icon name=\"application-x-sun-loops-studio-project\"/>\n"
           "  </mime-type>\n"
           "</mime-info>\n";
}

// Writes `data` to `file` unless the file already holds exactly that. True when it wrote.
bool writeIfDifferent (const juce::File& file, const juce::MemoryBlock& data, Result& result)
{
    if (file.existsAsFile())
    {
        juce::MemoryBlock old;
        if (file.loadFileAsData (old) && old == data)
            return false;
    }
    if (! file.getParentDirectory().createDirectory() || ! file.replaceWithData (data.getData(), data.getSize()))
    {
        result.error = "cannot write " + file.getFullPathName();
        return false;
    }
    result.changed = true;
    result.written.add (file.getFullPathName());
    return true;
}

bool writeText (const juce::File& file, const juce::String& text, Result& result)
{
    const auto utf8 = text.toUTF8();
    return writeIfDifferent (file, juce::MemoryBlock (utf8.getAddress(), utf8.sizeInBytes() - 1), result);
}

juce::MemoryBlock iconPng (int size)
{
    static const juce::Image original = juce::ImageFileFormat::loadFrom (IconData::app_icon_256_png, IconData::app_icon_256_pngSize);
    juce::MemoryBlock out;
    if (! original.isValid())
        return out;
    const auto image = size == original.getWidth() ? original : original.rescaled (size, size, juce::Graphics::highResamplingQuality);
    juce::MemoryOutputStream stream (out, false);
    juce::PNGImageFormat().writeImageToStream (image, stream);
    return out;
}

// Runs a desktop tool if it is installed (they are all optional), waiting at most 20 s.
void runTool (const juce::StringArray& command)
{
    const auto found = juce::File ("/usr/bin").getChildFile (command[0]).existsAsFile() || juce::File ("/bin").getChildFile (command[0]).existsAsFile()
                       || juce::File ("/usr/local/bin").getChildFile (command[0]).existsAsFile();
    if (! found)
        return;
    juce::ChildProcess process;
    if (process.start (command, 0))
    {
        process.waitForProcessToFinish (20000);
        spdlog::info ("Desktop integration: {} (exit {})", command.joinIntoString (" ").toStdString(), process.getExitCode());
    }
}

juce::String query (const juce::StringArray& command)
{
    if (! juce::File ("/usr/bin").getChildFile (command[0]).existsAsFile())
        return {};
    juce::ChildProcess process;
    if (! process.start (command))
        return {};
    const auto out = process.readAllProcessOutput().trim();
    process.waitForProcessToFinish (5000);
    return out;
}
} // namespace

juce::String desktopEntry (const juce::String& executable)
{
    // Inside a quoted argument the Desktop Entry specification escapes " ` $ and \ with a backslash.
    juce::String quoted;
    for (const auto c : executable)
    {
        if (c == '"' || c == '`' || c == '$' || c == '\\')
            quoted << '\\';
        quoted << juce::String::charToString (c);
    }
    return "[Desktop Entry]\n"
           "Type=Application\n"
           "Name=Sun Loops Studio\n"
           "GenericName=Digital Audio Workstation\n"
           "Comment=Loop-based music production: channel rack, piano roll, playlist and mixer\n"
           "Exec=\"" + quoted + "\" %f\n"
           "MimeType=" + juce::String (kMimeType) + ";\n"
           "Icon=" + juce::String (kAppIcon) + "\n"
           "Terminal=false\n"
           "Categories=AudioVideo;Audio;Midi;Sequencer;Music;\n"
           "Keywords=music;daw;sequencer;piano roll;beat;\n"
           "StartupWMClass=Sun Loops Studio\n"
           "X-Sun-Loops-Studio-Generated=true\n";
}

Result install (const Setup& setup)
{
    Result result;
    const juce::File data (juce::String (setup.dataHome.string()));
    writeText (data.getChildFile ("mime/packages/sun-loops-studio.xml"), mimeXml(), result);
    for (const int size : kSizes)
    {
        const auto png = iconPng (size);
        if (png.isEmpty())
        {
            result.error = "the program's icon could not be read";
            break;
        }
        const auto folder = data.getChildFile ("icons/hicolor").getChildFile (juce::String (size) + "x" + juce::String (size));
        writeIfDifferent (folder.getChildFile ("mimetypes").getChildFile (juce::String (kMimeIcon) + ".png"), png, result);
        writeIfDifferent (folder.getChildFile ("apps").getChildFile (juce::String (kAppIcon) + ".png"), png, result);
    }
    if (setup.executable.isNotEmpty())
    {
        const auto desktopFile = data.getChildFile ("applications/sun-loops-studio.desktop");
        const bool existed = desktopFile.existsAsFile();
        // A menu entry the user (or another tool) made is theirs: only ours is kept up to date.
        if (! existed || desktopFile.loadFileAsString().contains ("X-Sun-Loops-Studio-Generated=true"))
            if (writeText (desktopFile, desktopEntry (setup.executable), result) && ! existed)
                result.desktopFileCreated = true;
    }
    return result;
}

void integrateInBackground()
{
   #if JUCE_LINUX
    const auto appImage = juce::SystemStats::getEnvironmentVariable ("APPIMAGE", {});
    if (appImage.isEmpty())
    {
        // Installed with `cmake --install` (or a package): the system already knows the file type.
        for (const char* prefix : { "/usr/share", "/usr/local/share" })
            if (juce::File (prefix).getChildFile ("mime/packages/sun-loops-studio.xml").existsAsFile())
                return;
    }
    Setup setup;
    const auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    setup.dataHome = xdg.isNotEmpty() ? std::filesystem::path (xdg.toStdString())
                                      : std::filesystem::path (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                                                   .getChildFile (".local/share").getFullPathName().toStdString());
    setup.executable = appImage.isNotEmpty() ? appImage : juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    juce::Thread::launch ([setup]
    {
        const auto result = install (setup);
        if (result.error.isNotEmpty())
            spdlog::warn ("Desktop integration: {}", result.error.toStdString());
        if (! result.changed)
            return;
        spdlog::info ("Desktop integration: {} file(s) written in {}", result.written.size(), setup.dataHome.string());
        const auto data = juce::String (setup.dataHome.string());
        runTool ({ "update-mime-database", data + "/mime" });
        runTool ({ "update-desktop-database", data + "/applications" });
        runTool ({ "gtk-update-icon-cache", "-f", "-t", "-q", data + "/icons/hicolor" });
        runTool ({ "kbuildsycoca6", "--noincremental" });
        runTool ({ "kbuildsycoca5", "--noincremental" });
        // The first time: open .slp files with this program, unless another program was already chosen for them.
        if (result.desktopFileCreated && query ({ "xdg-mime", "query", "default", kMimeType }).isEmpty())
            runTool ({ "xdg-mime", "default", "sun-loops-studio.desktop", kMimeType });
    });
   #endif
}
} // namespace sls::desktop
