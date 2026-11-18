#pragma once

namespace fdve {

// The name and version of this program, as printed at start-up and as
// advertised to FreeDV Reporter and PSK Reporter. FDVE_VERSION comes from
// the project version in CMakeLists.txt.
inline constexpr const char* kSoftwareName = "freedv-embedded";
inline constexpr const char* kSoftwareVersion = FDVE_VERSION;
inline constexpr const char* kSoftwareAuthor = "Manoel - ON6RF";

} // namespace fdve
