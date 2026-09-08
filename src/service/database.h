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
#include "model.h"
#include "parser.h"
#include "utils/logger.h"
#include "utils/paths.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <sqlite3.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using ProgressCallback = std::function<void(size_t processed, size_t total)>;

enum class SearchField { None, PlatformID, AuthorID, AuthorName, AuthorNick, Title };

enum class DbMode { None, Normal, Import, Query };

class SQLiteStatement { // RAII wrapper for sqlite3_stmt
public:
    SQLiteStatement() : stmt_(nullptr) {}
    SQLiteStatement(sqlite3* db, const std::string& sql) : stmt_(nullptr) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            Error() << "Failed to prepare statement:" << sql << "Error:" << sqlite3_errmsg(db);
            stmt_ = nullptr;
        }
    }
    ~SQLiteStatement() {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }
    sqlite3_stmt* get() const { return stmt_; }

    SQLiteStatement(const SQLiteStatement&) = delete;
    SQLiteStatement& operator=(const SQLiteStatement&) = delete;
    SQLiteStatement(SQLiteStatement&& other) noexcept : stmt_(other.stmt_) { other.stmt_ = nullptr; }
    SQLiteStatement& operator=(SQLiteStatement&& other) noexcept {
        if (this != &other) {
            if (stmt_) {
                sqlite3_finalize(stmt_);
            }
            stmt_ = other.stmt_;
            other.stmt_ = nullptr;
        }
        return *this;
    }

private:
    sqlite3_stmt* stmt_;
};

class DbCache { // singleton class for caching database mappings
public:
    static DbCache& getInstance() {
        static DbCache instance;
        return instance;
    }
    bool tagMappingLoaded() const {
        return !tagById.empty();
    }
    bool featureHashCacheLoaded() const { return !picFeatureHashes.empty(); }
    bool importedFileLoaded() const { return !importedFiles.empty(); }

    const std::vector<TagStr>& getTags() const { return tags; }
    uint32_t getTagId(const std::string& tagText) const {
        auto it = tagToId.find(tagText);
        return it != tagToId.end() ? it->second : 0;
    }

    void loadTagMapping(std::unordered_map<std::string, uint32_t>&& tagToIdMap,
                        std::vector<TagStr>&& tagList,
                        std::unordered_map<uint32_t, TagStr>&& tagByIdMap) {
        std::lock_guard<std::mutex> lock(writeMutex);
        tagToId = tagToIdMap;
        tags = tagList;
        tagById = tagByIdMap;
    }
    void loadPicFeatureHashes(std::vector<std::pair<uint64_t, std::array<uint8_t, 64>>>&& featureHashes) {
        std::lock_guard<std::mutex> lock(writeMutex);
        picFeatureHashes = featureHashes;
    }
    void loadImportedFiles(std::unordered_map<std::string, std::unordered_set<std::string>>&& files) {
        std::lock_guard<std::mutex> lock(writeMutex);
        importedFiles = files;
    }

    TagStr getStringTag(uint32_t tagId) const {
        auto it = tagById.find(tagId);
        if (it != tagById.end()) {
            return it->second;
        }
        return TagStr{};
    }
    void updateTagCategory(uint32_t tagId, int category) {
        std::lock_guard<std::mutex> lock(writeMutex);
        auto it = tagById.find(tagId);
        if (it != tagById.end()) {
            it->second.category = category;
            for (auto& tag : tags) {
                if (tag.tag == it->second.tag) {
                    tag.category = category;
                    break;
                }
            }
        }
    }
    bool isFileImported(const std::filesystem::path& filePath) const {
        std::string dir = filePath.parent_path().string();
        std::string filename = filePath.filename().string();
        if (importedFiles.find(dir) != importedFiles.end()) {
            if (importedFiles.at(dir).find(filename) != importedFiles.at(dir).end()) {
                return true;
            }
        }
        return false;
    }
    void addImportedFile(const std::filesystem::path& filePath) {
        if (isFileImported(filePath)) return;
        std::lock_guard<std::mutex> lock(writeMutex);

        std::string dir = filePath.parent_path().string();
        std::string filename = filePath.filename().string();
        if (importedFiles.find(dir) == importedFiles.end()) {
            importedFiles[dir] = std::unordered_set<std::string>();
        }
        importedFiles[dir].insert(filename);
    }

    bool tagExists(const std::string& tagText) const { return tagToId.find(tagText) != tagToId.end(); }
    uint32_t addTagToCache(const std::string& tagText, int platform, int category) {
        if (tagToId.find(tagText) != tagToId.end()) {
            return tagToId.at(tagText);
        }
        std::lock_guard<std::mutex> lock(writeMutex);
        uint32_t maxId = 0;
        for (const auto& [id, _] : tagById) { if (id > maxId) maxId = id; }
        uint32_t newId = maxId + 1;
        tagToId[tagText] = newId;
        TagStr newTag{tagText, platform, category, ""};
        tagById[newId] = newTag;
        tags.emplace_back(newTag);
        return newId;
    }
    void addTagToCacheWithId(const std::string& tagText, uint32_t id, int platform, int category) {
        std::lock_guard<std::mutex> lock(writeMutex);
        tagToId[tagText] = id;
        TagStr newTag{tagText, platform, category, ""};
        tagById[id] = newTag;
        tags.emplace_back(newTag);
    }

    void clearTagMapping() {
        std::lock_guard<std::mutex> lock(writeMutex);
        tagToId.clear();
        tags.clear();
        tagById.clear();
        tagChildren.clear();
        tagParents.clear();
        aliasToCanonical.clear();
        tagAliases.clear();
    }

    void loadTagAliases(std::unordered_map<uint32_t, uint32_t>&& aliasToCanonical,
                        std::unordered_map<uint32_t, std::vector<uint32_t>>&& tagAliases) {
        std::lock_guard<std::mutex> lock(writeMutex);
        this->aliasToCanonical = aliasToCanonical;
        this->tagAliases = tagAliases;
    }

    uint32_t getCanonicalId(uint32_t tagId) const {
        auto it = aliasToCanonical.find(tagId);
        return it != aliasToCanonical.end() ? it->second : 0;
    }
    bool hasAliases(uint32_t tagId) const {
        auto it = tagAliases.find(tagId);
        return it != tagAliases.end() && !it->second.empty();
    }
    const std::vector<uint32_t>& getAliases(uint32_t tagId) const {
        static const std::vector<uint32_t> empty;
        auto it = tagAliases.find(tagId);
        return it != tagAliases.end() ? it->second : empty;
    }
    std::vector<uint32_t> getAliasGroup(uint32_t tagId) const {
        std::vector<uint32_t> result;
        std::unordered_set<uint32_t> visited;
        std::vector<uint32_t> stack{tagId};
        while (!stack.empty()) {
            uint32_t cur = stack.back();
            stack.pop_back();
            if (visited.count(cur)) continue;
            visited.insert(cur);
            result.push_back(cur);
            auto it = tagAliases.find(cur);
            if (it != tagAliases.end()) {
                for (uint32_t a : it->second) if (!visited.count(a)) stack.push_back(a);
            }
            auto it2 = aliasToCanonical.find(cur);
            if (it2 != aliasToCanonical.end()) {
                if (!visited.count(it2->second)) stack.push_back(it2->second);
            }
        }
        return result;
    }
    const std::unordered_map<uint32_t, std::vector<uint32_t>>& getAllAliases() const {
        return tagAliases;
    }
    void addAlias(uint32_t aliasTagId, uint32_t canonicalTagId) {
        std::lock_guard<std::mutex> lock(writeMutex);
        aliasToCanonical[aliasTagId] = canonicalTagId;
        tagAliases[canonicalTagId].push_back(aliasTagId);
    }
    void removeAlias(uint32_t aliasTagId, uint32_t canonicalTagId) {
        std::lock_guard<std::mutex> lock(writeMutex);
        aliasToCanonical.erase(aliasTagId);
        auto& aliases = tagAliases[canonicalTagId];
        aliases.erase(std::remove(aliases.begin(), aliases.end(), aliasTagId), aliases.end());
    }

    void loadTagHierarchy(std::unordered_map<uint32_t, std::vector<uint32_t>>&& children,
                          std::unordered_map<uint32_t, std::vector<uint32_t>>&& parents) {
        std::lock_guard<std::mutex> lock(writeMutex);
        tagChildren = children;
        tagParents = parents;
    }

    bool hasChildren(uint32_t tagId) const {
        auto it = tagChildren.find(tagId);
        return it != tagChildren.end() && !it->second.empty();
    }
    bool hasParents(uint32_t tagId) const {
        auto it = tagParents.find(tagId);
        return it != tagParents.end() && !it->second.empty();
    }
    const std::vector<uint32_t>& getChildren(uint32_t tagId) const {
        static const std::vector<uint32_t> empty;
        auto it = tagChildren.find(tagId);
        return it != tagChildren.end() ? it->second : empty;
    }
    const std::vector<uint32_t>& getParents(uint32_t tagId) const {
        static const std::vector<uint32_t> empty;
        auto it = tagParents.find(tagId);
        return it != tagParents.end() ? it->second : empty;
    }
    const std::unordered_map<uint32_t, std::vector<uint32_t>>& getAllParents() const {
        return tagParents;
    }

    void addParentChild(uint32_t childId, uint32_t parentId) {
        std::lock_guard<std::mutex> lock(writeMutex);
        tagChildren[parentId].push_back(childId);
        tagParents[childId].push_back(parentId);
    }
    void removeParentChild(uint32_t childId, uint32_t parentId) {
        std::lock_guard<std::mutex> lock(writeMutex);
        auto& children = tagChildren[parentId];
        children.erase(std::remove(children.begin(), children.end(), childId), children.end());
        auto& parents = tagParents[childId];
        parents.erase(std::remove(parents.begin(), parents.end(), parentId), parents.end());
    }
    bool wouldCreateCycle(uint32_t childId, uint32_t parentId) {
        std::lock_guard<std::mutex> lock(writeMutex);
        std::unordered_set<uint32_t> visited;
        std::vector<uint32_t> stack{parentId};
        while (!stack.empty()) {
            uint32_t current = stack.back();
            stack.pop_back();
            if (current == childId) return true;
            if (visited.count(current)) continue;
            visited.insert(current);
            auto it = tagChildren.find(current);
            if (it != tagChildren.end()) {
                for (uint32_t c : it->second) stack.push_back(c);
            }
        }
        return false;
    }

private:
    DbCache() = default;
    ~DbCache() = default;
    DbCache(const DbCache&) = delete;
    DbCache& operator=(const DbCache&) = delete;
    DbCache(DbCache&&) = delete;
    DbCache& operator=(DbCache&&) = delete;

    std::mutex writeMutex;

    // in-memory tag mapping
    std::unordered_map<std::string, uint32_t> tagToId;  // tag text → tag_id (unified)
    std::vector<TagStr> tags;
    std::unordered_map<uint32_t, TagStr> tagById;
    std::unordered_map<uint32_t, std::vector<uint32_t>> tagChildren;  // parent_id → [child_ids]
    std::unordered_map<uint32_t, std::vector<uint32_t>> tagParents;   // child_id → [parent_ids]
    std::unordered_map<uint32_t, uint32_t> aliasToCanonical;              // alias_tag_id → canonical_tag_id
    std::unordered_map<uint32_t, std::vector<uint32_t>> tagAliases;     // canonical_tag_id → [alias_tag_ids]

    // feature hash cache for similarity search
    std::vector<std::pair<uint64_t, std::array<uint8_t, 64>>> picFeatureHashes; // (picID, featureHash)

    // imported files cache
    std::unordered_map<std::string, std::unordered_set<std::string>> importedFiles; // directory -> set of imported file names
};

class PicDatabase { // sqlite database wrapper
public:
    PicDatabase(const std::string& databaseFile = "", DbMode mode = DbMode::Normal);
    explicit PicDatabase(DbMode mode) : PicDatabase("", mode) {}
    ~PicDatabase();

    // transaction and mode management
    void enableForeignKeyRestriction() const {
        if (!execute("PRAGMA foreign_keys = ON;")) {
            Error() << "Failed to enable foreign key restriction:" << sqlite3_errmsg(db);
        }
    }
    void disableForeignKeyRestriction() const {
        if (!execute("PRAGMA foreign_keys = OFF;")) {
            Error() << "Failed to disable foreign key restriction:" << sqlite3_errmsg(db);
        }
    }
    bool beginTransaction() const { return execute("BEGIN TRANSACTION;"); }
    bool commitTransaction() const { return execute("COMMIT;"); }
    bool rollbackTransaction() const { return execute("ROLLBACK;"); }
    void setMode(DbMode mode) {
        if (currentMode == mode) return;
        execute("PRAGMA journal_mode = WAL");
        switch (mode) {
        case DbMode::Normal: // use for ui thread
            execute("PRAGMA cache_size = -32000");
            execute("PRAGMA synchronous = NORMAL");
            execute("PRAGMA foreign_keys = ON");
            break;

        case DbMode::Import: // use for batch import
            execute("PRAGMA cache_size = -128000");
            execute("PRAGMA temp_store = memory");
            execute("PRAGMA foreign_keys = OFF");
            execute("PRAGMA synchronous = NORMAL");
            break;

        case DbMode::Query: // use for read-only query
            execute("PRAGMA cache_size = -64000");
            execute("PRAGMA mmap_size = 268435456");
            execute("PRAGMA foreign_keys = ON");
            execute("PRAGMA query_only = ON");
            break;

        default:
            break;
        }
        currentMode = mode;
    }

    // getters
    PicInfo getPicInfo(uint64_t id) const;
    std::vector<uint64_t> getMetadataPicIds(const PlatformID& platformID) const;
    std::vector<PicInfo> getMetadataPicInfos(const PlatformID& platformID) const;

    Metadata getMetadata(PlatformType platform, int64_t PlatformID) const;
    Metadata getMetadata(const ImageSource& identifier) const { return getMetadata(identifier.platform, identifier.platformID); }
    Metadata getMetadata(const PlatformID& platformID) const { return getMetadata(platformID.platform, platformID.platformID); }

    std::vector<TagCount> getTagCounts() const; // for gui tag selection panel display
    TagStr getStringTag(uint32_t tagId) const { return cache.getStringTag(tagId); }
    
    // tag source and category management
    bool setTagCategory(uint32_t tagId, TagCategory category) const;

    // tag hierarchy management
    bool setTagParent(uint32_t childTagId, uint32_t parentTagId);
    bool removeTagParent(uint32_t childTagId, uint32_t parentTagId) const;

    // tag alias management
    bool setTagAlias(uint32_t aliasTagId, uint32_t canonicalTagId);
    bool removeTagAlias(uint32_t aliasTagId, uint32_t canonicalTagId) const;
    
    // platform tag classification
    bool classifyPlatformTag(PlatformType platform, const std::string& tag, bool isCharacter) const;
    bool deletePlatformTagClassification(PlatformType platform, const std::string& tag) const;
    std::optional<bool> getPlatformTagClassification(PlatformType platform, const std::string& tag) const;
    std::unordered_map<std::string, bool> getAllPlatformTagClassifications(PlatformType platform) const;
    void syncClassifiedPlatformTagsToPictureTags() const;
    std::optional<uint32_t> getTagIdByTagText(const std::string& tagText) const;
    bool deleteTag(uint32_t tagId) const;
    bool deleteTag(const std::string& tagText) const;
    std::optional<PlatformType> getPlatformTypeByTagText(const std::string& tagText) const;

    // manual tag management
    bool addTagToPicture(uint64_t picId, uint32_t tagId, float probability = 1.0f) const;
    bool removeTagFromPicture(uint64_t picId, uint32_t tagId) const;
    uint32_t addTag(const std::string& tagName, TagCategory category = TagCategory::Attribute) const;
    const std::vector<TagStr>& getAllTags() const { return cache.getTags(); }
    void syncMetadataFile(const PicInfo& picInfo, const Metadata* meta, const std::string& tagName, bool adding) const;

    void refreshTagMapping() const;

    // insert functions
    bool insertPicture(const ParsedPicture& picInfo) const;
    bool insertMetadata(const ParsedMetadata& metadataInfo);
    bool updateMetadata(const ParsedMetadata& metadataInfo);

    // search functions
    std::unordered_set<uint64_t> tagSearch(const std::unordered_set<uint32_t>& includedTagIds,
                                           const std::unordered_set<uint32_t>& excludedTagIds) const;
    std::unordered_set<PlatformID>
    textSearch(const std::string& searchText, PlatformType platformType, SearchField searchField) const;

    // import functions
    void importFilesFromDirectory(
        const std::filesystem::path& directory,   // single-threaded import function, only for debug use
        ParserType parserType = ParserType::None, // use as a baseline comparison to multithreaded Importer class
        ProgressCallback progressCallback = nullptr);
    void processAndImportSingleFile(const std::filesystem::path& path, ParserType parserType = ParserType::None);
    void syncMetadataAndPicTables(std::unordered_set<PlatformID> newMetadataIds = {}) const; // post-import operations
    bool isFileImported(const std::filesystem::path& filePath) const { return cache.isFileImported(filePath); }
    void addImportedFile(const std::filesystem::path& filePath) const;
    void updateTagCounts() const;

    // tagger functions
    std::string getModelName() const;
    void importTagSet(const std::string& modelName, const std::vector<std::pair<std::string, bool>>& tags) const; // (tag, isCharacter)
    std::vector<std::pair<uint64_t, std::vector<std::filesystem::path>>> getUntaggedPics() const;                 // (picID, filePath)
    void updatePicTags(uint64_t picID,
                       const std::vector<PicTag>& picTags,
                       RestrictType restrictType,
                       const std::vector<uint8_t>& featureHash) const;

private:
    sqlite3* db = nullptr;
    DbMode currentMode = DbMode::None;
    DbCache& cache = DbCache::getInstance();

    std::unordered_set<PlatformID> newMetadataIds; // for syncMetadataAndPicTables use
    std::unordered_map<PlatformID, std::vector<uint32_t>> newMetadataTagIds; // deferred platform tag associations

    void initDatabase(const std::string& databaseFile);
    bool createTables() const;
    void initTagMapping() const;
    void initImportedFiles() const;

    bool execute(const std::string& sql) const {
        char* errorMsg = nullptr;
        int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMsg);
        if (rc != SQLITE_OK) {
            Error() << "Error executing SQL: " << errorMsg;
            sqlite3_free(errorMsg);
            return false;
        }
        return true;
    }
    SQLiteStatement prepare(const std::string& sql) const { return SQLiteStatement(db, sql); }
};
