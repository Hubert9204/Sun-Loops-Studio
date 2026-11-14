#pragma once

// Helios LLM mode (0.64.0): what the program and its helper process (sun-loops-helios, tools/helios-llm) say to each other.
// Shared by both sides; plain C++, no JUCE.
//
// The helper is started by the program with juce::ChildProcessCoordinator and answers with juce::ChildProcessWorker; every
// message is one JSON object (UTF-8) with a "type":
//
//   program -> helper
//     {"type":"load", "path":"/.../gemma-4-E2B-it-Q4_0.gguf", "context":8192, "threads":4}
//     {"type":"generate", "id":7, "prompt":"<bos><|turn>system\n...", "maxTokens":768, "temperature":0.7, "seed":123}
//     {"type":"cancel"}                     stops the running answer (it ends with "done", reason "cancelled")
//     {"type":"quit"}                       the helper ends (it also ends when the program disconnects or is gone)
//
//   helper -> program
//     {"type":"loaded", "ok":true, "error":"", "context":8192, "gpu":false}
//     {"type":"token", "id":7, "text":"Ciao"}        pieces of the answer, always whole UTF-8 characters
//     {"type":"done", "id":7, "reason":"end"|"limit"|"cancelled"|"error", "error":"", "promptTokens":812, "reused":640,
//      "generated":95, "seconds":4.2}
//
// The helper keeps what it has already read of the last conversation (the model's memory of it) and reads only what is new
// in the next prompt, so the fixed system text is read once per session.

namespace sls::helios
{
// The command-line marker juce::ChildProcessCoordinator uses to recognise the helper (must match on both sides).
inline constexpr const char* kWorkerId = "sls-helios-worker";

// Version of this protocol; the helper reports it when it is started with --version.
inline constexpr int kProtocolVersion = 1;
} // namespace sls::helios
