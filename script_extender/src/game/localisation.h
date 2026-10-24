#pragma once

#include <map>
#include <string>
#include <vector>

namespace se::game {

// The English text of each of `keys` from the localisation CSVs the game reads: the folders of the mods
// given with -mod= on the command line (in that order), then the game's own unless a mod replaces
// "localisation", then the plugin's (mod/script_extender/localisation, the texts of the Script Extender's
// own keys). A file with the name of an earlier one is skipped; the first definition of a key
// wins. "\n" becomes a newline, as in the game's loader. Keys without a definition are missing from the
// result. Reads the files once per call.
std::map<std::string, std::string> read_localisation(const std::vector<std::string>& keys);

}  // namespace se::game
