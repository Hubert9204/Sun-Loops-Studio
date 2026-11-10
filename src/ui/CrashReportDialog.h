#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

#include "support/GitHub.h"

namespace sls
{
// The question asked at start-up after a crash (0.43.0): send an anonymous bug report to GitHub, look at it first, or ignore
// and delete it. "Send report" creates the issue through the GitHub API when a token is available (on a worker thread; the
// crash file is archived only after GitHub confirmed it with 201 Created), or opens GitHub's "new issue" page in the browser
// when there is none. Closing the window keeps the file, so the question comes back at the next start. Texts in Italian when
// the system language is Italian, else in English.
class CrashReportDialog : public juce::Component
{
public:
    struct Setup
    {
        std::filesystem::path crashFile;
        std::string title, body;  // the issue, already made anonymous
        std::string summary;      // one line: "SIGSEGV in sls::Player::render"
        github::Repo repo;
        std::string token;        // empty: the browser is used instead
        std::string userAgent;
        std::string apiBase = github::kApiBase;
        bool italian = false;
    };

    explicit CrashReportDialog (Setup setup);

    // Shows the dialog (modal); `whenClosed` runs after it has gone, however it was closed.
    static void show (Setup setup, std::function<void()> whenClosed);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    enum class State { Asking, Sending, Sent, Failed, InBrowser };

    void send();
    void sent (const github::IssueResult& result, bool archived);
    void openInBrowser();
    void deleteAndClose();
    void close();
    void togglePreview();
    void layout();
    void setState (State newState, const juce::String& statusText = {});
    const char* text (const char* english, const char* italian) const { return setup.italian ? italian : english; }

    Setup setup;
    State state = State::Asking;
    bool previewShown = false;
    juce::String issueUrl;

    juce::Label heading, message, status;
    juce::TextEditor preview;
    juce::TextButton sendButton, showButton, ignoreButton, browserButton, keepButton, openIssueButton;
};
} // namespace sls
