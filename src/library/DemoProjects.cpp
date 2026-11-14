#include "library/DemoProjects.h"

#include "DemoData.h"

namespace sls
{
const std::vector<DemoProjectInfo>& demoProjects()
{
    static const std::vector<DemoProjectInfo> list {
        { "sunrise-drive", "Sunrise Drive",
          "Synthwave, 110 BPM, A minor: the song the program opens with - every instrument, effect and automation kind",
          "sunrise-drive.slp" },
        { "white-line", "White Line",
          "Melodic techno, 129 BPM, C minor: a rolling bass and a pad pumping under the kick, a breakdown with a riser and a filter sweep",
          "white-line.slp" },
        { "ryo", "Ryo",
          "Lo-fi hip hop, 92 BPM, C minor: swung drums through a crushed drum bus, electric piano, a kalimba hook and tape stops",
          "ryo.slp" },
    };
    return list;
}

const DemoProjectInfo* findDemoProject (const std::string& id)
{
    for (const auto& d : demoProjects())
        if (d.id == id)
            return &d;
    return nullptr;
}

std::string demoProjectText (const std::string& id)
{
    const auto* info = findDemoProject (id);
    if (info == nullptr)
        return {};
    for (int i = 0; i < DemoData::namedResourceListSize; ++i)
        if (info->fileName == DemoData::originalFilenames[i])
        {
            int size = 0;
            if (const auto* data = DemoData::getNamedResource (DemoData::namedResourceList[i], size); data != nullptr && size > 0)
                return std::string (data, static_cast<size_t> (size));
        }
    return {};
}

ProjectParseResult readDemoProject (const std::string& id)
{
    const auto text = demoProjectText (id);
    if (text.empty())
    {
        ProjectParseResult result;
        result.error = "There is no demo project \"" + id + "\".";
        return result;
    }
    return ProjectDocument::fromJson (juce::String::fromUTF8 (text.data(), static_cast<int> (text.size())));
}
} // namespace sls
