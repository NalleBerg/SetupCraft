#pragma once
#include <map>
#include <string>

// Returns a canonical mapping of locale code -> display name (wide string).
std::map<std::wstring, std::wstring> GetCanonicalDisplayNames();
