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

#include "database.h"
#include "model.h"
#include "parser.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

// utility functions

int64_t uint64_to_int64(uint64_t u) {
    int64_t i;
    std::memcpy(&i, &u, sizeof(u));
    return i;
}
uint64_t int64_to_uint64(int64_t i) {
    uint64_t u;
    std::memcpy(&u, &i, sizeof(i));
    return u;
}
std::vector<std::filesystem::path> collectFiles(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        files.push_back(entry.path());
    }
    Info() << "Total files collected:" << files.size() << "from directory:" << directory.string();
    return files;
}

// PicDatabase class implementation

PicDatabase::PicDatabase(const std::string& databaseFile, DbMode mode) {
    std::string dbFile = databaseFile;
    
    if (dbFile.empty() || dbFile == "database.db") {
        Paths::ensureDirectoryExists(Paths::getDataDirectory());
        dbFile = Paths::getDatabasePathString();
    }
    
    initDatabase(dbFile);
    setMode(mode);
    initTagMapping();
    if (mode == DbMode::Import) initImportedFiles(); // only needed in import mode, to avoid parsing duplicate files
}
PicDatabase::~PicDatabase() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

// init functions

void PicDatabase::initDatabase(const std::string& databaseFile) {
    if (sqlite3_open(databaseFile.c_str(), &db) != SQLITE_OK) {
        Error() << "Error opening database: " << sqlite3_errmsg(db);
        return;
    }
    if (!createTables()) {
        Error() << "Failed to create tables";
        return;
    }
    Info() << "Database initialized";
}
bool PicDatabase::createTables() const {
    const std::string metadataTable = R"(
        CREATE TABLE IF NOT EXISTS metadata (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )";

    // pictures related tables
    const std::string picturesTable = R"(
        CREATE TABLE IF NOT EXISTS pictures (
            id INTEGER PRIMARY KEY NOT NULL,
            width INTEGER,
            height INTEGER,
            size INTEGER,
            file_type INTEGER,

            edit_time TEXT DEFAULT NULL,
            download_time TEXT DEFAULT NULL,

            feature_hash BLOB DEFAULT NULL,

            restrict_type INTEGER DEFAULT 0,
            ai_type INTEGER DEFAULT 0
        )
    )";
    const std::string pictureTagsTable = R"(
        CREATE TABLE IF NOT EXISTS picture_tags (
            id INTEGER NOT NULL,
            tag_id INTEGER NOT NULL,
            probability REAL DEFAULT 0.0,

            PRIMARY KEY (id, tag_id),

            FOREIGN KEY (id) REFERENCES pictures(id) ON DELETE CASCADE,
            FOREIGN KEY (tag_id) REFERENCES tags(tag_id) ON DELETE CASCADE
        )
    )";
    const std::string pictureFilesTable = R"(
        CREATE TABLE IF NOT EXISTS picture_file_paths (
            id INTEGER NOT NULL,
            file_path TEXT NOT NULL UNIQUE,

            PRIMARY KEY (id, file_path),

            FOREIGN KEY (id) REFERENCES pictures(id) ON DELETE CASCADE
        )
    )";
    const std::string pictureSourceTable = R"(
        CREATE TABLE IF NOT EXISTS picture_source (
            id INTEGER NOT NULL,
            platform INTEGER NOT NULL,
            platform_id INTEGER NOT NULL,
            image_index INTEGER NOT NULL,

            PRIMARY KEY (platform, platform_id, image_index),

            FOREIGN KEY (id) REFERENCES pictures(id) ON DELETE CASCADE
        )
    )";

    // picture metadata related tables
    const std::string picMetadataTable = R"(
        CREATE TABLE IF NOT EXISTS picture_metadata (
            platform INTEGER NOT NULL,
            platform_id INTEGER NOT NULL,
            date TEXT NOT NULL,

            author_id INTEGER NOT NULL,
            author_name TEXT NOT NULL,
            author_nick TEXT,
            author_description TEXT,

            title TEXT,
            description TEXT,

            view_count INTEGER DEFAULT 0,
            like_count INTEGER DEFAULT 0,
            bookmark_count INTEGER DEFAULT 0,
            reply_count INTEGER DEFAULT 0,
            forward_count INTEGER DEFAULT 0,
            quote_count INTEGER DEFAULT 0,

            restrict_type INTEGER DEFAULT 0,
            ai_type INTEGER DEFAULT 0,

            PRIMARY KEY (platform, platform_id)
        )
    )";
    const std::string platformTagClassificationTable = R"(
        CREATE TABLE IF NOT EXISTS platform_tag_classification (
            platform INTEGER NOT NULL,
            tag TEXT NOT NULL,
            is_character BOOLEAN NOT NULL,

            PRIMARY KEY (platform, tag)
        )
    )";
    // imported files tracking tables
    const std::string importedDirectoriesTable = R"(
        CREATE TABLE IF NOT EXISTS imported_directories (
            dir_id INTEGER PRIMARY KEY AUTOINCREMENT,
            dir_path TEXT NOT NULL UNIQUE
        )
    )";
    const std::string importedFilesTable = R"(
        CREATE TABLE IF NOT EXISTS imported_files (
            dir_id INTEGER NOT NULL,
            filename TEXT NOT NULL,
            
            PRIMARY KEY (dir_id, filename),

            FOREIGN KEY (dir_id) REFERENCES imported_directories(dir_id) ON DELETE CASCADE
        )
    )";

    const std::string tagsTable = R"(
        CREATE TABLE IF NOT EXISTS tags (
            tag_id INTEGER PRIMARY KEY,
            tag TEXT NOT NULL UNIQUE,
            platform INTEGER NOT NULL DEFAULT 0,
            category INTEGER NOT NULL DEFAULT 0,
            translated_tag TEXT,
            count INTEGER DEFAULT 0
        )
    )";

    const std::string tagParentTable = R"(
        CREATE TABLE IF NOT EXISTS tag_parent (
            tag_id INTEGER NOT NULL,
            parent_tag_id INTEGER NOT NULL,
            PRIMARY KEY (tag_id, parent_tag_id),
            FOREIGN KEY (tag_id) REFERENCES tags(tag_id) ON DELETE CASCADE,
            FOREIGN KEY (parent_tag_id) REFERENCES tags(tag_id) ON DELETE CASCADE
        )
    )";

    const std::string tagAliasTable = R"(
        CREATE TABLE IF NOT EXISTS tag_alias (
            tag_id INTEGER NOT NULL,
            canonical_tag_id INTEGER NOT NULL,
            PRIMARY KEY (tag_id, canonical_tag_id),
            FOREIGN KEY (tag_id) REFERENCES tags(tag_id) ON DELETE CASCADE,
            FOREIGN KEY (canonical_tag_id) REFERENCES tags(tag_id) ON DELETE CASCADE
        )
    )";

    const std::vector<std::string> tables = {metadataTable,
                                              picturesTable,
                                              tagsTable,
                                              tagParentTable,
                                              tagAliasTable,
                                              pictureTagsTable,
                                              pictureFilesTable,
                                              pictureSourceTable,
                                              picMetadataTable,
                                              platformTagClassificationTable,
                                              importedDirectoriesTable,
                                              importedFilesTable};
    const std::vector<std::string> indexes = {
        "CREATE INDEX IF NOT EXISTS idx_picture_tags_id ON picture_tags(id)",
        "CREATE INDEX IF NOT EXISTS idx_picture_tags_tag_id ON picture_tags(tag_id)",
        "CREATE INDEX IF NOT EXISTS idx_picture_file_paths_id ON picture_file_paths(id)",
        "CREATE INDEX IF NOT EXISTS idx_picture_source_id ON picture_source(id)",
        "CREATE INDEX IF NOT EXISTS idx_tags_tag ON tags(tag)",
        "CREATE INDEX IF NOT EXISTS idx_tags_platform_tag ON tags(platform, tag)",
        "CREATE INDEX IF NOT EXISTS idx_tags_category ON tags(category)",
        "CREATE INDEX IF NOT EXISTS idx_picture_metadata_author_id ON picture_metadata(author_id)",
        "CREATE INDEX IF NOT EXISTS idx_picture_metadata_author_name ON picture_metadata(author_name)",
        "CREATE INDEX IF NOT EXISTS idx_picture_metadata_title ON picture_metadata(title)",
        "CREATE INDEX IF NOT EXISTS idx_picture_tags_id_2 ON picture_tags(tag_id, id)",
        "CREATE INDEX IF NOT EXISTS idx_imported_directories_dir_path ON imported_directories(dir_path)",
        "CREATE INDEX IF NOT EXISTS idx_tag_parent_parent ON tag_parent(parent_tag_id)",
        "CREATE INDEX IF NOT EXISTS idx_tag_alias_canonical ON tag_alias(canonical_tag_id)",
        "CREATE INDEX IF NOT EXISTS idx_tag_alias_tag ON tag_alias(tag_id)"};
    beginTransaction();
    for (const auto& tableSql : tables) {
        if (!execute(tableSql)) {
            Error() << "Failed to create table:" << sqlite3_errmsg(db);
            rollbackTransaction();
            return false;
        }
    }
    for (const auto& indexSql : indexes) {
        if (!execute(indexSql)) {
            Error() << "Failed to create index:" << sqlite3_errmsg(db);
            rollbackTransaction();
            return false;
        }
    }

    commitTransaction();
    return true;
}
void PicDatabase::refreshTagMapping() const {
    cache.clearTagMapping();
    initTagMapping();
}

void PicDatabase::initTagMapping() const {
    if (cache.tagMappingLoaded()) return;

    SQLiteStatement stmt;
    std::vector<TagStr> tags;
    std::unordered_map<std::string, uint32_t> tagToId;
    std::unordered_map<uint32_t, TagStr> tagById;

    stmt = prepare("SELECT tag_id, tag, platform, category, translated_tag FROM tags ORDER BY tag_id ASC");
    if (!stmt.get()) {
        Error() << "Failed to prepare statement for fetching tags.";
        return;
    }
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        uint32_t id = sqlite3_column_int(stmt.get(), 0);
        const char* tag = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        int platform = sqlite3_column_int(stmt.get(), 2);
        int category = sqlite3_column_int(stmt.get(), 3);
        const char* translatedTag = sqlite3_column_type(stmt.get(), 4) != SQLITE_NULL
            ? reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4)) : "";

        TagStr tagStr{tag ? tag : "", platform, category, translatedTag ? translatedTag : ""};
        tags.emplace_back(tagStr);
        tagById[id] = tagStr;
        tagToId[tag ? tag : ""] = id;
    }

    cache.loadTagMapping(std::move(tagToId), std::move(tags), std::move(tagById));

    std::unordered_map<uint32_t, std::vector<uint32_t>> tagChildren;
    std::unordered_map<uint32_t, std::vector<uint32_t>> tagParents;
    stmt = prepare("SELECT tag_id, parent_tag_id FROM tag_parent");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        uint32_t childId = sqlite3_column_int(stmt.get(), 0);
        uint32_t parentId = sqlite3_column_int(stmt.get(), 1);
        tagChildren[parentId].push_back(childId);
        tagParents[childId].push_back(parentId);
    }
    cache.loadTagHierarchy(std::move(tagChildren), std::move(tagParents));

    std::unordered_map<uint32_t, uint32_t> aliasToCanonical;
    std::unordered_map<uint32_t, std::vector<uint32_t>> tagAliases;
    stmt = prepare("SELECT tag_id, canonical_tag_id FROM tag_alias");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        uint32_t aliasTagId = sqlite3_column_int(stmt.get(), 0);
        uint32_t canonicalId = sqlite3_column_int(stmt.get(), 1);
        aliasToCanonical[aliasTagId] = canonicalId;
        tagAliases[canonicalId].push_back(aliasTagId);
    }
    cache.loadTagAliases(std::move(aliasToCanonical), std::move(tagAliases));

    Info() << "Tag mappings loaded. Total tags:" << tags.size();
}
void PicDatabase::initImportedFiles() const {
    if (cache.importedFileLoaded()) return;
    SQLiteStatement stmt;
    std::unordered_map<std::string, std::unordered_set<std::string>> importedFiles;
    stmt = prepare(R"(
        SELECT id.dir_path, f.filename
        FROM imported_files f
        JOIN imported_directories id ON f.dir_id = id.dir_id
    )");
    if (!stmt.get()) {
        Error() << "Failed to prepare statement for fetching imported files.";
        return;
    }
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const char* dirPathCStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        const char* fileNameCStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        if (dirPathCStr && fileNameCStr) {
            std::string dirPath(dirPathCStr);
            std::string fileName(fileNameCStr);
            importedFiles[dirPath].insert(fileName);
        }
    }
    cache.loadImportedFiles(std::move(importedFiles));
    Info() << "Imported files tracking initialized. Directories:" << importedFiles.size();
}

// insert functions

bool PicDatabase::insertPicture(const ParsedPicture& picInfo) const {
    SQLiteStatement stmt;
    // insert into pictures table
    stmt = prepare(R"(
        INSERT OR IGNORE INTO pictures(
            id, width, height, size, file_type, edit_time, download_time, restrict_type
        ) VALUES (
            ?, ?, ?, ?, ?, ?, ?, ?
        )
    )");
    sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(picInfo.id));
    sqlite3_bind_int(stmt.get(), 2, picInfo.width);
    sqlite3_bind_int(stmt.get(), 3, picInfo.height);
    sqlite3_bind_int64(stmt.get(), 4, picInfo.size);
    sqlite3_bind_int(stmt.get(), 5, static_cast<int>(picInfo.fileType));
    sqlite3_bind_text(stmt.get(), 6, picInfo.editTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 7, picInfo.downloadTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 8, static_cast<int>(picInfo.restrictType));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to insert picture: " << sqlite3_errmsg(db);
        return false;
    }
    // insert into picture_file_paths table
    stmt = prepare(R"(
            INSERT OR IGNORE INTO picture_file_paths(
                id, file_path
            ) VALUES (?, ?)
        )");
    sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(picInfo.id));
    sqlite3_bind_text(stmt.get(), 2, picInfo.filePath.generic_u8string().c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to insert picture_file_path: " << sqlite3_errmsg(db);
        return false;
    }
    // insert into picture_source table
    if (picInfo.identifier.platform == PlatformType::Unknown) return true; // no source info to insert
    stmt = prepare(R"(
            INSERT OR IGNORE INTO picture_source(
                id, platform, platform_id, image_index
            ) VALUES (
                ?, ?, ?, ?
            )
        )");
    sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(picInfo.id));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(picInfo.identifier.platform));
    sqlite3_bind_int64(stmt.get(), 3, picInfo.identifier.platformID);
    sqlite3_bind_int(stmt.get(), 4, picInfo.identifier.imageIndex);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to insert picture_source: " << sqlite3_errmsg(db);
        return false;
    }

    return true;
}
bool PicDatabase::insertMetadata(const ParsedMetadata& metadataInfo) {
    if (metadataInfo.updateIfExists) {
        Warn() << "insertMetadata called with updateIfExists=true, redirecting to updateMetadata.";
        return updateMetadata(metadataInfo);
    }
    if (metadataInfo.id == 0) return false; // invalid metadata ID
    SQLiteStatement stmt;
    // insert into picture_metadata table
    stmt = prepare(R"(
        INSERT OR IGNORE INTO picture_metadata(
            platform, platform_id, date, author_id, author_name, author_nick, 
            author_description, title, description, view_count, like_count, bookmark_count,
            reply_count, forward_count, quote_count, restrict_type, ai_type
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(metadataInfo.platformType));
    sqlite3_bind_int64(stmt.get(), 2, metadataInfo.id);
    sqlite3_bind_text(stmt.get(), 3, metadataInfo.date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 4, metadataInfo.authorID);
    sqlite3_bind_text(stmt.get(), 5, metadataInfo.authorName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 6, metadataInfo.authorNick.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 7, metadataInfo.authorDescription.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 8, metadataInfo.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 9, metadataInfo.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 10, metadataInfo.viewCount);
    sqlite3_bind_int(stmt.get(), 11, metadataInfo.likeCount);
    sqlite3_bind_int(stmt.get(), 12, metadataInfo.bookmarkCount);
    sqlite3_bind_int(stmt.get(), 13, metadataInfo.replyCount);
    sqlite3_bind_int(stmt.get(), 14, metadataInfo.forwardCount);
    sqlite3_bind_int(stmt.get(), 15, metadataInfo.quoteCount);
    sqlite3_bind_int(stmt.get(), 16, static_cast<int>(metadataInfo.restrictType));
    sqlite3_bind_int(stmt.get(), 17, static_cast<int>(metadataInfo.aiType));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to insert picture_metadata: " << sqlite3_errmsg(db);
        return false;
    }
    // insert tags into tags table (but NOT into picture_tags yet — pictures may not exist yet)
    std::vector<uint32_t> tagIds;
    for (const auto& tag : metadataInfo.tags) {
        if (!cache.tagExists(tag)) {
            stmt = prepare(R"(
                INSERT OR IGNORE INTO tags(tag, platform, category, count) VALUES (?, ?, 0, 0)
            )");
            sqlite3_bind_text(stmt.get(), 1, tag.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt.get(), 2, static_cast<int>(metadataInfo.platformType));
            sqlite3_step(stmt.get());

            uint32_t newId;
            stmt = prepare(R"(
                SELECT tag_id FROM tags WHERE tag = ?
            )");
            sqlite3_bind_text(stmt.get(), 1, tag.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
                newId = static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 0));
            } else {
                Error() << "Failed to get tag_id for tag: " << tag;
                continue;
            }
            cache.addTagToCacheWithId(tag, newId, static_cast<int>(metadataInfo.platformType), 0);
        }
        
        auto classification = getPlatformTagClassification(metadataInfo.platformType, tag);
        if (classification.has_value()) {
            uint32_t tagId = cache.getTagId(tag);
            stmt = prepare(R"(
                UPDATE tags SET category = ? WHERE tag_id = ?
            )");
            sqlite3_bind_int(stmt.get(), 1, classification.value() ? static_cast<int>(TagCategory::Character) : static_cast<int>(TagCategory::Attribute));
            sqlite3_bind_int(stmt.get(), 2, tagId);
            if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                Error() << "Failed to update tag category: " << sqlite3_errmsg(db);
            }
        }
        
        tagIds.push_back(cache.getTagId(tag));
    }
    newMetadataIds.insert(PlatformID{metadataInfo.platformType, metadataInfo.id});
    newMetadataTagIds[PlatformID{metadataInfo.platformType, metadataInfo.id}] = std::move(tagIds);
    return true;
}
bool PicDatabase::updateMetadata(const ParsedMetadata& metadataInfo) {
    if (metadataInfo.id == 0) return false; // invalid metadata ID
    SQLiteStatement stmt;
    // update picture_metadata table
    stmt = prepare(R"(
        INSERT INTO picture_metadata(
            platform, platform_id, date, author_id, author_name, author_nick, 
            author_description, title, description, view_count, like_count, bookmark_count,
            reply_count, forward_count, quote_count, restrict_type, ai_type
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(platform, platform_id) DO UPDATE SET
            date=excluded.date,
            author_id=excluded.author_id,
            author_name=excluded.author_name,
            author_nick=excluded.author_nick,
            author_description=excluded.author_description,
            title=excluded.title,
            description=excluded.description,
            view_count=excluded.view_count,
            like_count=excluded.like_count,
            bookmark_count=excluded.bookmark_count,
            reply_count=excluded.reply_count,
            forward_count=excluded.forward_count,
            quote_count=excluded.quote_count,
            -- only update restrict_type when excluded.restrict_type is not NULL and greater than existing
            restrict_type = CASE
                WHEN excluded.restrict_type IS NOT NULL
                     AND (picture_metadata.restrict_type IS NULL OR excluded.restrict_type > picture_metadata.restrict_type)
                THEN excluded.restrict_type
                ELSE picture_metadata.restrict_type
            END,
            -- only update ai_type when excluded.ai_type is not NULL and greater than existing
            ai_type = CASE
                WHEN excluded.ai_type IS NOT NULL
                     AND (picture_metadata.ai_type IS NULL OR excluded.ai_type > picture_metadata.ai_type)
                THEN excluded.ai_type
                ELSE picture_metadata.ai_type
            END
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(metadataInfo.platformType));
    sqlite3_bind_int64(stmt.get(), 2, metadataInfo.id);
    sqlite3_bind_text(stmt.get(), 3, metadataInfo.date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 4, metadataInfo.authorID);
    sqlite3_bind_text(stmt.get(), 5, metadataInfo.authorName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 6, metadataInfo.authorNick.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 7, metadataInfo.authorDescription.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 8, metadataInfo.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 9, metadataInfo.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 10, metadataInfo.viewCount);
    sqlite3_bind_int(stmt.get(), 11, metadataInfo.likeCount);
    sqlite3_bind_int(stmt.get(), 12, metadataInfo.bookmarkCount);
    sqlite3_bind_int(stmt.get(), 13, metadataInfo.replyCount);
    sqlite3_bind_int(stmt.get(), 14, metadataInfo.forwardCount);
    sqlite3_bind_int(stmt.get(), 15, metadataInfo.quoteCount);
    sqlite3_bind_int(stmt.get(), 16, static_cast<int>(metadataInfo.restrictType));
    sqlite3_bind_int(stmt.get(), 17, static_cast<int>(metadataInfo.aiType));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to update picture_metadata: " << sqlite3_errmsg(db);
        return false;
    }
    // insert tags into tags table (but NOT into picture_tags yet — pictures may not exist yet)
    std::vector<uint32_t> tagIds;
    for (const auto& tag : metadataInfo.tags) {
        if (!cache.tagExists(tag)) {
            stmt = prepare(R"(
                INSERT OR IGNORE INTO tags(tag, platform, category, count) VALUES (?, ?, 0, 0)
            )");
            sqlite3_bind_text(stmt.get(), 1, tag.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt.get(), 2, static_cast<int>(metadataInfo.platformType));
            sqlite3_step(stmt.get());

            uint32_t newId;
            stmt = prepare(R"(
                SELECT tag_id FROM tags WHERE tag = ?
            )");
            sqlite3_bind_text(stmt.get(), 1, tag.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
                newId = static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 0));
            } else {
                Error() << "Failed to get tag_id for tag: " << tag;
                continue;
            }
            cache.addTagToCacheWithId(tag, newId, static_cast<int>(metadataInfo.platformType), 0);
        }
        tagIds.push_back(cache.getTagId(tag));
    }
    newMetadataIds.insert(PlatformID{metadataInfo.platformType, metadataInfo.id});
    newMetadataTagIds[PlatformID{metadataInfo.platformType, metadataInfo.id}] = std::move(tagIds);
    if (metadataInfo.tags.size() == metadataInfo.tagsTransl.size()) {
        for (size_t i = 0; i < metadataInfo.tags.size(); ++i) {
            uint32_t tagId = cache.getTagId(metadataInfo.tags[i]);
            stmt = prepare(R"(
                UPDATE tags SET translated_tag = ? WHERE tag_id = ?
            )");
            sqlite3_bind_text(stmt.get(), 1, metadataInfo.tagsTransl[i].c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt.get(), 2, tagId);
            if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                Error() << "Failed to update tag translated_tag: " << sqlite3_errmsg(db);
                return false;
            }
        }
    }
    return true;
}

// query functions

PicInfo PicDatabase::getPicInfo(uint64_t id) const {
    PicInfo info{};
    SQLiteStatement stmt;

    // query main picture info
    stmt = prepare("SELECT width, height, size, file_type, edit_time, download_time, feature_hash, restrict_type, ai_type FROM "
                   "pictures WHERE id = ?");
    sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(id));
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        info.id = id;
        info.width = sqlite3_column_int(stmt.get(), 0);
        info.height = sqlite3_column_int(stmt.get(), 1);
        info.size = sqlite3_column_int(stmt.get(), 2);
        info.fileType = static_cast<ImageFormat>(sqlite3_column_int(stmt.get(), 3));
        info.editTime = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4)));
        info.downloadTime = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 5)));
        const void* featureHashBlob = sqlite3_column_blob(stmt.get(), 6);
        int featureHashSize = sqlite3_column_bytes(stmt.get(), 6);
        if (featureHashBlob && featureHashSize == sizeof(info.featureHash)) {
            std::memcpy(info.featureHash.data(), featureHashBlob, sizeof(info.featureHash));
        } else {
            info.featureHash.fill(0);
        }
        info.restrictType = static_cast<RestrictType>(sqlite3_column_int(stmt.get(), 7));
        info.aiType = static_cast<AIType>(sqlite3_column_int(stmt.get(), 8));
    } else {
        return info; // id 不存在，返回空对象
    }

    // query file paths
    stmt = prepare("SELECT file_path FROM picture_file_paths WHERE id = ?");
    sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(id));
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        info.filePaths.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0)));
    }

    // query tags
    stmt = prepare("SELECT tag_id, probability FROM picture_tags WHERE id = ?");
    sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(id));
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        PicTag picTag{};
        picTag.tagId = sqlite3_column_int(stmt.get(), 0);
        picTag.probability = static_cast<float>(sqlite3_column_double(stmt.get(), 1));
        info.tags.push_back(picTag);
    }

    // query source identifiers
    stmt = prepare("SELECT platform, platform_id, image_index FROM picture_source WHERE id = ?");
    sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(id));
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        ImageSource identifier;
        identifier.platform = static_cast<PlatformType>(sqlite3_column_int(stmt.get(), 0));
        identifier.platformID = sqlite3_column_int64(stmt.get(), 1);
        identifier.imageIndex = sqlite3_column_int(stmt.get(), 2);
        info.sourceIdentifiers.push_back(identifier);
    }

    return info;
}
std::vector<uint64_t> PicDatabase::getMetadataPicIds(const PlatformID& platformId) const {
    std::vector<uint64_t> picIds;
    SQLiteStatement stmt;

    stmt = prepare(R"(
            SELECT ps.id
            FROM picture_source ps
            WHERE ps.platform = ? AND ps.platform_id = ?
            ORDER BY ps.image_index ASC
        )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(platformId.platform));
    sqlite3_bind_int64(stmt.get(), 2, platformId.platformID);
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        uint64_t picID = int64_to_uint64(sqlite3_column_int64(stmt.get(), 0));
        picIds.push_back(picID);
    }

    return picIds;
}
std::vector<PicInfo> PicDatabase::getMetadataPicInfos(const PlatformID& platformId) const {
    std::vector<uint64_t> picIds = getMetadataPicIds(platformId);
    std::vector<PicInfo> picInfos;

    for (uint64_t picId : picIds) {
        PicInfo picInfo = getPicInfo(picId);
        picInfos.push_back(picInfo);
    }

    return picInfos;
}
Metadata PicDatabase::getMetadata(PlatformType platform, int64_t platformID) const {
    Metadata info{};
    SQLiteStatement stmt;

    // query main metadata
    stmt = prepare(R"(
        SELECT date, author_id, author_name, author_nick, author_description,
               title, description, view_count, like_count, bookmark_count,
               reply_count, forward_count, quote_count, restrict_type, ai_type
        FROM picture_metadata
        WHERE platform = ? AND platform_id = ?
        )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(platform));
    sqlite3_bind_int64(stmt.get(), 2, platformID);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        info.platformType = platform;
        info.id = platformID;
        info.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        info.authorID = sqlite3_column_int64(stmt.get(), 1);
        info.authorName = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
        info.authorNick = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
        info.authorDescription = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4));
        info.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 5));
        info.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 6));
        info.viewCount = sqlite3_column_int(stmt.get(), 7);
        info.likeCount = sqlite3_column_int(stmt.get(), 8);
        info.bookmarkCount = sqlite3_column_int(stmt.get(), 9);
        info.replyCount = sqlite3_column_int(stmt.get(), 10);
        info.forwardCount = sqlite3_column_int(stmt.get(), 11);
        info.quoteCount = sqlite3_column_int(stmt.get(), 12);
        info.restrictType = static_cast<RestrictType>(sqlite3_column_int(stmt.get(), 13));
        info.aiType = static_cast<AIType>(sqlite3_column_int(stmt.get(), 14));
    } else {
        return info;
    }

    // query tags from pictures belonging to this metadata post
    stmt = prepare(R"(
        SELECT DISTINCT pt.tag_id FROM picture_tags pt
        JOIN picture_source ps ON pt.id = ps.id
        WHERE ps.platform = ? AND ps.platform_id = ?
        )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(platform));
    sqlite3_bind_int64(stmt.get(), 2, platformID);
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        auto tagId = static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 0));
        info.tagIds.push_back(tagId);
    }

    return info;
}

// import functions

void PicDatabase::processAndImportSingleFile(const std::filesystem::path& filePath, ParserType parserType) {
    std::string ext = filePath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    try {
        if (ext == ".jpg" || ext == ".png" || ext == ".jpeg" || ext == ".gif" || ext == ".webp") {
            insertPicture(parsePicture(filePath, parserType));
        } else {
            switch (parserType) {
            case ParserType::PowerfulPixivDownloader: {
                std::vector<ParsedMetadata> parsedMetadata = powerfulPixivDownloaderMetadataParser(filePath);
                for (const auto& metadata : parsedMetadata) {
                    insertMetadata(metadata);
                }
                break;
            }
            case ParserType::GallerydlTwitter: {
                insertMetadata(gallerydlTwitterMetadataParser(filePath));
                break;
            }
            default:
                break;
            }
        }
    } catch (const std::exception& e) {
        Warn() << "Error processing file:" << filePath.c_str() << "Error:" << e.what();
    }
}
void PicDatabase::importFilesFromDirectory(const std::filesystem::path& directory,
                                           ParserType parserType,
                                           ProgressCallback progressCallback) {
    auto collectedFiles = collectFiles(directory);
    std::vector<std::filesystem::path> files;
    for (const auto& filePath : collectedFiles) {
        if (!isFileImported(filePath)) {
            files.push_back(filePath);
        }
    }
    disableForeignKeyRestriction();

    constexpr size_t BATCH_SIZE = 1000;
    size_t processed = 0;
    for (size_t i = 0; i < files.size(); i += BATCH_SIZE) {
        beginTransaction();
        auto batch_end = std::min(i + BATCH_SIZE, files.size());
        auto start_time = std::chrono::high_resolution_clock::now();
        for (size_t j = i; j < batch_end; j++) {
            processAndImportSingleFile(files[j], parserType);
            addImportedFile(files[j]);
            processed++;
        }
        if (!commitTransaction()) {
            Warn() << "Batch commit failed, rolling back";
            rollbackTransaction();
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        double speed = (batch_end - i) / elapsed.count();
        double eta = (files.size() - processed) / speed;
        int eta_minutes = static_cast<int>(eta) / 60;
        int eta_seconds = static_cast<int>(eta) % 60;
        if (progressCallback) {
            progressCallback(processed, files.size());
        }
        // Info() << "Processed files: " << processed << " | Speed: " << speed << " files/sec | ETA: " << eta_minutes << "m "
        //        << eta_seconds << "s";
    }
    syncMetadataAndPicTables();
    updateTagCounts();
    enableForeignKeyRestriction();
    // Info() << "Import completed. Total files processed:" << processed;
}
void PicDatabase::syncMetadataAndPicTables(std::unordered_set<PlatformID> newMetadataIds) const { // post-import operations
    SQLiteStatement stmt;
    // sync restrict_type and ai_type in pictures table
    if (newMetadataIds.empty()) {
        newMetadataIds = this->newMetadataIds;
    }
    for (const auto& metadataId : newMetadataIds) {
        stmt = prepare(R"(
            UPDATE pictures
            SET restrict_type = CASE
                WHEN restrict_type IS NULL OR restrict_type < (
                    SELECT restrict_type FROM picture_metadata WHERE platform = ? AND platform_id = ?
                )
                THEN (SELECT restrict_type FROM picture_metadata WHERE platform = ? AND platform_id = ?)
                ELSE restrict_type
            END,
            ai_type = CASE
                WHEN ai_type IS NULL OR ai_type < (
                    SELECT ai_type FROM picture_metadata WHERE platform = ? AND platform_id = ?
                )
                THEN (SELECT ai_type FROM picture_metadata WHERE platform = ? AND platform_id = ?)
                ELSE ai_type
            END,
            edit_time = (SELECT date FROM picture_metadata WHERE platform = ? AND platform_id = ?)
            WHERE id IN (
                SELECT id FROM picture_source WHERE platform = ? AND platform_id = ?
            )
        )");
        sqlite3_bind_int(stmt.get(), 1, static_cast<int>(metadataId.platform));
        sqlite3_bind_int64(stmt.get(), 2, metadataId.platformID);
        sqlite3_bind_int(stmt.get(), 3, static_cast<int>(metadataId.platform));
        sqlite3_bind_int64(stmt.get(), 4, metadataId.platformID);
        sqlite3_bind_int(stmt.get(), 5, static_cast<int>(metadataId.platform));
        sqlite3_bind_int64(stmt.get(), 6, metadataId.platformID);
        sqlite3_bind_int(stmt.get(), 7, static_cast<int>(metadataId.platform));
        sqlite3_bind_int64(stmt.get(), 8, metadataId.platformID);
        sqlite3_bind_int(stmt.get(), 9, static_cast<int>(metadataId.platform));
        sqlite3_bind_int64(stmt.get(), 10, metadataId.platformID);
        sqlite3_bind_int(stmt.get(), 11, static_cast<int>(metadataId.platform));
        sqlite3_bind_int64(stmt.get(), 12, metadataId.platformID);
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
            Warn() << "Failed to sync restrict_type and ai_type for platform:" << static_cast<int>(metadataId.platform)
                   << "platform_id:" << metadataId.platformID << "Error:" << sqlite3_errmsg(db);
        }
    }

    // sync deferred platform tags into picture_tags
    auto& deferredTagIds = const_cast<std::unordered_map<PlatformID, std::vector<uint32_t>>&>(this->newMetadataTagIds);
    for (const auto& metadataId : newMetadataIds) {
        auto tagIt = deferredTagIds.find(metadataId);
        if (tagIt == deferredTagIds.end()) continue;

        stmt = prepare(R"(
            SELECT id FROM picture_source WHERE platform = ? AND platform_id = ?
        )");
        sqlite3_bind_int(stmt.get(), 1, static_cast<int>(metadataId.platform));
        sqlite3_bind_int64(stmt.get(), 2, metadataId.platformID);

        std::vector<uint64_t> picIds;
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            picIds.push_back(int64_to_uint64(sqlite3_column_int64(stmt.get(), 0)));
        }

        for (uint64_t picId : picIds) {
            for (uint32_t tagId : tagIt->second) {
                stmt = prepare(R"(
                    INSERT OR IGNORE INTO picture_tags(id, tag_id, probability) VALUES (?, ?, 1.0)
                )");
                sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(picId));
                sqlite3_bind_int(stmt.get(), 2, static_cast<int>(tagId));
                if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                    Warn() << "Failed to insert platform tag into picture_tags:" << sqlite3_errmsg(db);
                }
            }
        }
    }
    deferredTagIds.clear();

    enableForeignKeyRestriction();
}
void PicDatabase::addImportedFile(const std::filesystem::path& filePath) const {
    if (isFileImported(filePath)) return;

    cache.addImportedFile(filePath);

    SQLiteStatement stmt;
    std::string dir = filePath.parent_path().string();
    std::string filename = filePath.filename().string();

    // Step 1: Insert or ignore the directory
    stmt = prepare(R"(
        INSERT INTO imported_directories (dir_path)
        VALUES (?)
        ON CONFLICT(dir_path) DO NOTHING
    )");
    sqlite3_bind_text(stmt.get(), 1, dir.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to insert directory: " << sqlite3_errmsg(db);

        return;
    }

    // Step 2: Get the dir_id
    int64_t dirId = -1;
    stmt = prepare(R"(
        SELECT dir_id FROM imported_directories WHERE dir_path = ?
    )");
    sqlite3_bind_text(stmt.get(), 1, dir.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        dirId = sqlite3_column_int64(stmt.get(), 0);
    } else {
        Error() << "Failed to fetch dir_id: " << sqlite3_errmsg(db);
        return;
    }

    // Step 3: Insert the file
    stmt = prepare(R"(
        INSERT INTO imported_files (dir_id, filename)
        VALUES (?, ?)
        ON CONFLICT(dir_id, filename) DO NOTHING
    )");
    sqlite3_bind_int64(stmt.get(), 1, dirId);
    sqlite3_bind_text(stmt.get(), 2, filename.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to insert imported file: " << sqlite3_errmsg(db);
    }
}
void PicDatabase::updateTagCounts() const {
    SQLiteStatement stmt;
    if (!execute(R"(
        UPDATE tags SET count = (
            SELECT COUNT(*) FROM picture_tags WHERE tag_id = tags.tag_id
        )
    )")) {
        Warn() << "Failed to count tags:" << sqlite3_errmsg(db);
    }
}

// Search functions

std::unordered_set<uint64_t> PicDatabase::tagSearch(const std::unordered_set<uint32_t>& includedTagIds,
                                                    const std::unordered_set<uint32_t>& excludedTagIds) const {
    std::unordered_set<uint64_t> results;
    
    if (includedTagIds.empty() && excludedTagIds.empty()) {
        SQLiteStatement stmt = prepare("SELECT id FROM pictures");
        if (!stmt.get()) {
            Error() << "Failed to prepare get all pictures statement:" << sqlite3_errmsg(db);
            return results;
        }
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            results.insert(int64_to_uint64(sqlite3_column_int64(stmt.get(), 0)));
        }
        return results;
    }
    
    if (includedTagIds.empty()) return results;

    std::string includedTagIdStr;
    std::string excludedTagIdStr;
    for (const auto& tagId : includedTagIds) {
        if (!includedTagIdStr.empty()) includedTagIdStr += ",";
        includedTagIdStr += std::to_string(tagId);
    }
    if (!excludedTagIds.empty()) {
        for (const auto& tagId : excludedTagIds) {
            if (!excludedTagIdStr.empty()) excludedTagIdStr += ",";
            excludedTagIdStr += std::to_string(tagId);
        }
    }

    std::string sql;
    if (excludedTagIds.empty()) {
        sql = R"(
            WITH RECURSIVE 
              inc_alias(root_id, tag_id) AS (
                SELECT tag_id, tag_id FROM tags WHERE tag_id IN ()" + includedTagIdStr + R"()
                UNION
                SELECT ia.root_id, ta.tag_id FROM tag_alias ta JOIN inc_alias ia ON ta.canonical_tag_id = ia.tag_id
                UNION
                SELECT ia.root_id, ta.canonical_tag_id FROM tag_alias ta JOIN inc_alias ia ON ta.tag_id = ia.tag_id
              ),
              inc_desc(root_id, tag_id) AS (
                SELECT root_id, tag_id FROM inc_alias
                UNION ALL
                SELECT inc_desc.root_id, tp.tag_id
                FROM tag_parent tp JOIN inc_desc ON tp.parent_tag_id = inc_desc.tag_id
              )
            SELECT pt.id
            FROM picture_tags pt
            JOIN inc_desc d ON pt.tag_id = d.tag_id
            GROUP BY pt.id
            HAVING COUNT(DISTINCT d.root_id) = )" + std::to_string(includedTagIds.size());
    } else {
        sql = R"(
            WITH RECURSIVE 
              inc_alias(root_id, tag_id) AS (
                SELECT tag_id, tag_id FROM tags WHERE tag_id IN ()" + includedTagIdStr + R"()
                UNION
                SELECT ia.root_id, ta.tag_id FROM tag_alias ta JOIN inc_alias ia ON ta.canonical_tag_id = ia.tag_id
                UNION
                SELECT ia.root_id, ta.canonical_tag_id FROM tag_alias ta JOIN inc_alias ia ON ta.tag_id = ia.tag_id
              ),
              inc_desc(root_id, tag_id) AS (
                SELECT root_id, tag_id FROM inc_alias
                UNION ALL
                SELECT inc_desc.root_id, tp.tag_id
                FROM tag_parent tp JOIN inc_desc ON tp.parent_tag_id = inc_desc.tag_id
              ),
              exc_alias(root_id, tag_id) AS (
                SELECT tag_id, tag_id FROM tags WHERE tag_id IN ()" + excludedTagIdStr + R"()
                UNION
                SELECT ea.root_id, ta.tag_id FROM tag_alias ta JOIN exc_alias ea ON ta.canonical_tag_id = ea.tag_id
                UNION
                SELECT ea.root_id, ta.canonical_tag_id FROM tag_alias ta JOIN exc_alias ea ON ta.tag_id = ea.tag_id
              ),
              exc_desc(root_id, tag_id) AS (
                SELECT root_id, tag_id FROM exc_alias
                UNION ALL
                SELECT exc_desc.root_id, tp.tag_id
                FROM tag_parent tp JOIN exc_desc ON tp.parent_tag_id = exc_desc.tag_id
              )
            SELECT pt.id
            FROM picture_tags pt
            JOIN inc_desc d ON pt.tag_id = d.tag_id
            WHERE pt.id NOT IN (
                SELECT pt2.id FROM picture_tags pt2 
                JOIN exc_desc e ON pt2.tag_id = e.tag_id
            )
            GROUP BY pt.id
            HAVING COUNT(DISTINCT d.root_id) = )" + std::to_string(includedTagIds.size());
    }

    SQLiteStatement stmt = prepare(sql);
    if (!stmt.get()) {
        Error() << "Failed to prepare tag search statement:" << sqlite3_errmsg(db);
        return results;
    }
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        results.insert(int64_to_uint64(sqlite3_column_int64(stmt.get(), 0)));
    }
    return results;
}
std::unordered_set<PlatformID>
PicDatabase::textSearch(const std::string& searchText, PlatformType platformType, SearchField searchField) const {
    std::unordered_set<PlatformID> results;
    if (searchText.empty() || searchField == SearchField::None) return results;
    SQLiteStatement stmt;
    bool isNumeric =
        !searchText.empty() && std::all_of(searchText.begin(), searchText.end(), [](char c) { return std::isdigit(c); });

    std::string searchFieldStr;
    std::string likePattern;
    switch (searchField) {
    case SearchField::PlatformID:
        searchFieldStr = "platform_id";
        break;
    case SearchField::AuthorID:
        searchFieldStr = "author_id";
        break;
    case SearchField::AuthorName:
        searchFieldStr = "author_name";
        break;
    case SearchField::AuthorNick:
        searchFieldStr = "author_nick";
        break;
    case SearchField::Title:
        searchFieldStr = "title";
        break;
    default:
        return results;
    }

    switch (searchField) {
    case SearchField::PlatformID:
    case SearchField::AuthorID:
        if (isNumeric) {
            if (platformType != PlatformType::Unknown) {
                searchFieldStr = "platform = " + std::to_string(static_cast<int>(platformType)) + " AND " + searchFieldStr;
            }
            stmt = prepare("SELECT platform, platform_id FROM picture_metadata WHERE " + searchFieldStr + " = ?");
            sqlite3_bind_int64(stmt.get(), 1, std::stoll(searchText));
        } else {
            return results;
        }
        break;
    case SearchField::AuthorName:
    case SearchField::AuthorNick:
    case SearchField::Title:
        if (platformType != PlatformType::Unknown) {
            searchFieldStr = "platform = " + std::to_string(static_cast<int>(platformType)) + " AND " + searchFieldStr;
        }
        stmt = prepare(" SELECT platform, platform_id FROM picture_metadata WHERE " + searchFieldStr + " LIKE ? COLLATE NOCASE");
        likePattern = searchText + "%";
        sqlite3_bind_text(stmt.get(), 1, likePattern.c_str(), -1, SQLITE_TRANSIENT);
        break;
    default:
        return results;
    }

    if (!stmt.get()) {
        Error() << "Failed to prepare text search statement:" << sqlite3_errmsg(db);
        return results;
    }
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        PlatformID platformID{};
        platformID.platform = static_cast<PlatformType>(sqlite3_column_int(stmt.get(), 0));
        platformID.platformID = sqlite3_column_int64(stmt.get(), 1);
        results.insert(platformID);
    }
    return results;
}

// Get tag counts

std::vector<TagCount> PicDatabase::getTagCounts() const {
    std::vector<TagCount> tagCounts;
    SQLiteStatement stmt = prepare("SELECT tag_id, tag, platform, category, translated_tag, count FROM tags ORDER BY count DESC");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        uint32_t tagId = sqlite3_column_int(stmt.get(), 0);
        std::string tag = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        int platform = sqlite3_column_int(stmt.get(), 2);
        int category = sqlite3_column_int(stmt.get(), 3);
        const char* translatedTag = sqlite3_column_type(stmt.get(), 4) != SQLITE_NULL
            ? reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4)) : "";
        uint32_t count = sqlite3_column_int(stmt.get(), 5);
        
        uint32_t fileCount = 0;
        SQLiteStatement fileStmt = prepare("SELECT COUNT(*) FROM picture_tags WHERE tag_id = ?");
        sqlite3_bind_int(fileStmt.get(), 1, tagId);
        if (sqlite3_step(fileStmt.get()) == SQLITE_ROW) {
            fileCount = static_cast<uint32_t>(sqlite3_column_int(fileStmt.get(), 0));
        }
        
        tagCounts.emplace_back(TagCount{TagStr{tag, platform, category, translatedTag ? translatedTag : ""}, tagId, count, fileCount});
    }
    return tagCounts;
}

// tagger functions

std::string PicDatabase::getModelName() const {
    SQLiteStatement stmt = prepare(R"(
        SELECT value FROM metadata WHERE key = 'model_name'
    )");
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        return std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0)));
    }
    return "";
}
void PicDatabase::importTagSet(const std::string& modelName, const std::vector<std::pair<std::string, bool>>& tags) const {
    SQLiteStatement stmt;
    beginTransaction();
    // update model name
    stmt = prepare(R"(
        INSERT OR REPLACE INTO metadata(key, value) VALUES ('model_name', ?)
    )");
    sqlite3_bind_text(stmt.get(), 1, modelName.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to insert/update model_name: " << sqlite3_errmsg(db);
    }

    // insert tags with INSERT OR IGNORE to preserve existing tag_ids
    for (const auto& [tag, isCharacter] : tags) {
        int category = isCharacter ? static_cast<int>(TagCategory::Character) : static_cast<int>(TagCategory::Attribute);
        stmt = prepare(R"(
            INSERT OR IGNORE INTO tags(tag, platform, category, count) VALUES (?, 0, ?, 0)
        )");
        sqlite3_bind_text(stmt.get(), 1, tag.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt.get(), 2, category);
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
            Error() << "Failed to insert tag: " << sqlite3_errmsg(db);
        }
    }
    if (!commitTransaction()) {
        Error() << "Import tag set failed, rolling back";
        rollbackTransaction();
    }
    // update in-memory tag mapping
    cache.clearTagMapping();
    initTagMapping();
}
std::vector<std::pair<uint64_t, std::vector<std::filesystem::path>>> PicDatabase::getUntaggedPics() const {
    std::vector<std::pair<uint64_t, std::vector<std::filesystem::path>>> untaggedPics;
    SQLiteStatement stmt = prepare(R"(
        SELECT p.id, pfp.file_path
        FROM pictures p
        LEFT JOIN picture_tags pt ON p.id = pt.id
        LEFT JOIN picture_file_paths pfp ON p.id = pfp.id
        WHERE pt.id IS NULL
    )");
    std::unordered_map<uint64_t, std::vector<std::filesystem::path>> picMap;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        uint64_t picID = int64_to_uint64(sqlite3_column_int64(stmt.get(), 0));
        std::filesystem::path filePath = std::filesystem::path(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1)));
        picMap[picID].push_back(filePath);
    }
    for (const auto& [picID, filePaths] : picMap) {
        untaggedPics.emplace_back(picID, filePaths);
    }
    return untaggedPics;
}
void PicDatabase::updatePicTags(uint64_t picID,
                                const std::vector<PicTag>& picTags,
                                RestrictType restrictType,
                                const std::vector<uint8_t>& featureHash) const {
    SQLiteStatement stmt;
    // delete existing tags
    stmt = prepare(R"(
        DELETE FROM picture_tags WHERE id = ?
    )");
    sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(picID));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to delete existing picture_tags: " << sqlite3_errmsg(db);
    }
    // insert new tags
    for (const auto& picTag : picTags) {
        stmt = prepare(R"(
            INSERT INTO picture_tags(id, tag_id, probability) VALUES (?, ?, ?)
        )");
        sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(picID));
        sqlite3_bind_int(stmt.get(), 2, picTag.tagId);
        sqlite3_bind_double(stmt.get(), 3, static_cast<double>(picTag.probability));
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
            Error() << "Failed to insert picture_tag: " << sqlite3_errmsg(db);
        }
    }
    // update restrict_type and feature_hash in pictures table
    stmt = prepare(R"(
        UPDATE pictures SET restrict_type = ?, feature_hash = ? WHERE id = ?
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(restrictType));
    sqlite3_bind_blob(stmt.get(), 2, featureHash.data(), static_cast<int>(featureHash.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 3, uint64_to_int64(picID));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to update pictures restrict_type and feature_hash: " << sqlite3_errmsg(db);
    }
}

bool PicDatabase::classifyPlatformTag(PlatformType platform, const std::string& tag, bool isCharacter) const {
    SQLiteStatement stmt = prepare(R"(
        INSERT OR REPLACE INTO platform_tag_classification(platform, tag, is_character)
        VALUES (?, ?, ?)
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(platform));
    sqlite3_bind_text(stmt.get(), 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 3, isCharacter ? 1 : 0);
    
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to classify platform tag: " << sqlite3_errmsg(db);
        return false;
    }
    return true;
}

std::optional<bool> PicDatabase::getPlatformTagClassification(PlatformType platform, const std::string& tag) const {
    SQLiteStatement stmt = prepare(R"(
        SELECT is_character FROM platform_tag_classification
        WHERE platform = ? AND tag = ?
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(platform));
    sqlite3_bind_text(stmt.get(), 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        return sqlite3_column_int(stmt.get(), 0) != 0;
    }
    return std::nullopt;
}

std::unordered_map<std::string, bool> PicDatabase::getAllPlatformTagClassifications(PlatformType platform) const {
    std::unordered_map<std::string, bool> classifications;
    SQLiteStatement stmt = prepare(R"(
        SELECT tag, is_character FROM platform_tag_classification
        WHERE platform = ?
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(platform));
    
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        std::string tag = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        bool isCharacter = sqlite3_column_int(stmt.get(), 1) != 0;
        classifications[tag] = isCharacter;
    }
    return classifications;
}

bool PicDatabase::deletePlatformTagClassification(PlatformType platform, const std::string& tag) const {
    SQLiteStatement stmt = prepare(R"(
        DELETE FROM platform_tag_classification
        WHERE platform = ? AND tag = ?
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(platform));
    sqlite3_bind_text(stmt.get(), 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to delete platform tag classification: " << sqlite3_errmsg(db);
        return false;
    }
    return true;
}
void PicDatabase::syncClassifiedPlatformTagsToPictureTags() const {
    std::unordered_map<std::string, bool> pixivClassifications = getAllPlatformTagClassifications(PlatformType::Pixiv);
    if (pixivClassifications.empty()) return;
    
    beginTransaction();
    
    for (const auto& [tag, isCharacter] : pixivClassifications) {
        int category = isCharacter ? static_cast<int>(TagCategory::Character) : static_cast<int>(TagCategory::Attribute);
        
        SQLiteStatement stmt = prepare(R"(
            UPDATE tags SET category = ? WHERE tag = ?
        )");
        sqlite3_bind_int(stmt.get(), 1, category);
        sqlite3_bind_text(stmt.get(), 2, tag.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt.get());
    }
    
    commitTransaction();
    Info() << "Synced" << pixivClassifications.size() << "classified platform tags.";
}

std::optional<uint32_t> PicDatabase::getTagIdByTagText(const std::string& tagText) const {
    uint32_t cachedId = cache.getTagId(tagText);
    if (cachedId != 0) return cachedId;

    SQLiteStatement stmt = prepare(R"(
        SELECT tag_id FROM tags WHERE tag = ? LIMIT 1
    )");
    sqlite3_bind_text(stmt.get(), 1, tagText.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        return static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 0));
    }
    return std::nullopt;
}

bool PicDatabase::deleteTag(uint32_t tagId) const {
    SQLiteStatement delPictureTags = prepare(R"(
        DELETE FROM picture_tags WHERE tag_id = ?
    )");
    sqlite3_bind_int(delPictureTags.get(), 1, tagId);
    sqlite3_step(delPictureTags.get());
    
    SQLiteStatement delTag = prepare(R"(
        DELETE FROM tags WHERE tag_id = ?
    )");
    sqlite3_bind_int(delTag.get(), 1, tagId);
    
    if (sqlite3_step(delTag.get()) != SQLITE_DONE) {
        Error() << "Failed to delete tag: " << sqlite3_errmsg(db);
        return false;
    }
    return true;
}

bool PicDatabase::deleteTag(const std::string& tagText) const {
    SQLiteStatement findStmt = prepare(R"(
        SELECT tag_id FROM tags WHERE tag = ? LIMIT 1
    )");
    sqlite3_bind_text(findStmt.get(), 1, tagText.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(findStmt.get()) != SQLITE_ROW) {
        Warn() << "Tag not found for deletion:" << tagText;
        return false;
    }
    uint32_t tagId = sqlite3_column_int(findStmt.get(), 0);
    
    SQLiteStatement delPictureTags = prepare(R"(
        DELETE FROM picture_tags WHERE tag_id = ?
    )");
    sqlite3_bind_int(delPictureTags.get(), 1, tagId);
    sqlite3_step(delPictureTags.get());
    
    SQLiteStatement delTag = prepare(R"(
        DELETE FROM tags WHERE tag_id = ?
    )");
    sqlite3_bind_int(delTag.get(), 1, tagId);
    
    if (sqlite3_step(delTag.get()) != SQLITE_DONE) {
        Error() << "Failed to delete tag: " << sqlite3_errmsg(db);
        return false;
    }
    return true;
}

std::optional<PlatformType> PicDatabase::getPlatformTypeByTagText(const std::string& tagText) const {
    SQLiteStatement stmt = prepare(R"(
        SELECT platform FROM tags WHERE tag = ? AND platform != 0 LIMIT 1
    )");
    sqlite3_bind_text(stmt.get(), 1, tagText.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        return static_cast<PlatformType>(sqlite3_column_int(stmt.get(), 0));
    }
    return std::nullopt;
}

bool PicDatabase::addTagToPicture(uint64_t picId, uint32_t tagId, float probability) const {
    SQLiteStatement stmt = prepare(R"(
        INSERT OR IGNORE INTO picture_tags(id, tag_id, probability) VALUES (?, ?, ?)
    )");
    sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(picId));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(tagId));
    sqlite3_bind_double(stmt.get(), 3, static_cast<double>(probability));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to add tag to picture: " << sqlite3_errmsg(db);
        return false;
    }

    SQLiteStatement updateCount = prepare(R"(
        UPDATE tags SET count = count + 1 WHERE tag_id = ?
    )");
    sqlite3_bind_int(updateCount.get(), 1, static_cast<int>(tagId));
    sqlite3_step(updateCount.get());
    return true;
}

bool PicDatabase::removeTagFromPicture(uint64_t picId, uint32_t tagId) const {
    SQLiteStatement stmt = prepare(R"(
        DELETE FROM picture_tags WHERE id = ? AND tag_id = ?
    )");
    sqlite3_bind_int64(stmt.get(), 1, uint64_to_int64(picId));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(tagId));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to remove tag from picture: " << sqlite3_errmsg(db);
        return false;
    }
    if (sqlite3_changes(db) > 0) {
        SQLiteStatement updateCount = prepare(R"(
            UPDATE tags SET count = MAX(count - 1, 0) WHERE tag_id = ?
        )");
        sqlite3_bind_int(updateCount.get(), 1, static_cast<int>(tagId));
        sqlite3_step(updateCount.get());
    }
    return true;
}

bool PicDatabase::setTagCategory(uint32_t tagId, TagCategory category) const {
    SQLiteStatement stmt = prepare(R"(
        UPDATE tags SET category = ? WHERE tag_id = ?
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(category));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(tagId));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to set tag category: " << sqlite3_errmsg(db);
        return false;
    }
    auto& cache = DbCache::getInstance();
    cache.updateTagCategory(tagId, static_cast<int>(category));
    return true;
}

bool PicDatabase::setTagParent(uint32_t childTagId, uint32_t parentTagId) {
    if (childTagId == parentTagId) {
        Warn() << "Cannot set tag as its own parent";
        return false;
    }
    if (cache.wouldCreateCycle(childTagId, parentTagId)) {
        Warn() << "Setting parent would create cycle: child=" << childTagId << " parent=" << parentTagId;
        return false;
    }
    SQLiteStatement stmt = prepare(R"(
        INSERT OR IGNORE INTO tag_parent(tag_id, parent_tag_id) VALUES (?, ?)
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(childTagId));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(parentTagId));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to set tag parent: " << sqlite3_errmsg(db);
        return false;
    }
    cache.addParentChild(childTagId, parentTagId);
    return true;
}

bool PicDatabase::removeTagParent(uint32_t childTagId, uint32_t parentTagId) const {
    SQLiteStatement stmt = prepare(R"(
        DELETE FROM tag_parent WHERE tag_id = ? AND parent_tag_id = ?
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(childTagId));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(parentTagId));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to remove tag parent: " << sqlite3_errmsg(db);
        return false;
    }
    cache.removeParentChild(childTagId, parentTagId);
    return true;
}

bool PicDatabase::setTagAlias(uint32_t aliasTagId, uint32_t canonicalTagId) {
    if (aliasTagId == canonicalTagId) return false;

    auto group = cache.getAliasGroup(aliasTagId);
    for (uint32_t tid : group) {
        if (tid == canonicalTagId) {
            Warn() << "Tags already in the same alias group";
            return false;
        }
    }

    SQLiteStatement stmt = prepare(R"(
        INSERT OR IGNORE INTO tag_alias(tag_id, canonical_tag_id) VALUES (?, ?)
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(aliasTagId));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(canonicalTagId));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to set tag alias: " << sqlite3_errmsg(db);
        return false;
    }
    cache.addAlias(aliasTagId, canonicalTagId);
    return true;
}

bool PicDatabase::removeTagAlias(uint32_t aliasTagId, uint32_t canonicalTagId) const {
    SQLiteStatement stmt = prepare(R"(
        DELETE FROM tag_alias WHERE tag_id = ? AND canonical_tag_id = ?
    )");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(aliasTagId));
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(canonicalTagId));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to remove tag alias: " << sqlite3_errmsg(db);
        return false;
    }
    cache.removeAlias(aliasTagId, canonicalTagId);
    return true;
}

uint32_t PicDatabase::addTag(const std::string& tagName, TagCategory category) const {
    SQLiteStatement stmt = prepare(R"(
        SELECT tag_id FROM tags WHERE tag = ?
    )");
    sqlite3_bind_text(stmt.get(), 1, tagName.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        return static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 0));
    }

    stmt = prepare(R"(
        INSERT INTO tags(tag, platform, category, count) VALUES (?, 0, ?, 0)
    )");
    sqlite3_bind_text(stmt.get(), 1, tagName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(category));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        Error() << "Failed to add tag: " << sqlite3_errmsg(db);
        return 0;
    }
    uint32_t newId = static_cast<uint32_t>(sqlite3_last_insert_rowid(db));
    cache.clearTagMapping();
    initTagMapping();
    return newId;
}

void PicDatabase::syncMetadataFile(const PicInfo& picInfo, const Metadata* meta, const std::string& tagName, bool adding) const {
    if (picInfo.filePaths.empty()) return;

    std::filesystem::path picDir = picInfo.filePaths[0].parent_path();

    std::filesystem::path metaPath;
    if (meta && meta->platformType == PlatformType::Pixiv) {
        std::string prefix = std::to_string(meta->id) + "-";
        for (const auto& entry : std::filesystem::directory_iterator(picDir)) {
            if (!entry.is_regular_file()) continue;
            std::string fname = entry.path().filename().string();
            if (fname.find(prefix) == 0 && fname.find("-meta.json") != std::string::npos) {
                metaPath = entry.path();
                break;
            }
        }
    } else if (meta && meta->platformType == PlatformType::Twitter) {
        for (const auto& entry : std::filesystem::directory_iterator(picDir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".json") {
                std::string fname = entry.path().filename().string();
                if (fname.find("-meta") == std::string::npos) {
                    metaPath = entry.path();
                    break;
                }
            }
        }
    }

    if (metaPath.empty() || !std::filesystem::exists(metaPath)) {
        Warn() << "syncMetadataFile: no meta.json found in" << picDir.string();
        return;
    }

    try {
        std::ifstream inFile(metaPath);
        if (!inFile.is_open()) return;
        nlohmann::json j = nlohmann::json::parse(inFile, nullptr, false);
        inFile.close();
        if (j.is_discarded()) return;

        std::string tagKey;
        if (meta && meta->platformType == PlatformType::Pixiv) {
            tagKey = j.contains("tagsTranslOnly") ? "tagsTranslOnly" : "tags";
        } else {
            tagKey = "hashtags";
        }

        if (!j.contains(tagKey) || !j[tagKey].is_array()) {
            j[tagKey] = nlohmann::json::array();
        }

        auto& tags = j[tagKey];
        if (adding) {
            bool exists = false;
            for (const auto& t : tags) {
                if (t.get<std::string>() == tagName) { exists = true; break; }
            }
            if (!exists) tags.push_back(tagName);
        } else {
            for (auto it = tags.begin(); it != tags.end(); ++it) {
                if (it->get<std::string>() == tagName) {
                    tags.erase(it);
                    break;
                }
            }
        }

        std::ofstream outFile(metaPath);
        if (outFile.is_open()) {
            outFile << j.dump(2);
            outFile.close();
            Info() << "Synced metadata file:" << metaPath.string() << (adding ? " added" : " removed") << tagName;
        }
    } catch (const std::exception& e) {
        Warn() << "Failed to sync metadata file:" << metaPath.string() << e.what();
    }
}
