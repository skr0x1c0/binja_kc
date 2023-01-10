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
//


#include <sys/stat.h>

#include <cstdlib>
#include <iostream>

#include <binaryninjaapi.h>
#include <binaryninjacore.h>
#include <lowlevelilinstruction.h>

#include <binja/kcview/lib.h>
#include <binja/utils/binary_view.h>
#include <binja/utils/settings.h>

using namespace BinaryNinja;
using namespace std;
using namespace Binja;

bool is_file(char *fname) {
    struct stat buf;
    if (stat(fname, &buf) == 0 && (buf.st_mode & S_IFREG) == S_IFREG)
        return true;

    return false;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "USAGE: " << argv[0] << " <file_name>" << endl;
        exit(-1);
    }

    char *fname = argv[1];
    if (!is_file(fname)) {
        cerr << "Error: " << fname << " is not a regular file" << endl;
        exit(-1);
    }

    SetBundledPluginDirectory(BNGetBundledPluginDirectory());
    InitPlugins(true);

    Utils::BinjaSettings::Register();
    KCView::CorePluginInit();
    LogToStdout(BNLogLevel::DebugLog);

    Json::Value opts;
    opts["loader.stripPAC"] = true;
    Ref<BinaryView> bv = Binja::Utils::OpenBinaryView(fname, false, nullptr, nullptr, opts);
    bv->UpdateAnalysisAndWait();

    cout << "Target:   " << fname << endl
         << endl;
    cout << "TYPE:     " << bv->GetTypeName() << endl;
    cout << "START:    0x" << hex << bv->GetStart() << endl;
    cout << "ENTRY:    0x" << hex << bv->GetEntryPoint() << endl;
    cout << "PLATFORM: " << bv->GetDefaultPlatform()->GetName() << endl;
    cout << endl;

    cout << "---------- 10 Functions ----------" << endl;
    int x = 0;
    for (auto func: bv->GetAnalysisFunctionList()) {
        cout << hex << func->GetStart() << " " << func->GetSymbol()->GetFullName() << endl;
        if (++x >= 10)
            break;
    }
    cout << endl;

    cout << "---------- 10 Strings ----------" << endl;
    x = 0;
    for (auto str_ref: bv->GetStrings()) {
        char *str = (char *) malloc(str_ref.length + 1);
        bv->Read(str, str_ref.start, str_ref.length);
        str[str_ref.length] = 0;

        cout << hex << str_ref.start << " (" << dec << str_ref.length << ") " << str << endl;
        free(str);

        if (++x >= 10)
            break;
    }

    // Shutting down is required to allow for clean exit of the core
    BNShutdown();

    return 0;
}
