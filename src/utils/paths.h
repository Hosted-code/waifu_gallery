/*
 * Waifu Gallery - A anime illustration gallery application.
 * Copyright (C) 2025 R4nd5tr <r4nd5tr@outlook.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <filesystem>
#include <string>
#include <cstdlib>
#include <sstream>

namespace Paths {

inline std::filesystem::path getConfigDirectory() {
    const char* envPath = std::getenv("WAIFU_GALLERY_CONFIG_PATH");
    if (envPath && strlen(envPath) > 0) {
        return std::filesystem::path(envPath);
    }
    
    const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfigHome && strlen(xdgConfigHome) > 0) {
        return std::filesystem::path(xdgConfigHome) / "waifu_gallery";
    }
    
    const char* home = std::getenv("HOME");
    if (home && strlen(home) > 0) {
        return std::filesystem::path(home) / ".config" / "waifu_gallery";
    }
    
    return std::filesystem::current_path();
}

inline std::filesystem::path getDataDirectory() {
    const char* envPath = std::getenv("WAIFU_GALLERY_DB_PATH");
    if (envPath && strlen(envPath) > 0) {
        return std::filesystem::path(envPath);
    }
    
    const char* xdgDataHome = std::getenv("XDG_DATA_HOME");
    if (xdgDataHome && strlen(xdgDataHome) > 0) {
        return std::filesystem::path(xdgDataHome) / "waifu_gallery";
    }
    
    const char* home = std::getenv("HOME");
    if (home && strlen(home) > 0) {
        return std::filesystem::path(home) / ".local" / "share" / "waifu_gallery";
    }
    
    return std::filesystem::current_path();
}

inline std::filesystem::path getDatabasePath() {
    return getDataDirectory() / "database.db";
}

inline std::filesystem::path getSettingsPath() {
    return getConfigDirectory() / "settings.json";
}

inline std::filesystem::path getLogPath() {
    return getDataDirectory() / "waifu_gallery.log";
}

inline std::filesystem::path getAutoTaggerPath() {
    return getDataDirectory() / "autotagger";
}

inline bool ensureDirectoryExists(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        return true;
    }
    
    std::error_code ec;
    return std::filesystem::create_directories(path, ec);
}

inline std::string getDataDirectoryString() {
    return getDataDirectory().generic_u8string();
}

inline std::string getDatabasePathString() {
    return getDatabasePath().generic_u8string();
}

inline std::string getSettingsPathString() {
    return getSettingsPath().generic_u8string();
}

}