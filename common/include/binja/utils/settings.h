//
// Created by Sreejith Krishnan R on 10/01/23.
//

#pragma once

#include <binaryninjaapi.h>

namespace Binja::Utils {

class BinjaSettings {
public:
    explicit BinjaSettings(BNBinaryView* bvObj, BNSettings* settingsObj)
        : bvObj_{bvObj}, settingsObj_{settingsObj} {}

    const bool KCStripPAC() const;
    const std::vector<std::string> KCExcludedFilesets() const;
    const std::vector<std::string> KCIncludedFilesets() const;
    const bool KCSymbolicateKallocTypes() const;

    const std::optional<std::string> DebugInfoSymbolsSearchPath() const;

    const bool DWARFEnabled() const;
    const bool DWARFLoadTypes() const;
    const bool DWARFLoadDataVariables() const;
    const bool DWARFLoadFunctions() const;

    const bool MachoEnabled() const;
    const bool MachoLoadDataVariables() const;
    const bool MachoLoadFunctions() const;

    const bool SymtabEnabled() const;
    const bool SymtabLoadDataVariables() const;
    const bool SymtabLoadFunctions() const;

private:
    template <class T>
    T GetSetting(const std::string& key) const;

public:
    static void Register();

private:
    BNBinaryView* bvObj_;
    BNSettings* settingsObj_;
};

}// namespace Binja::Utils
