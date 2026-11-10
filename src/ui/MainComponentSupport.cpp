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
        crash::symbolize (*report, std::filesystem::read_symlink ("/proc/self/exe", error)); // the same build: name every frame
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
} // namespace sls
