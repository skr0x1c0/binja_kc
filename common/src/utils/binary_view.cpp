// Copyright (c) skr0x1c0 2022.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "utils/binary_view.h"

using namespace Binja;
using namespace Utils;
using namespace BinaryNinja;

Ref<BinaryView> Utils::OpenBinaryView(const std::string &filename, bool updateAnalysis, Ref<BinaryViewType> viewType,
                                      std::function<bool(size_t, size_t)> progress, Json::Value options) {
    if (!progress)
        progress = [](size_t, size_t) { return true; };

    // Loading will surely fail if the file does not exist, so exit early
    if (!BNPathExists(filename.c_str()))
        return nullptr;

    // Detect bndb
    bool isDatabase = false;
    Ref<BinaryView> view = nullptr;

    if (filename.size() > 6 && filename.substr(filename.size() - 5) == ".bndb") {
        // Open database, read raw view contents from it
        static const std::string sqlite_header = "SQLite format 3";

        FILE *f = fopen(filename.c_str(), "rb");
        // Unable to open file
        if (f == nullptr)
            return nullptr;

        char header[0x20];
        fread(header, 1, sqlite_header.size(), f);
        fclose(f);
        header[sqlite_header.size()] = 0;

        // File is not a valid sqlite db
        if (sqlite_header != header)
            return nullptr;

        Ref<FileMetadata> file = new FileMetadata(filename);
        view = file->OpenDatabaseForConfiguration(filename);
        isDatabase = true;
    } else {
        // Open file, read raw contents
        Ref<FileMetadata> file = new FileMetadata(filename);
        view = new BinaryData(file, filename);
    }

    if (!view)
        return nullptr;
    return Utils::OpenBinaryView(view, updateAnalysis, viewType, progress, options, isDatabase);
}

BinaryNinja::Ref<BinaryNinja::BinaryView> Utils::OpenBinaryView(
    BinaryNinja::Ref<BinaryNinja::BinaryView> view, bool updateAnalysis, Ref<BinaryViewType> requestedViewType,
    std::function<bool(size_t, size_t)> progress, Json::Value options, bool isDatabase) {
    Ref<BinaryViewType> bvt = requestedViewType;
    Ref<BinaryViewType> universalBvt;
    std::vector<Ref<BinaryViewType>> availableViewTypes = BinaryViewType::GetViewTypesForData(view);
    for (auto it = availableViewTypes.rbegin(), end = availableViewTypes.rend(); it != end; ++it) {
        Ref<BinaryViewType> available = *it;
        if (available->GetName() == "Universal") {
            universalBvt = available;
            continue;
        }
        if (!bvt && available->GetName() != "Raw") {
            bvt = available;
        }
    }

    // No available views: Load as Mapped
    if (!bvt)
        bvt = BinaryViewType::GetByName("Mapped");

    Ref<Settings> defaultSettings = Settings::Instance(bvt->GetName() + "_settings");
    defaultSettings->DeserializeSchema(Settings::Instance()->SerializeSchema());
    defaultSettings->SetResourceId(bvt->GetName());

    Ref<Settings> loadSettings;
    if (isDatabase) {
        loadSettings = view->GetLoadSettings(bvt->GetName());
    }
    if (!loadSettings) {
        if (universalBvt && options.isMember("files.universal.architecturePreference")) {
            // Load universal architecture
            loadSettings = universalBvt->GetLoadSettingsForData(view);
            if (!loadSettings) {
                LogError("Could not load entry from Universal image. No load settings!");
                return nullptr;
            }
            std::string architectures = loadSettings->Get<std::string>("loader.universal.architectures");

            std::unique_ptr<Json::CharReader> reader(Json::CharReaderBuilder().newCharReader());
            Json::Value archList;
            std::string errors;
            if (!reader->parse((const char *) architectures.data(), (const char *) architectures.data() + architectures.size(), &archList, &errors)) {
                BinaryNinja::LogError("Error parsing architecture list: %s", errors.data());
                return nullptr;
            }

            Json::Value archEntry;
            for (auto archPref: options["files.universal.architecturePreference"]) {
                for (auto entry: archList) {
                    if (entry["architecture"].asString() == archPref.asString()) {
                        archEntry = entry;
                        break;
                    }
                }
                if (!archEntry.isNull())
                    break;
            }
            if (archEntry.isNull()) {
                std::string error = "Could not load any of:";
                for (auto archPref: options["files.universal.architecturePreference"]) {
                    error += std::string(" ") + archPref.asString();
                }
                error += " from Universal image. Entry not found! Available entries:";
                for (auto entry: archList) {
                    error += std::string(" ") + entry["architecture"].asString();
                }
                LogError("%s", error.c_str());
                return nullptr;
            }

            loadSettings = Settings::Instance(GetUniqueIdentifierString());
            loadSettings->DeserializeSchema(archEntry["loadSchema"].asString());
        } else {
            // Load non-universal architecture
            loadSettings = bvt->GetLoadSettingsForData(view);
        }
    }

    if (!loadSettings) {
        LogError("Could not get load settings for binary view of type '%s'", bvt->GetName().c_str());
        return nullptr;
    }

    loadSettings->SetResourceId(bvt->GetName());
    view->SetLoadSettings(bvt->GetName(), loadSettings);

    for (auto key: options.getMemberNames()) {
        auto value = options[key];
        if (loadSettings->Contains(key)) {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            std::string json = Json::writeString(builder, value);

            if (!loadSettings->SetJson(key, json, view)) {
                LogError("Setting: %s set operation failed!", key.c_str());
                return nullptr;
            }
        } else if (defaultSettings->Contains(key)) {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            std::string json = Json::writeString(builder, value);

            if (!defaultSettings->SetJson(key, json, view)) {
                LogError("Setting: %s set operation failed!", key.c_str());
                return nullptr;
            }
        } else {
            LogError("Setting: %s not available!", key.c_str());
            return nullptr;
        }
    }

    Ref<BinaryView> bv;
    if (isDatabase) {
        view = view->GetFile()->OpenExistingDatabase(view->GetFile()->GetFilename(), progress);
        if (!view) {
            LogError("Unable to open existing database with filename %s", view->GetFile()->GetFilename().c_str());
            return nullptr;
        }
        bv = view->GetFile()->GetViewOfType(bvt->GetName());
    } else {
        bv = bvt->Create(view);
    }

    if (!bv) {
        return view;
    }
    if (updateAnalysis) {
        bv->UpdateAnalysisAndWait();
    }
    return bv;
}