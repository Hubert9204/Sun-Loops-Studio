#pragma once

#include <string>
#include <vector>

#include "library/ProjectFile.h"

namespace sls
{
// The demo projects (0.65.0): finished songs that come with the program, listed in the browser's "Demo Projects" folder and in the
// File menu. They are ordinary project files (resources/demos/*.slp) built into the program; one opens as a new, untitled project
// named after the demo, so saving it never writes over the demo.
struct DemoProjectInfo
{
    std::string id;          // "white-line": the browser item's payload is "demo:<id>"
    std::string title;       // "White Line"
    std::string description; // one line: style, tempo, key, what it shows
    std::string fileName;    // the file built into the program ("white-line.slp")
};

constexpr const char* kDemoProjectPrefix = "demo:";

const std::vector<DemoProjectInfo>& demoProjects();
const DemoProjectInfo* findDemoProject (const std::string& id);

// The demo's project file as text (empty for an unknown id).
std::string demoProjectText (const std::string& id);

// Reads a demo as readProjectFile reads a file (error set for an unknown id).
ProjectParseResult readDemoProject (const std::string& id);
} // namespace sls
