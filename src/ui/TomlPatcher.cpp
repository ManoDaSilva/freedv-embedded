#include "ui/TomlPatcher.h"

#include <vector>

namespace fdve {

namespace {

std::string trim(const std::string& s)
{
    const size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

// "[ audio.radio ]  # comment" -> "audio.radio"; anything else -> "".
// Sets isHeader for any table header, including "[[array]]" ones.
std::string headerName(const std::string& line, bool& isHeader)
{
    const std::string t = trim(line);
    isHeader = !t.empty() && t[0] == '[';
    if (!isHeader || t.rfind("[[", 0) == 0) return "";
    const size_t close = t.find(']');
    if (close == std::string::npos) return "";
    std::string name;
    for (char c : t.substr(1, close - 1))
        if (c != ' ' && c != '\t') name += c;
    return name;
}

// True when the line assigns to exactly this key. valueStart is set to the
// position just after the "=".
bool assignsKey(const std::string& line, const std::string& key, size_t& valueStart)
{
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.compare(first, key.size(), key) != 0) return false;
    size_t pos = line.find_first_not_of(" \t", first + key.size());
    if (pos == std::string::npos || line[pos] != '=') return false;
    valueStart = pos + 1;
    return true;
}

// Scans a value from "from". Returns the position of a "#" comment on this
// line, or npos. depth tracks open "[" and "{" across lines, for values that
// continue on following lines.
size_t scanValue(const std::string& line, size_t from, int& depth)
{
    char quote = 0;
    for (size_t i = from; i < line.size(); i++)
    {
        const char c = line[i];
        if (quote != 0)
        {
            if (c == '\\' && quote == '"') i++;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '[' || c == '{') depth++;
        else if (c == ']' || c == '}') depth--;
        else if (c == '#') return i;
    }
    return std::string::npos;
}

bool isKeyLine(const std::string& line)
{
    const std::string t = trim(line);
    return !t.empty() && t[0] != '#' && t[0] != '[' && t.find('=') != std::string::npos;
}

} // namespace

std::string tomlString(const std::string& value)
{
    std::string out = "\"";
    for (char c : value)
    {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string setTomlValue(const std::string& text, const std::string& section, const std::string& key,
                         const std::string& literal)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            if (start < text.size()) lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }

    // Find the section: [first, last) are the lines after its header.
    size_t first = lines.size();
    size_t last = lines.size();
    bool found = false;
    for (size_t i = 0; i < lines.size(); i++)
    {
        bool isHeader = false;
        const std::string name = headerName(lines[i], isHeader);
        if (!isHeader) continue;
        if (found) { last = i; break; }
        if (name == section) { found = true; first = i + 1; }
    }

    if (!found)
    {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
        lines.push_back("[" + section + "]");
        lines.push_back(key + " = " + literal);
    }
    else
    {
        bool replaced = false;
        size_t lastKeyLine = first; // insert position if the key is missing
        for (size_t i = first; i < last; i++)
        {
            size_t valueStart = 0;
            if (isKeyLine(lines[i])) lastKeyLine = i + 1;
            if (!assignsKey(lines[i], key, valueStart)) continue;

            int depth = 0;
            size_t comment = scanValue(lines[i], valueStart, depth);
            // A value that continues on the next lines: drop the continuation.
            size_t extra = 0;
            while (depth > 0 && i + extra + 1 < last)
            {
                extra++;
                comment = scanValue(lines[i + extra], 0, depth);
            }
            const std::string tail = comment == std::string::npos ? "" : lines[i + extra].substr(comment);
            const size_t commentColumn = extra == 0 ? comment : std::string::npos;

            std::string line = lines[i].substr(0, valueStart) + " " + literal;
            if (!tail.empty())
            {
                // Keep the comment in its column when the new value still fits.
                if (commentColumn != std::string::npos && line.size() + 1 < commentColumn)
                    line.append(commentColumn - line.size(), ' ');
                else
                    line += "  ";
                line += tail;
            }
            lines[i] = line;
            lines.erase(lines.begin() + i + 1, lines.begin() + i + 1 + extra);
            replaced = true;
            break;
        }
        if (!replaced) lines.insert(lines.begin() + lastKeyLine, key + " = " + literal);
    }

    std::string out;
    for (const auto& line : lines) out += line + "\n";
    return out;
}

} // namespace fdve
