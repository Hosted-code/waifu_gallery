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

#include "worker.h"
#include "service/database.h"
#include <cstring>
#include <QImageReader>

DatabaseWorker::DatabaseWorker(QObject* parent) : QObject(parent), database{DbMode::Query} { // search worker
}
DatabaseWorker::~DatabaseWorker() {}

void DatabaseWorker::searchPics(const SearchContext& searchCtx, size_t requestId) {
    if (!DbCache::getInstance().tagMappingLoaded()) {
        database.refreshTagMapping();
    }

    const std::unordered_set<uint32_t>& includedTags = searchCtx.includedTags;
    const std::unordered_set<uint32_t>& excludedTags = searchCtx.excludedTags;
    PlatformType platform = searchCtx.searchPlatform;
    SearchField searchField = searchCtx.searchField;
    const std::string& searchText = searchCtx.searchText;
    const FilterContext& filterCtx = searchCtx.filterCtx;

    bool tagSearchApplied = !includedTags.empty() || !excludedTags.empty();
    bool textSearchApplied = !searchText.empty() && searchField != SearchField::None;

    DisplayItemType displayType;
    if (textSearchApplied && !tagSearchApplied) {
        displayType = DisplayItemType::Metadata;
    } else {
        displayType = DisplayItemType::Pic;
    }

    bool noFiltersApplied = includedTags.empty() && excludedTags.empty() && !textSearchApplied;

    bool needTagSearch = noFiltersApplied || (includedTags != lastIncludedTags || excludedTags != lastExcludedTags);
    if (needTagSearch) {
        lastIncludedTags = includedTags;
        lastExcludedTags = excludedTags;
        lastTagSearchResult = database.tagSearch(includedTags, excludedTags);
    }
    if (platform != lastPlatformType || searchField != lastSearchField || searchText != lastSearchText) {
        lastPlatformType = platform;
        lastSearchField = searchField;
        lastSearchText = searchText;
        lastTextSearchResult = database.textSearch(searchText, platform, searchField);
    }

    DisplayItems* displayItems = new DisplayItems();
    if (displayType == DisplayItemType::Metadata) {
        std::vector<PlatformID> intersectedResult;
        for (const auto& id : lastTextSearchResult) {
            intersectedResult.push_back(id);
        }

        displayItems->type = DisplayItemType::Metadata;
        displayItems->metadataItems.reserve(intersectedResult.size());
        displayItems->picItems.reserve(intersectedResult.size());
        size_t metadataIdx = 0;
        size_t picIdx = 0;
        for (const auto& platformID : intersectedResult) {
            Metadata metadata = database.getMetadata(platformID);
            if (!isMatchFilter(metadata, filterCtx)) continue;
            auto picIds = database.getMetadataPicIds(platformID);
            displayItems->metadataItems.emplace_back(MetadataItem{std::move(metadata), picIdx, picIds.size()});
            for (const auto& picId : picIds) {
                displayItems->picItems.emplace_back(PicItem{database.getPicInfo(picId), metadataIdx, 1});
                picIdx++;
            }
            metadataIdx++;
        }
    } else if (displayType == DisplayItemType::Pic) {
        std::vector<uint64_t> intersectedResult;
        std::unordered_set<uint64_t> textSearchIntersectedResult;

        if (textSearchApplied) {
            std::unordered_set<uint64_t> textSearchResultPics;
            for (const auto& platformID : lastTextSearchResult) {
                auto picIds = database.getMetadataPicIds(platformID);
                textSearchResultPics.insert(picIds.begin(), picIds.end());
            }
            const auto& small =
                lastTagSearchResult.size() < textSearchResultPics.size() ? lastTagSearchResult : textSearchResultPics;
            const auto& large =
                lastTagSearchResult.size() < textSearchResultPics.size() ? textSearchResultPics : lastTagSearchResult;
            for (const auto& id : small) {
                if (large.find(id) != large.end()) {
                    textSearchIntersectedResult.insert(id);
                }
            }
        }

        if (textSearchApplied) {
            for (const auto& id : textSearchIntersectedResult) {
                intersectedResult.push_back(id);
            }
        } else {
            for (const auto& id : lastTagSearchResult) {
                intersectedResult.push_back(id);
            }
        }
        displayItems->type = DisplayItemType::Pic;
        displayItems->picItems.reserve(intersectedResult.size());
        displayItems->metadataItems.reserve(intersectedResult.size());
        size_t picIdx = 0;
        size_t metadataIdx = 0;
        for (const auto& id : intersectedResult) {
            PicInfo picInfo = database.getPicInfo(id);
            if (!isMatchFilter(picInfo, filterCtx)) continue;
            displayItems->picItems.emplace_back(PicItem{picInfo, metadataIdx, picInfo.sourceIdentifiers.size()});
            for (const auto& identifier : picInfo.sourceIdentifiers) {
                displayItems->metadataItems.emplace_back(MetadataItem{database.getMetadata(identifier), picIdx, 1});
                metadataIdx++;
            }
            picIdx++;
        }
    }

    // gather available tags from resultItems
    std::unordered_map<uint32_t, int> tagCount;
    std::unordered_set<uint64_t> countedPics;
    for (const auto& item : displayItems->picItems) {
        const auto& pic = item.info;
        if (countedPics.find(pic.id) != countedPics.end()) continue;
        countedPics.insert(pic.id);
        for (const auto& picTag : pic.tags) {
            tagCount[picTag.tagId]++;
        }
    }

    // prepare available tags
    std::vector<TagCount> availableTags;
    for (const auto& [tagId, count] : tagCount) {
        if (includedTags.find(tagId) != includedTags.end() || excludedTags.find(tagId) != excludedTags.end()) {
            continue;
        }
        TagCount tagCountEntry;
        tagCountEntry.tag = database.getStringTag(tagId);
        tagCountEntry.tagId = tagId;
        tagCountEntry.count = count;
        availableTags.push_back(tagCountEntry);
    }
    std::sort(availableTags.begin(), availableTags.end(), [](const TagCount& a, const TagCount& b) { return b.count < a.count; });

    emit searchComplete(displayItems, availableTags, requestId);
}
