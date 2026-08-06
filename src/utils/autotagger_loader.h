/*
 * Waifu Gallery - A anime illustration gallery application.
 * Copyright (C) 2025 R4nd5tr <r4nd5tr@outlook.com>
 *
 * This program is free software: you can redistribute it and/or modify
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
#include "autotagger/autotagger.h"
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using CreateFunc = AutoTagger* (*)();
using DestroyFunc = void (*)(AutoTagger*);

class AutoTaggerLoader {
public:
    AutoTaggerLoader() = default;
    ~AutoTaggerLoader() { unload(); }
    std::vector<std::filesystem::path> discoverAutoTaggers() {
        std::vector<std::filesystem::path> taggerPaths;
        std::filesystem::path searchPath = "./model/";
        for (const auto& entry : std::filesystem::directory_iterator(searchPath)) {
            if (entry.is_regular_file()) {
#ifdef _WIN32
                if (entry.path().extension() == ".dll")
#else
                if (entry.path().extension() == ".so")
#endif
                    taggerPaths.push_back(entry.path());
            }
        }
        return taggerPaths;
    };

    bool load(const std::filesystem::path& dllPath) {
#ifdef _WIN32
        hDll_ = LoadLibraryW(dllPath.wstring().c_str());
        if (!hDll_) return false;
        createFunc_ = reinterpret_cast<CreateFunc>(GetProcAddress(static_cast<HMODULE>(hDll_), "createAutoTagger"));
        destroyFunc_ = reinterpret_cast<DestroyFunc>(GetProcAddress(static_cast<HMODULE>(hDll_), "destroyAutoTagger"));
#else
        hDll_ = dlopen(dllPath.string().c_str(), RTLD_LAZY);
        if (!hDll_) return false;
        createFunc_ = reinterpret_cast<CreateFunc>(dlsym(hDll_, "createAutoTagger"));
        destroyFunc_ = reinterpret_cast<DestroyFunc>(dlsym(hDll_, "destroyAutoTagger"));
#endif
        return createFunc_ && destroyFunc_;
    }

    void unload() {
        if (taggerInstance_) {
            destroyFunc_(taggerInstance_);
            taggerInstance_ = nullptr;
        }
        if (hDll_) {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(hDll_));
#else
            dlclose(hDll_);
#endif
            hDll_ = nullptr;
            createFunc_ = nullptr;
            destroyFunc_ = nullptr;
        }
    }

    AutoTagger* getTagger() {
        if (taggerInstance_ == nullptr) {
            taggerInstance_ = createFunc_ ? createFunc_() : nullptr;
        }
        return taggerInstance_;
    }

    bool isLoaded() const { return hDll_ != nullptr; }

private:
    AutoTagger* taggerInstance_ = nullptr;
    void* hDll_ = nullptr;
    CreateFunc createFunc_ = nullptr;
    DestroyFunc destroyFunc_ = nullptr;
};
