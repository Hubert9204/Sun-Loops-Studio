// Part of MainComponent (0.43.0): the crash report offered at start-up, the update check, the GitHub token for reports, the
// audio facts written into crash files, and the status line copied to the session log.
#include "ui/MainComponent.h"

#include <sys/stat.h>

#include <spdlog/spdlog.h>

#include "support/CrashHandler.h"
#include "support/CrashReport.h"
#include "support/Logging.h"
#include "ui/CrashReportDialog.h"
#include "ui/TextPrompt.h"

#ifndef SLS_GITHUB_REPO
 #define SLS_GITHUB_REPO "Hubert9204/Sun-Loops-Studio"
#endif
#ifndef SLS_CRASH_REPORT_TOKEN
 #define SLS_CRASH_REPORT_TOKEN ""
#endif
#ifndef JUCE_APPLICATION_VERSION_STRING
 #define JUCE_APPLICATION_VERSION_STRING "0.0.0"
#endif

namespace sls
{
namespace
{
constexpr const char* kTokenKey = "crashReport.githubToken";
constexpr const char* kCheckUpdatesKey = "updates.checkAutomatically";
constexpr const char* kSkippedVersionKey = "updates.skippedVersion";
constexpr const char* kDiscordInvite = "https://discord.gg/VwzdD6tRNB"; // the Sun Loops community (permanent invite, 0.55.1)

github::Repo repository()
{
    if (const auto r = github::Repo::parse (SLS_GITHUB_REPO))
        return *r;
    return { "Hubert9204", "Sun-Loops-Studio" };
}

std::string userAgent() { return std::string ("SunLoopsStudio/") + JUCE_APPLICATION_VERSION_STRING; }

// SLS_GITHUB_API points both calls at another server (a test server, or a GitHub Enterprise); normally api.github.com.
std::string apiBase()
{
    const auto custom = juce::SystemStats::getEnvironmentVariable ("SLS_GITHUB_API", {}).trim();
    return custom.isNotEmpty() ? custom.toStdString() : std::string (github::kApiBase);
}

bool systemIsItalian()
{
    const auto language = juce::SystemStats::getUserLanguage().toLowerCase();
    const auto lang = juce::SystemStats::getEnvironmentVariable ("LANG", {}).toLowerCase();
    return language.startsWith ("it") || lang.startsWith ("it");
}
} // namespace

std::string MainComponent::githubToken() const
{
    if (settings != nullptr)
        if (const auto saved = settings->getValue (kTokenKey).trim(); saved.isNotEmpty())
            return saved.toStdString();
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("SLS_GITHUB_TOKEN", {}).trim(); env.isNotEmpty())
        return env.toStdString();
    return SLS_CRASH_REPORT_TOKEN;
}

void MainComponent::askForGitHubToken()
{
    promptForText (*this, "GitHub token for crash reports",
                   "Crash reports are created as issues in " + juce::String (repository().path())
                       + ". Paste a GitHub token that may create issues there (a fine-grained token with \"Issues: Read and write\" on that "
                         "repository only), or leave the box empty to send reports from the browser instead. The token is kept in the "
                         "settings file on this computer.",
                   settings != nullptr ? settings->getValue (kTokenKey) : juce::String(), "Save",
                   [this] (const juce::String& typed)
                   {
                       if (settings == nullptr)
                           return;
                       settings->setValue (kTokenKey, typed.trim());
                       settings->saveIfNeeded();
                       ::chmod (settings->getFile().getFullPathName().toRawUTF8(), 0600); // the token: readable by this user only
                       statusLabel.setText (typed.trim().isEmpty() ? "No GitHub token: crash reports open in the browser"
                                                                   : "GitHub token saved for crash reports",
                                            juce::dontSendNotification);
                   });
}

// Called from startSession: a crash file from the last session is offered as a report first, then `then` runs (the offer to
// open the autosaved project).
bool MainComponent::offerCrashReport (std::function<void()> then)
{
    const auto file = crash::crashFile (logging::stateDirectory());
    std::error_code error;
    if (! std::filesystem::exists (file, error))
        return false;
    auto report = crash::load (file);
    if (! report)
    {
        spdlog::warn ("A crash file that could not be read was deleted: {}", file.string());
        std::filesystem::remove (file, error);
        return false;
    }
    if (report->version == JUCE_APPLICATION_VERSION_STRING)
        crash::symbolize (*report, crash::executablePath()); // the same build: name every frame
    spdlog::warn ("The last session crashed: {} in {}", report->signalName, report->signature());

    CrashReportDialog::Setup setup;
    setup.crashFile = file;
    setup.title = crash::issueTitle (*report);
    setup.body = crash::issueBody (*report, crash::Redaction::current());
    setup.summary = report->signalName + " - " + report->description() + "\n" + report->signature();
    setup.repo = repository();
    setup.token = githubToken();
    setup.userAgent = userAgent();
    setup.apiBase = apiBase();
    setup.italian = systemIsItalian();
    CrashReportDialog::show (std::move (setup), std::move (then));
    return true;
}

void MainComponent::checkForUpdates (bool manual)
{
    if (updateChecker == nullptr)
    {
        updates::UpdateChecker::Options options;
        options.currentVersion = JUCE_APPLICATION_VERSION_STRING;
        options.repo = repository();
        options.userAgent = userAgent();
        options.timeoutSeconds = 5;
        options.apiBase = apiBase();
        updateChecker = std::make_unique<updates::UpdateChecker> (options, [] (std::function<void()> f) { juce::MessageManager::callAsync (std::move (f)); });
    }
    const bool automatic = settings == nullptr || settings->getBoolValue (kCheckUpdatesKey, true);
    updateChecker->setEnabled (manual || automatic);
    const bool started = updateChecker->start ([safe = juce::Component::SafePointer<MainComponent> (this), manual] (const updates::Result& result)
    {
        spdlog::info ("Update check: {}", result.message);
        if (safe != nullptr)
            safe->showUpdateResult (result, manual);
    });
    if (manual)
        statusLabel.setText (started ? "Checking for updates..." : "An update check is already running", juce::dontSendNotification);
}

void MainComponent::showUpdateResult (const updates::Result& result, bool manual)
{
    if (result.status != updates::Status::UpdateAvailable)
    {
        if (manual)
        {
            statusLabel.setText (juce::String (result.message), juce::dontSendNotification);
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Check for updates", juce::String (result.message));
        }
        return;
    }
    const auto info = result.info;
    if (! manual && settings != nullptr && settings->getValue (kSkippedVersionKey) == juce::String (info.version))
        return; // the user chose to skip this one
    // An automatic check never opens its window over another question (the crash report, the recovered project, a file
    // chooser): it waits until they have been answered.
    if (auto* modal = juce::ModalComponentManager::getInstance(); modal != nullptr && modal->getNumModalComponents() > 0)
    {
        statusLabel.setText ("Sun Loops Studio " + juce::String (info.version) + " is available", juce::dontSendNotification);
        juce::Timer::callAfterDelay (1000, [safe = juce::Component::SafePointer<MainComponent> (this), result, manual]
        {
            if (safe != nullptr)
                safe->showUpdateResult (result, manual);
        });
        return;
    }

    auto* window = new juce::AlertWindow ("Sun Loops Studio " + juce::String (info.version) + " is available",
                                          "You have " + juce::String (JUCE_APPLICATION_VERSION_STRING) + "."
                                              + (info.changelog.empty() ? juce::String() : "\n\nWhat is new:\n" + juce::String::fromUTF8 (info.changelog.c_str())),
                                          juce::MessageBoxIconType::InfoIcon, this);
    window->addButton ("Download", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Later", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->addButton ("Skip this version", 2);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this), info] (int choice)
    {
        if (choice == 1)
            juce::URL (juce::String (info.url)).launchInDefaultBrowser();
        else if (choice == 2 && safe != nullptr && safe->settings != nullptr)
        {
            safe->settings->setValue (kSkippedVersionKey, juce::String (info.version));
            safe->settings->saveIfNeeded();
        }
    }),
                             true);
    statusLabel.setText ("Sun Loops Studio " + juce::String (info.version) + " is available", juce::dontSendNotification);
}

bool MainComponent::checksForUpdatesAutomatically() const
{
    return settings == nullptr || settings->getBoolValue (kCheckUpdatesKey, true);
}

void MainComponent::setCheckForUpdatesAutomatically (bool shouldCheck)
{
    if (settings != nullptr)
    {
        settings->setValue (kCheckUpdatesKey, shouldCheck);
        settings->saveIfNeeded();
    }
    if (updateChecker != nullptr)
        updateChecker->setEnabled (shouldCheck);
    spdlog::info ("Automatic update check {}", shouldCheck ? "on" : "off");
}

void MainComponent::showLogFolder()
{
    const juce::File folder (juce::String (logging::stateDirectory().string()));
    folder.getChildFile ("logs").createDirectory();
    folder.getChildFile ("logs").revealToUser();
}

// The audio facts a crash report needs, kept up to date in the crash handler and written to the log when they change.
void MainComponent::noteAudioDevice()
{
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    const auto driver = device != nullptr ? device->getTypeName() : juce::String ("none");
    const auto name = device != nullptr ? device->getName() : juce::String ("none");
    const auto rate = device != nullptr ? juce::String (device->getCurrentSampleRate(), 0) : juce::String ("0");
    const auto buffer = device != nullptr ? juce::String (device->getCurrentBufferSizeSamples()) : juce::String ("0");
    crash::setContext ({ { "audio_driver", driver.toStdString() }, { "audio_device", name.toStdString() },
                         { "sample_rate", rate.toStdString() }, { "buffer_size", buffer.toStdString() } });
    spdlog::info ("Audio: {} / {}, {} Hz, buffer {}", driver.toStdString(), name.toStdString(), rate.toStdString(), buffer.toStdString());
}

// Called by the timer: every new text in the status line goes to the session log, so a crash report shows what was done last.
void MainComponent::logStatusChanges()
{
    const auto text = statusLabel.getText();
    if (text == lastLoggedStatus)
        return;
    lastLoggedStatus = text;
    if (text.isNotEmpty())
        spdlog::info ("Status: {}", text.toStdString());
}
// The Help button (0.55.0): what used to be under File > Updates and crash reports, with the community and the About window.
void MainComponent::showHelpMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Helios: ask how to do something  (F1)");
    menu.addItem (2, "User guide (on GitHub)");
    menu.addSeparator();
    menu.addItem (3, "Join the Sun Loops Discord");
    menu.addItem (4, "Sun Loops Studio on GitHub");
    menu.addItem (5, "Report a problem or suggest an idea (GitHub)");
    menu.addSeparator();
    menu.addItem (10, "Check for updates now");
    menu.addItem (11, "Check for updates at start-up", true, checksForUpdatesAutomatically());
    menu.addSeparator();
    menu.addItem (12, githubToken().empty() ? "GitHub token for crash reports... (none: reports open in the browser)"
                                            : "GitHub token for crash reports... (set)");
    menu.addItem (13, "Show the session log folder");
    menu.addSeparator();
    menu.addItem (20, "About Sun Loops Studio");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&helpButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            const juce::String github = "https://github.com/" + juce::String (repository().path());
                            switch (result)
                            {
                                case 1: safe->openHelios(); break;
                                case 2: juce::URL (github + "/blob/main/docs/" + (systemIsItalian() ? "user-guide.it.md" : "user-guide.md")).launchInDefaultBrowser(); break;
                                case 3: openDiscord(); break;
                                case 4: juce::URL (github).launchInDefaultBrowser(); break;
                                case 5: juce::URL (github + "/issues/new").launchInDefaultBrowser(); break;
                                case 10: safe->checkForUpdates (true); break;
                                case 11: safe->setCheckForUpdatesAutomatically (! safe->checksForUpdatesAutomatically()); break;
                                case 12: safe->askForGitHubToken(); break;
                                case 13: safe->showLogFolder(); break;
                                case 20: safe->showAbout(); break;
                                default: break;
                            }
                        });
}

void MainComponent::openDiscord()
{
    juce::URL (kDiscordInvite).launchInDefaultBrowser();
}

void MainComponent::showAbout()
{
    const juce::String github = "https://github.com/" + juce::String (repository().path());
    auto* window = new juce::AlertWindow ("Sun Loops Studio " + juce::String (JUCE_APPLICATION_VERSION_STRING),
                                          "Loop-based music production for Linux.\n\n"
                                          "Developed by Hubert9204 with the help of AI.\n"
                                          + github + "\n\n"
                                          "Free software under the GNU Affero General Public License, version 3. Built with JUCE 8. "
                                          "SunBurst and HexForge are the program's own synthesizers. The built-in recordings keep "
                                          "their licenses: Salamander Grand Piano (CC BY 3.0), Growlybass (CC0), DrumGizmo kits "
                                          "(CC BY 4.0).\n\n"
                                          "Join the community on Discord to ask questions, report problems and share your music.",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addButton ("GitHub page", 1);
    window->addButton ("Join Discord", 2);
    window->addButton ("Close", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([github] (int choice)
    {
        if (choice == 1)
            juce::URL (github).launchInDefaultBrowser();
        else if (choice == 2)
            openDiscord();
    }),
                             true);
}

} // namespace sls
