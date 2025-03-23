//
// Created by Sreejith Krishnan R on 10/01/23.
//

#include <binaryninjaapi.h>

#include "binja/utils/settings.h"

using namespace Binja;
using namespace Utils;

namespace BN = BinaryNinja;

using SettingsRef = BN::Ref<BN::Settings>;

#define MAIN_SETTINGS_GROUP "binjaKC"

#define KC_SETTINGS_GROUP MAIN_SETTINGS_GROUP ".kernelcache"
#define KC_SETTING_EXCLUDED_FILESETS KC_SETTINGS_GROUP ".excludedFilesets"
#define KC_SETTING_INCLUDED_FILESETS KC_SETTINGS_GROUP ".includedFilesets"
#define KC_SETTING_APPLY_DYLD_CHAINED_FIXUPS KC_SETTINGS_GROUP ".applyDyldChainedFixups"
#define KC_SETTING_STRIP_PAC KC_SETTINGS_GROUP ".stripPAC"
#define KC_SETTING_SYMBOLICATE_KALLOC_TYPES KC_SETTINGS_GROUP ".symbolicateKallocTypes"

#define DEBUGINFO_SETTINGS_GROUP MAIN_SETTINGS_GROUP ".debugInfo"
#define DEBUGINFO_SETTING_SYMBOLS_DIRECTORY DEBUGINFO_SETTINGS_GROUP ".symbolsDirectory"

#define DWARF_SETTINGS_GROUP MAIN_SETTINGS_GROUP ".dwarf"
#define DWARF_SETTINGS_ENABLE_DWARF DWARF_SETTINGS_GROUP ".enableDWARF"
#define DWARF_SETTINGS_LOAD_TYPES DWARF_SETTINGS_GROUP ".loadTypes"
#define DWARF_SETTINGS_LOAD_DATA_VARIABLES DWARF_SETTINGS_GROUP ".loadDataVariables"
#define DWARF_SETTINGS_LOAD_FUNCTIONS DWARF_SETTINGS_GROUP ".loadFunctions"

#define MACHO_SETTINGS_GROUP MAIN_SETTINGS_GROUP ".machoDebugInfo"
#define MACHO_SETTINGS_ENABLE_MACHO MACHO_SETTINGS_GROUP ".enableMacho"
#define MACHO_SETTINGS_LOAD_DATA_VARIABLES MACHO_SETTINGS_GROUP ".loadDataVariables"
#define MACHO_SETTINGS_LOAD_FUNCTIONS MACHO_SETTINGS_GROUP ".loadFunctions"

#define SYMTAB_SETTINGS_GROUP MAIN_SETTINGS_GROUP ".symtab"
#define SYMTAB_SETTINGS_ENABLE_SYMTAB SYMTAB_SETTINGS_GROUP ".enableSymtab"
#define SYMTAB_SETTINGS_LOAD_DATA_VARIABLES SYMTAB_SETTINGS_GROUP ".loadDataVariables"
#define SYMTAB_SETTINGS_LOAD_FUNCTIONS SYMTAB_SETTINGS_GROUP ".loadFunctions"

#define FUNCTION_STARTS_SETTINGS_GROUP MAIN_SETTINGS_GROUP ".functionStarts"
#define FUNCTION_STARTS_SETTINGS_ENABLE_FUNCTION_STARTS FUNCTION_STARTS_SETTINGS_GROUP ".enableFunctionStarts"

namespace {

void RegisterKCSettings(SettingsRef settings) {
    settings->RegisterSetting(
        KC_SETTING_EXCLUDED_FILESETS,
        R"({
            "default": ["com.apple.driver.FairPlayIOKit"],
            "description": "List of filesets in kernel cache to ignore",
            "elementType": "string",
            "ignore": [],
            "title": "Excluded filesets",
            "type": "array"
        })");

    settings->RegisterSetting(
        KC_SETTING_INCLUDED_FILESETS,
        R"({
            "default": [],
            "description": "List of filesets in kernel cache to include. If empty, all filesets are included except the ones in 'Excluded filesets'",
            "elementType": "string",
            "ignore": [],
            "title": "Included filesets",
            "type": "array"
        })");
    // TODO: fix description
    settings->RegisterSetting(
        KC_SETTING_APPLY_DYLD_CHAINED_FIXUPS,
        R"({
            "default": true,
            "description": "Apply dyld chained fixups",
            "title": "Apply dyld chained fixups",
            "type": "boolean"
        })");
    settings->RegisterSetting(
        KC_SETTING_STRIP_PAC,
        R"({
            "default": false,
            "description": "Strip PAC from PAC signed pointers",
            "title": "Strip PAC",
            "type": "boolean"
        })");
    settings->RegisterSetting(
        KC_SETTING_SYMBOLICATE_KALLOC_TYPES,
        R"({
            "default": true,
            "description": "Symbolicate __kalloc_type and __kalloc_var sections",
            "title": "Symbolicate kalloc types",
            "type": "boolean"
        })");
}

void RegisterDebugInfoSettings(SettingsRef settings) {
    settings->RegisterSetting(
        DEBUGINFO_SETTING_SYMBOLS_DIRECTORY,
        R""({
            "default": "",
            "description": "Absolute path to directory containing symbol sources (dSYM and Mach-O)",
            "title": "Symbols directory",
            "type": "string",
            "optional": true
        })"");
}

void RegisterDWARFSettings(SettingsRef settings) {
    settings->RegisterSetting(
        DWARF_SETTINGS_ENABLE_DWARF,
        R"({
            "default": true,
            "description": "Load debug info from .dSYM files",
            "title": "Enable DWARF debug info",
            "type": "boolean"
        })");

    settings->RegisterSetting(
        DWARF_SETTINGS_LOAD_TYPES,
        R"({
            "default": true,
            "description":"Load type information from DWARF",
            "title":"Load types",
            "type":"boolean"
        })");

    settings->RegisterSetting(
        DWARF_SETTINGS_LOAD_DATA_VARIABLES,
        R"({
            "default": true,
            "description":"Load global data variable debug info from DWARF",
            "title":"Load data variable info",
            "type":"boolean"
        })");

    settings->RegisterSetting(
        DWARF_SETTINGS_LOAD_FUNCTIONS,
        R"({
            "default": true,
            "description":"Load function debug info from DWARF",
            "title":"Load function info",
            "type":"boolean"
        })");
}

void RegisterMachoSettings(SettingsRef settings) {
    settings->RegisterSetting(
        MACHO_SETTINGS_ENABLE_MACHO,
        R""({
            "default": false,
            "description": "Load debug info from Mach-O files (eg: *.kext inside KDK)",
            "title": "Enable Mach-O debug info",
            "type": "boolean"
        })"");

    settings->RegisterSetting(
        MACHO_SETTINGS_LOAD_DATA_VARIABLES,
        R"({
            "default": true,
            "description":"Load global data variable debug info from Mach-O",
            "title":"Load data variable info",
            "type":"boolean"
        })");

    settings->RegisterSetting(
        MACHO_SETTINGS_LOAD_FUNCTIONS,
        R"({
            "default": true,
            "description":"Load function debug info from Mach-O",
            "title":"Load function info",
            "type":"boolean"
        })");
}

void RegisterSymtabSettings(SettingsRef settings) {
    settings->RegisterSetting(
        SYMTAB_SETTINGS_ENABLE_SYMTAB,
        R"({
            "default": true,
            "description": "Load debug info from kernelcache SYMTAB",
            "title": "Enable symbol table debug info",
            "type": "boolean"
        })");

    settings->RegisterSetting(
        SYMTAB_SETTINGS_LOAD_DATA_VARIABLES,
        R"({
            "default": true,
            "description":"Load global data variable debug info from symbol table",
            "title":"Load data variable info",
            "type":"boolean"
        })");

    settings->RegisterSetting(
        SYMTAB_SETTINGS_LOAD_FUNCTIONS,
        R"({
            "default": true,
            "description":"Load function debug info from symbol table",
            "title":"Load function info",
            "type":"boolean"
        })");
}

void RegisterFunctionStartsSettings(SettingsRef settings) {
    settings->RegisterSetting(
        FUNCTION_STARTS_SETTINGS_ENABLE_FUNCTION_STARTS,
        R"""({
            "default": false,
            "description": "Load function starts using LC_FUNCTION_STARTS load command",
            "title": "Enable LC_FUNCTION_STARTS debug info",
            "type": "boolean"
        })""");
}

}// namespace


void BinjaSettings::Register() {
    auto settings = BN::Settings::Instance();
    settings->RegisterGroup(MAIN_SETTINGS_GROUP, "Binja KC");
    RegisterKCSettings(settings);
    RegisterDebugInfoSettings(settings);
    RegisterDWARFSettings(settings);
    RegisterMachoSettings(settings);
    RegisterSymtabSettings(settings);
    RegisterFunctionStartsSettings(settings);
}


template<>
bool BinjaSettings::GetSetting(const std::string &key) const {
    const SettingsRef settings = BinaryNinja::Settings::Instance();
    return BNSettingsGetBool(
        settingsObj_,
        key.c_str(),
        bvObj_,
        nullptr,
        nullptr);
}

template<>
std::string BinjaSettings::GetSetting(const std::string &key) const {
    const SettingsRef settings = BinaryNinja::Settings::Instance();
    return BNSettingsGetString(
        settingsObj_,
        key.c_str(),
        bvObj_,
        nullptr,
        nullptr);
}

template<>
std::vector<std::string> BinjaSettings::GetSetting(const std::string &key) const {
    size_t size = 0;
    char **outBuffer = (char **) BNSettingsGetStringList(
        settingsObj_, key.c_str(), bvObj_, nullptr, nullptr, &size);

    std::vector<std::string> result;
    result.reserve(size);
    for (size_t i = 0; i < size; i++)
        result.emplace_back(outBuffer[i]);

    BNFreeStringList(outBuffer, size);
    return result;
}

const bool BinjaSettings::KCApplyDyldChainedFixups() const {
    return GetSetting<bool>(KC_SETTING_APPLY_DYLD_CHAINED_FIXUPS);
}

const bool BinjaSettings::KCStripPAC() const {
    return GetSetting<bool>(KC_SETTING_STRIP_PAC);
}

const std::vector<std::string> BinjaSettings::KCExcludedFilesets() const {
    return GetSetting<std::vector<std::string>>(KC_SETTING_EXCLUDED_FILESETS);
}

const std::vector<std::string> BinjaSettings::KCIncludedFilesets() const {
    return GetSetting<std::vector<std::string>>(KC_SETTING_INCLUDED_FILESETS);
}

const bool BinjaSettings::KCSymbolicateKallocTypes() const {
    return GetSetting<bool>(KC_SETTING_SYMBOLICATE_KALLOC_TYPES);
}

const std::optional<std::string> BinjaSettings::DebugInfoSymbolsSearchPath() const {
    std::string result = GetSetting<std::string>(DEBUGINFO_SETTING_SYMBOLS_DIRECTORY);
    if (!result.empty()) {
        return result;
    }
    return std::nullopt;
}

const bool BinjaSettings::DWARFEnabled() const {
    return GetSetting<bool>(DWARF_SETTINGS_ENABLE_DWARF);
}

const bool BinjaSettings::DWARFLoadTypes() const {
    return GetSetting<bool>(DWARF_SETTINGS_LOAD_TYPES);
}

const bool BinjaSettings::DWARFLoadDataVariables() const {
    return GetSetting<bool>(DWARF_SETTINGS_LOAD_DATA_VARIABLES);
}

const bool BinjaSettings::DWARFLoadFunctions() const {
    return GetSetting<bool>(DWARF_SETTINGS_LOAD_FUNCTIONS);
}

const bool BinjaSettings::MachoEnabled() const {
    return GetSetting<bool>(MACHO_SETTINGS_ENABLE_MACHO);
}

const bool BinjaSettings::MachoLoadDataVariables() const {
    return GetSetting<bool>(MACHO_SETTINGS_LOAD_DATA_VARIABLES);
}

const bool BinjaSettings::MachoLoadFunctions() const {
    return GetSetting<bool>(MACHO_SETTINGS_LOAD_FUNCTIONS);
}

const bool BinjaSettings::SymtabEnabled() const {
    return GetSetting<bool>(SYMTAB_SETTINGS_ENABLE_SYMTAB);
}

const bool BinjaSettings::SymtabLoadDataVariables() const {
    return GetSetting<bool>(SYMTAB_SETTINGS_LOAD_DATA_VARIABLES);
}

const bool BinjaSettings::SymtabLoadFunctions() const {
    return GetSetting<bool>(SYMTAB_SETTINGS_LOAD_FUNCTIONS);
}

const bool BinjaSettings::FunctionStartsEnabled() const {
    return GetSetting<bool>(FUNCTION_STARTS_SETTINGS_ENABLE_FUNCTION_STARTS);
}
