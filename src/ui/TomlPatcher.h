#pragma once

#include <string>

namespace fdve {

// Changes one value in the text of a TOML file and leaves everything else as
// it was: comments, blank lines, key order, and the comment at the end of the
// changed line.
//
//   section  dotted table name as in the file, for example "audio.radio"
//   key      key inside that table
//   literal  the new value as TOML source, for example "\"debug\"" or "48000"
//
// If the key is missing it is added to the section; if the section is missing
// it is added at the end. It handles files in the style this project uses
// (one "[table]" per section, one key per line), not every possible TOML
// layout, so callers must parse the result to confirm it before saving.
std::string setTomlValue(const std::string& text, const std::string& section, const std::string& key,
                         const std::string& literal);

std::string tomlString(const std::string& value); // quotes and escapes

} // namespace fdve
