// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <unordered_map>
#include "json.hpp"

struct NidFuncTable {
    std::string m_encoded_id;
    std::string m_hex_id;
    std::string m_funcName;
    int m_libversion;
    int m_version_major;
    int m_version_minor;
};

constexpr std::string_view SpdxHeader =
    R"(// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
)";

constexpr int MAXIMUM_LINE_LENGTH = 100;

void GenerateCodeFiles(
    const std::unordered_map<std::string, std::vector<NidFuncTable>>& libName2FuncTableMap,
    const std::string& moduleName) {
    // Generate Header
    std::string spdx{SpdxHeader};
    std::string headerCode(spdx);
    headerCode += "\n";
    headerCode += "#pragma once\n\n#include \"common/types.h\"\n\n";
    headerCode += "namespace Core::Loader {\nclass SymbolsResolver;\n}\n\n";
    std::string trimmedName = moduleName;
    if (moduleName.find("libSce") != std::string::npos) {
        trimmedName = moduleName.substr(6, moduleName.size() - 1);
    } else if (moduleName.find("lib") != std::string::npos) {
        trimmedName = moduleName.substr(3, moduleName.size() - 1);
        trimmedName[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(trimmedName[0])));
    }
    std::string lowModName = trimmedName;
    std::transform(lowModName.begin(), lowModName.end(), lowModName.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string headerName(lowModName + ".h");
    headerCode += "namespace Libraries::" + trimmedName + " {\n\n";
    std::unordered_set<std::string> funcDeclares;
    for (const auto& lib : libName2FuncTableMap) {
        for (const auto& func : lib.second) {
            if (funcDeclares.find(func.m_funcName) == funcDeclares.end()) {
                std::string funcDeclare("s32 PS4_SYSV_ABI " + func.m_funcName + "();\n");
                if (funcDeclare.length() > MAXIMUM_LINE_LENGTH) {
                    funcDeclare = "s32 PS4_SYSV_ABI\n" + func.m_funcName + "();\n";
                }
                headerCode += funcDeclare;
                funcDeclares.insert(func.m_funcName);
            }
        }
    }

    headerCode += "\nvoid Register" + moduleName + "(Core::Loader::SymbolsResolver* sym);\n";

    headerCode += "} // namespace Libraries::" + trimmedName;
    std::ofstream headerFile(MODULE_DIR + headerName);
    headerFile << headerCode;
    headerFile.close();

    std::string sourceName(lowModName + ".cpp");
    std::string sourceCode(spdx);
    sourceCode += "\n";
    sourceCode += "#include \"common/logging/log.h\"\n";
    sourceCode += "#include \"core/libraries/error_codes.h\"\n";
    sourceCode += "#include \"core/libraries/libs.h\"\n";
    sourceCode += "#include \"core/libraries/" + lowModName + "/" + headerName + "\"\n\n";

    sourceCode += "namespace Libraries::" + trimmedName + " {\n\n";

    // function implementation
    std::unordered_set<std::string> funcImplementation;
    for (const auto& lib : libName2FuncTableMap) {
        for (const auto& func : lib.second) {
            if (funcImplementation.find(func.m_funcName) == funcImplementation.end()) {
                std::string funcHeader = "s32 PS4_SYSV_ABI " + func.m_funcName + "() {";
                if (funcHeader.length() > MAXIMUM_LINE_LENGTH) {
                    funcHeader = "s32 PS4_SYSV_ABI\n" + func.m_funcName + "() {";
                }
                const std::string funcDeclare(funcHeader + "\n" +
                                              "    LOG_ERROR(Lib_" + trimmedName +", \"(STUBBED) called\");\n"
                                              "    return ORBIS_OK;\n" +
                                              "}\n\n");
                sourceCode += funcDeclare;
                funcImplementation.insert(func.m_funcName);
            }
        }
    }
    sourceCode += "void Register" + moduleName + "(Core::Loader::SymbolsResolver* sym) {\n";
    for (const auto& lib : libName2FuncTableMap) {
        for (const auto& func : lib.second) {
            std::string nextLine = "    LIB_FUNCTION(\"" + func.m_encoded_id + "\", \"" + lib.first + "\", " +
                          std::to_string(func.m_libversion) + ", \"" + moduleName + "\", " +
                          std::to_string(func.m_version_major) + ", " +
                          std::to_string(func.m_version_minor) + ", " + func.m_funcName + ");\n";
            if (nextLine.length() > MAXIMUM_LINE_LENGTH) {
                nextLine = "    LIB_FUNCTION(\"" + func.m_encoded_id + "\", \"" + lib.first + "\", " +
                          std::to_string(func.m_libversion) + ", \"" + moduleName + "\", " +
                          std::to_string(func.m_version_major) + ", " +
                          std::to_string(func.m_version_minor) + ",\n" 
                          + "                 " + func.m_funcName + ");\n";
            }
            sourceCode += nextLine;
        }
    }

    sourceCode += "};\n\n";
    sourceCode += "} // namespace Libraries::" + trimmedName;
    std::ofstream sourceFile(MODULE_DIR + sourceName);
    sourceFile << sourceCode;
    sourceFile.close();
}

void GetSymbolsFromLibDoc(std::vector<std::string>& importModules) {
    const std::filesystem::path libdocDir(LIBDOC_DIR);
    if (!std::filesystem::exists(libdocDir)) {
        std::cerr << "Module database directory not found at: " << libdocDir << "\n"
                  << "Please ensure the ps4libdoc submodule is initialized." << std::endl;
        return;
    }

    for (const auto& importModule : importModules) {
        const auto moduleFilename = importModule + ".sprx.json";
        const std::filesystem::path modulePath = libdocDir / moduleFilename;
        if (!std::filesystem::exists(modulePath)) {
            std::cerr << "Module description missing: " << moduleFilename
                      << " (did you update the ps4libdoc submodule?)" << std::endl;
            continue;
        }

        std::ifstream file(modulePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open module description: " << modulePath << std::endl;
            continue;
        }

        nlohmann::json m_json_data;
        try {
            m_json_data = nlohmann::json::parse(file);
        } catch (const nlohmann::json::parse_error& err) {
            std::cerr << "Failed to parse " << modulePath << ": " << err.what() << std::endl;
            continue;
        }

        if (!m_json_data.contains("modules") || !m_json_data["modules"].is_array()) {
            std::cerr << "Invalid module description (missing 'modules' array): " << modulePath
                      << std::endl;
            continue;
        }

        bool bFound = false;
        for (const auto& modules : m_json_data["modules"]) {
            if (!modules.contains("name") || !modules["name"].is_string()) {
                std::cerr << "Skipping malformed module entry in " << modulePath << std::endl;
                continue;
            }

            const std::string subModuleName = modules["name"].get<std::string>();
            if (subModuleName != importModule) {
                continue;
            }

            if (!modules.contains("version_major") || !modules.contains("version_minor") ||
                !modules.contains("libraries")) {
                std::cerr << "Incomplete module information for " << subModuleName << " in "
                          << modulePath << std::endl;
                continue;
            }

            const int m_version_major = modules["version_major"].get<int>();
            const int m_version_minor = modules["version_minor"].get<int>();

            if (!modules["libraries"].is_array()) {
                std::cerr << "Expected 'libraries' array for " << subModuleName << " in "
                          << modulePath << std::endl;
                continue;
            }

            std::unordered_map<std::string, std::vector<NidFuncTable>> libName2FuncTableMap;
            for (const auto& libraries : modules["libraries"]) {
                if (!libraries.contains("name") || !libraries.contains("version") ||
                    !libraries.contains("symbols")) {
                    std::cerr << "Skipping malformed library entry in " << modulePath
                              << std::endl;
                    continue;
                }

                const std::string libName = libraries["name"].get<std::string>();
                const int libVersion = libraries["version"].get<int>();
                const auto& symbolsArray = libraries["symbols"];
                if (!symbolsArray.is_array()) {
                    std::cerr << "Expected 'symbols' array for library " << libName
                              << " in " << modulePath << std::endl;
                    continue;
                }

                auto& functionTable = libName2FuncTableMap[libName];
                for (const auto& symbols : symbolsArray) {
                    if (!symbols.contains("encoded_id") || !symbols.contains("hex_id")) {
                        std::cerr << "Skipping malformed symbol entry in " << modulePath
                                  << std::endl;
                        continue;
                    }

                    const std::string encoded_id = symbols["encoded_id"].get<std::string>();
                    const std::string hex_id = symbols["hex_id"].get<std::string>();

                    std::string symName;
                    if (symbols.contains("name") && !symbols["name"].is_null()) {
                        symName = symbols["name"].get<std::string>();
                    } else {
                        symName = "Func_" + hex_id;
                    }

                    functionTable.push_back(
                        NidFuncTable{encoded_id, hex_id, symName, libVersion, m_version_major,
                                     m_version_minor});
                }
            }

            GenerateCodeFiles(libName2FuncTableMap, subModuleName);
            bFound = true;
        }

        if (!bFound) {
            std::cerr << "Module entry not found inside " << moduleFilename << std::endl;
        }
    }
}
int main(int argc, char* argv[]) {
    std::filesystem::path genFolder(MODULE_DIR);
    if (!std::filesystem::exists(genFolder)) {
        std::filesystem::create_directories(genFolder);
    }

    std::vector<std::string> modules_to_generate;
    modules_to_generate.push_back(std::string("libSceGnmDriver"));
    modules_to_generate.push_back(std::string("libScePad"));
    modules_to_generate.push_back(std::string("libSceVideoOut"));
    modules_to_generate.push_back(std::string("libkernel"));
    modules_to_generate.push_back(std::string("libSceSystemService"));
    modules_to_generate.push_back(std::string("libSceUserService"));
    modules_to_generate.push_back(std::string("libSceCommonDialog"));
    modules_to_generate.push_back(std::string("libSceMsgDialog"));
    modules_to_generate.push_back(std::string("libSceAudioOut"));
    modules_to_generate.push_back(std::string("libSceAudioIn"));
    modules_to_generate.push_back(std::string("libSceNet"));
    modules_to_generate.push_back(std::string("libSceNetCtl"));
    modules_to_generate.push_back(std::string("libSceSsl"));
    modules_to_generate.push_back(std::string("libSceHttp"));
    modules_to_generate.push_back(std::string("libSceSaveData"));
    modules_to_generate.push_back(std::string("libSceSysmodule"));
    modules_to_generate.push_back(std::string("libSceSaveDataDialog"));
    modules_to_generate.push_back(std::string("libSceNpManager"));
    modules_to_generate.push_back(std::string("libSceNpTrophy"));
    modules_to_generate.push_back(std::string("libSceScreenShot"));
    modules_to_generate.push_back(std::string("libSceLibcInternal"));
    modules_to_generate.push_back(std::string("libSceRtc"));
    modules_to_generate.push_back(std::string("libSceGameLiveStreaming"));
    modules_to_generate.push_back(std::string("libSceSharePlay"));
    modules_to_generate.push_back(std::string("libSceRemoteplay"));
    modules_to_generate.push_back(std::string("libSceIme"));
    modules_to_generate.push_back(std::string("libSceVideodec"));
    GetSymbolsFromLibDoc(modules_to_generate);

    return 0;
}
