#include "ui/CrashReportDialog.h"

#include <system_error>
#include <thread>

#include <spdlog/spdlog.h>

#include "support/CrashReport.h"

namespace sls
{
namespace
{
constexpr int kWidth = 680;
constexpr int kCollapsedHeight = 250;
constexpr int kExpandedHeight = 620;
} // namespace

CrashReportDialog::CrashReportDialog (Setup s) : setup (std::move (s))
{
    heading.setText (juce::String::fromUTF8 (text ("Sun Loops Studio closed unexpectedly", "Sun Loops Studio si è chiuso in modo anomalo")),
                     juce::dontSendNotification);
    heading.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    message.setText (juce::String::fromUTF8 (text ("Sun Loops Studio closed unexpectedly during the last session. Do you want to send an anonymous "
                                                   "bug report to GitHub to help us fix it?",
                                                   "Sun Loops Studio si è chiuso in modo anomalo durante l'ultima sessione. Vuoi inviare un report "
                                                   "anonimo del bug su GitHub per aiutarci a risolverlo?"))
                         + "\n\n" + juce::String::fromUTF8 (setup.summary.c_str()),
                     juce::dontSendNotification);
    message.setJustificationType (juce::Justification::topLeft);
    message.setMinimumHorizontalScale (1.0f);
    status.setJustificationType (juce::Justification::centredLeft);
    status.setColour (juce::Label::textColourId, juce::Colour (0xffffc861));

    preview.setMultiLine (true);
    preview.setReadOnly (true);
    preview.setScrollbarsShown (true);
    preview.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    preview.setText (juce::String::fromUTF8 (("# " + setup.title + "\n\n" + setup.body).c_str()), false);
    preview.setVisible (false);

    sendButton.setButtonText (juce::String::fromUTF8 (text ("Send report", "Invia Report")));
    showButton.setButtonText (juce::String::fromUTF8 (text ("Show log before sending", "Mostra Log prima di inviare")));
    ignoreButton.setButtonText (juce::String::fromUTF8 (text ("Ignore / Delete", "Ignora/Elimina")));
    browserButton.setButtonText (juce::String::fromUTF8 (text ("Open in the browser instead", "Apri invece nel browser")));
    keepButton.setButtonText (juce::String::fromUTF8 (text ("Keep it for later", "Tienilo per dopo")));
    openIssueButton.setButtonText (juce::String::fromUTF8 (text ("Open the issue", "Apri la issue")));
    sendButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2f6f3e));
    sendButton.setTooltip (setup.token.empty()
                               ? juce::String::fromUTF8 (text ("No GitHub token is set: the report opens in your browser, ready to submit",
                                                               "Nessun token GitHub impostato: il report si apre nel browser, pronto da inviare"))
                               : juce::String::fromUTF8 (text ("Creates an issue in ", "Crea una issue in ")) + juce::String (setup.repo.path()));

    sendButton.onClick = [this] { send(); };
    showButton.onClick = [this] { togglePreview(); };
    ignoreButton.onClick = [this] { deleteAndClose(); };
    browserButton.onClick = [this] { openInBrowser(); };
    keepButton.onClick = [this] { close(); };
    openIssueButton.onClick = [this] { juce::URL (issueUrl).launchInDefaultBrowser(); close(); };

    for (auto* c : std::initializer_list<juce::Component*> { &heading, &message, &status, &preview, &sendButton, &showButton, &ignoreButton,
                                                             &browserButton, &keepButton, &openIssueButton })
        addChildComponent (c);
    setState (State::Asking);
    setSize (kWidth, kCollapsedHeight);
}

void CrashReportDialog::show (Setup setup, std::function<void()> whenClosed)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Sun Loops Studio";
    options.content.setOwned (new CrashReportDialog (std::move (setup)));
    options.dialogBackgroundColour = juce::Colour (0xff2a2d34);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    auto* window = options.create();
    window->setResizeLimits (520, kCollapsedHeight, 2000, 1600);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([whenClosed = std::move (whenClosed)] (int)
    {
        if (whenClosed)
            whenClosed();
    }),
                             true);
}

void CrashReportDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2d34));
}

void CrashReportDialog::resized()
{
    layout();
}

void CrashReportDialog::layout()
{
    auto area = getLocalBounds().reduced (16);
    heading.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);
    message.setBounds (area.removeFromTop (88));
    auto buttons = area.removeFromBottom (32);
    area.removeFromBottom (6);
    status.setBounds (area.removeFromBottom (22));
    if (previewShown)
        preview.setBounds (area.withTrimmedBottom (6));

    juce::Array<juce::TextButton*> shown;
    for (auto* b : { &sendButton, &showButton, &ignoreButton, &browserButton, &keepButton, &openIssueButton })
        if (b->isVisible())
            shown.add (b);
    for (auto* b : shown)
    {
        const int w = juce::jmax (110, juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (15.0f)), b->getButtonText()) + 30);
        b->setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (8);
    }
}

void CrashReportDialog::setState (State newState, const juce::String& statusText)
{
    state = newState;
    const bool asking = state == State::Asking;
    sendButton.setVisible (asking || state == State::Sending);
    sendButton.setEnabled (asking);
    showButton.setVisible (asking || state == State::Sending || state == State::Failed);
    showButton.setButtonText (juce::String::fromUTF8 (previewShown ? text ("Hide the report", "Nascondi il report")
                                                                    : text ("Show log before sending", "Mostra Log prima di inviare")));
    ignoreButton.setVisible (asking || state == State::Failed || state == State::InBrowser);
    ignoreButton.setButtonText (juce::String::fromUTF8 (state == State::InBrowser ? text ("I sent it: delete the file", "L'ho inviato: elimina il file")
                                                                                   : text ("Ignore / Delete", "Ignora/Elimina")));
    browserButton.setVisible (state == State::Failed);
    keepButton.setVisible (state == State::Failed || state == State::InBrowser || state == State::Sent);
    keepButton.setButtonText (juce::String::fromUTF8 (state == State::Sent ? text ("Close", "Chiudi") : text ("Keep it for later", "Tienilo per dopo")));
    openIssueButton.setVisible (state == State::Sent && issueUrl.isNotEmpty());
    for (auto* c : std::initializer_list<juce::Component*> { &heading, &message, &status })
        c->setVisible (true);
    status.setText (statusText, juce::dontSendNotification);
    layout();
}

void CrashReportDialog::togglePreview()
{
    previewShown = ! previewShown;
    preview.setVisible (previewShown);
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->setContentComponentSize (getWidth(), previewShown ? kExpandedHeight : kCollapsedHeight);
    else
        setSize (getWidth(), previewShown ? kExpandedHeight : kCollapsedHeight);
    setState (state, status.getText());
}

void CrashReportDialog::send()
{
    if (setup.token.empty())
    {
        openInBrowser();
        return;
    }
    setState (State::Sending, juce::String::fromUTF8 (text ("Sending the report to GitHub...", "Invio del report a GitHub...")));
    github::IssueRequest request;
    request.apiBase = setup.apiBase;
    request.repo = setup.repo;
    request.token = setup.token;
    request.userAgent = setup.userAgent;
    request.title = setup.title;
    request.body = setup.body;
    request.labels = { "crash" };
    // The request runs on its own thread; the file is archived there only after GitHub answered 201 Created, so that happens
    // even if this window has been closed in the meantime.
    std::thread ([request, file = setup.crashFile, safe = juce::Component::SafePointer<CrashReportDialog> (this)]
    {
        const auto result = github::createIssue (request);
        const bool archived = result.created && crash::archive (file, "sent");
        if (result.created)
            spdlog::info ("Crash report sent: issue #{} {}", result.number, result.url);
        else
            spdlog::warn ("The crash report could not be sent: {}", result.error);
        juce::MessageManager::callAsync ([safe, result, archived]
        {
            if (safe != nullptr)
                safe->sent (result, archived);
        });
    }).detach();
}

void CrashReportDialog::sent (const github::IssueResult& result, bool archived)
{
    if (result.created)
    {
        issueUrl = juce::String (result.url);
        setState (State::Sent, juce::String::fromUTF8 (text ("Thank you: the report is issue #", "Grazie: il report è la issue #")) + juce::String (result.number)
                                   + (archived ? juce::String() : juce::String::fromUTF8 (text (" (the crash file could not be moved)", " (il file del crash non è stato spostato)"))));
    }
    else
        setState (State::Failed, juce::String::fromUTF8 (text ("Not sent: ", "Non inviato: ")) + juce::String (result.error));
}

void CrashReportDialog::openInBrowser()
{
    juce::URL (juce::String (github::newIssueLink (setup.repo, setup.title, setup.body))).launchInDefaultBrowser();
    setState (State::InBrowser, juce::String::fromUTF8 (text ("The report is open in your browser: check it and press \"Submit new issue\" (a GitHub account is needed).",
                                                              "Il report è aperto nel browser: controllalo e premi \"Submit new issue\" (serve un account GitHub).")));
}

void CrashReportDialog::deleteAndClose()
{
    std::error_code error;
    std::filesystem::remove (setup.crashFile, error);
    spdlog::info ("The crash report was {}", state == State::InBrowser ? "sent from the browser and deleted" : "deleted without sending it");
    close();
}

void CrashReportDialog::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}
} // namespace sls
