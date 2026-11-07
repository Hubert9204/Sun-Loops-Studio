#include <cstdio>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/MainComponent.h"

class SunLoopsStudioApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
       #if JUCE_LINUX
        // JUCE draws through X11 (XWayland under Wayland): without a display it would crash, so say so and stop instead.
        if (juce::SystemStats::getEnvironmentVariable ("DISPLAY", {}).isEmpty())
        {
            std::fputs ("Sun Loops Studio: no graphical display (DISPLAY is not set). Start it from a desktop session.\n", stderr);
            setApplicationReturnValue (1);
            quit();
            return;
        }
       #endif
        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // "sun-loops-studio song.slp" (or a double-click on a project file) opens that project.
        for (const auto& argument : getCommandLineParameterArray())
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (argument.unquoted());
            if (file.existsAsFile() && sls::hasProjectExtension (file))
            {
                if (auto* main = mainWindow->getMainComponent())
                    main->openProjectFile (file);
                break;
            }
        }
    }

    void shutdown() override { mainWindow = nullptr; }

    // Closing the window (or the session ending) first asks about unsaved changes.
    void systemRequestedQuit() override
    {
        if (auto* main = mainWindow != nullptr ? mainWindow->getMainComponent() : nullptr)
            main->confirmDiscardChanges ([] { juce::JUCEApplication::quit(); });
        else
            quit();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff2a2d34), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new sls::MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1180, 600, 8192, 8192); // the toolbar needs about 1150 px
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        sls::MainComponent* getMainComponent() { return dynamic_cast<sls::MainComponent*> (getContentComponent()); }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (SunLoopsStudioApplication)
