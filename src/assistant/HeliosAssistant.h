#pragma once

#include <atomic>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <juce_events/juce_events.h>

#include "assistant/HeliosChat.h"

namespace sls
{
class HelpIndex;

// Helios LLM mode (0.64.0): the chat with a language model that runs on this computer. It owns the helper process
// (sun-loops-helios, which runs the model with llama.cpp), the model files (downloaded once, on request) and the conversation.
// Everything here happens on the message thread; the helper and the download report back through it. Listeners hear of every
// change (a new piece of an answer, the download's progress, the state).
class HeliosAssistant : public juce::ChangeBroadcaster
{
public:
    enum class State
    {
        unavailable, // this build has no helper (or it is missing)
        noModel,     // the chosen model is not on this computer yet
        downloading,
        idle,        // the model is here, the helper not started (or stopped to free the memory)
        loading,     // the helper is starting and reading the model
        ready,
        answering,
        failed       // see error()
    };

    struct Message
    {
        bool fromUser = true;
        juce::String text;  // what the user typed, or the answer so far
        bool done = true;   // false while the answer is being written
    };

    explicit HeliosAssistant (const HelpIndex& manual);
    ~HeliosAssistant() override;

    State state() const noexcept { return currentState; }
    juce::String error() const { return lastError; }
    juce::String status() const; // one line for the window: "Ready (Gemma 4 E2B, on the processor)", "Downloading 41%..."

    // The model (helios::models()): "light" or "quality".
    void chooseModel (const juce::String& id);
    juce::String modelId() const { return model; }
    juce::File modelFile() const;
    bool modelPresent() const;
    static juce::File modelFolder(); // where models are kept

    // The helper next to the program (Contents/MacOS on a Mac); missing in builds without it.
    static juce::File helperFile();

    void download();          // the chosen model; checks the free space first
    void cancelDownload();    // the part already downloaded stays and is continued next time
    void deleteModel();       // frees the disk space
    double downloadProgress() const noexcept { return progress; } // 0..1 while downloading

    void start(); // starts the helper and loads the model (if it is here); the first question also does it
    void stop();  // stops the helper and frees its memory; the conversation stays

    void ask (const juce::String& question);
    void stopAnswer();
    void newChat();
    const std::vector<Message>& messages() const noexcept { return shown; }

    // For the tests and a curious user: how the last answer went.
    juce::String lastAnswerInfo() const { return answerInfo; }

private:
    class Helper;
    void setState (State s, const juce::String& errorText = {});
    void helperLoaded (bool ok, const juce::String& errorText, bool gpu);
    void helperToken (int id, const juce::String& text);
    void helperDone (int id, const juce::String& reason, const juce::String& errorText, const juce::var& info);
    void helperLost();
    void sendPending();
    void refreshModelState();

    const HelpIndex& help;
    juce::String model { "light" };
    State currentState = State::unavailable;
    juce::String lastError, answerInfo;
    bool onGpu = false;

    std::unique_ptr<Helper> helper;
    int helperGeneration = 0;
    std::vector<Message> shown;           // what the window shows
    std::vector<helios::Turn> history;    // what the model has read (questions with their manual excerpts)
    juce::String pendingQuestion;         // asked while the model was still loading
    int answerId = 0;
    bool answerCatchphrase = false;
    std::string answerText;
    std::mt19937 random { std::random_device{}() };

    std::thread downloader;
    std::atomic<bool> cancelDownloading { false };
    double progress = 0.0;
    std::uint64_t downloadedBytes = 0, downloadTotal = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (HeliosAssistant)
};
} // namespace sls
