/*
 * ================================================================
 *   Copyright (C) 2023 XUranus All rights reserved.
 *
 *   File:         vcheck.cpp
 *   Author:       XUranus
 *   Date:         2023-07-01
 *   Description:  a command line tool to diff volume data checksum 
 * ==================================================================
 */

#include "GetOption.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <ios>
#include <iostream>
#include <cstdint>
#include <ostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <openssl/evp.h>

using namespace xuranus::getopt;

namespace {
#ifdef _WIN32
    const std::string SEPARATOR = "\\";
#else
    const std::string SEPARATOR = "/";
#endif
}

static const char* g_helpMessage =
    "vchecksum [options...]    util for dump volume data checksum\n"
    "[ -v | --volume= ]     volume path\n"
    "[ -b | --blocksize=]   block size to calculate checksum\n"
    "[ -o | --output=]      output directory\n"
    "[ -d | --sha256dump ]  dump sha256 checksum to human readable text\n"
    "[ -h | --help ]        show help\n";

int PrintHelp()
{
    ::printf("%s\n", g_helpMessage);
    return 0;
}

void ComputeSHA256(uint8_t* data, uint32_t len, uint8_t* output, uint32_t outputLen)
{
    EVP_MD_CTX *mdctx = nullptr;
    const EVP_MD *md = nullptr;
    unsigned char mdValue[EVP_MAX_MD_SIZE] = { 0 };
    unsigned int mdLen;

    if ((md = EVP_get_digestbyname("SHA256")) == nullptr) {
        std::cerr << "Unknown message digest SHA256" << std::endl;
        return;
    }

    if ((mdctx = EVP_MD_CTX_new()) == nullptr) {
        std::cout << "Memory allocation failed" << std::endl;
        return;
    }

    EVP_DigestInit_ex(mdctx, md, nullptr);
    EVP_DigestUpdate(mdctx, data, len);
    EVP_DigestFinal_ex(mdctx, mdValue, &mdLen);
    assert(mdLen == outputLen);
    memcpy(output, mdValue, mdLen);
    EVP_MD_CTX_free(mdctx);
    return;
}

int ExecDumpVolumeSha256(
    const std::string& volumePath,
    const std::string& blockSizeString,
    const std::string& outputDir)
{
    std::string outputFile = outputDir + SEPARATOR + "sha256.checksum.txt";
    std::ifstream volumeIn(volumePath, std::ios::binary);
    if (!volumeIn.is_open()) {
        std::cerr << "failed to open volume for read: " << volumePath << std::endl;
        return 1;
    }
    std::ofstream fileOut(outputFile, std::ios::trunc);
    if (!fileOut.is_open()) {
        std::cerr << "failed to open checksum file for write: " << outputFile << std::endl;
        return 1;
    }
    uint32_t blockSize = 1024 * 1024 * 4;
    uint8_t* dataBuffer = new uint8_t[blockSize];
    uint8_t checksumBuffer[32] = { 0 };
    memset(dataBuffer, 0, blockSize);
    memset(checksumBuffer, 0, 32);
    std::cout << "== DUMP SHA256 CHECKSUM ===" << std::endl;
    std::cout << "VolumePath: " << volumePath << std::endl;
    std::cout << "OutPutFile: " << outputFile << std::endl;
    std::cout << "BlockSize:  " << blockSize << std::endl;
    while (volumeIn.read(reinterpret_cast<char*>(dataBuffer), blockSize)) {
        ComputeSHA256(dataBuffer, blockSize, checksumBuffer, 32);
        for (int i = 0; i < 32; ++i) {
            fileOut << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(checksumBuffer[i]);
        }
        fileOut << std::endl;
        memset(dataBuffer, 0, blockSize);
        memset(checksumBuffer, 0, 32);
    }
    volumeIn.close();
    fileOut.close();
    delete[] dataBuffer;
    return 0;
}

int main(int argc, char** argv)
{
    std::string outputDir;
    std::string volumePath;
    std::string blockSize = "4MB";
    bool sha256dump = false;

    GetOptionResult result = GetOption(
        const_cast<const char**>(argv) + 1,
        argc - 1,
        "v:b:o:dh",
        { "volume=", "blocksize=", "output=", "sha256dump", "help"});

    for (const OptionResult opt: result.opts) {
        std::cout << opt.option << " " << opt.value << std::endl;
        if (opt.option == "o" || opt.option == "output=") {
            outputDir = opt.value;
        } else if (opt.option == "v" || opt.option == "volume=") {
            volumePath = opt.value;
        } if (opt.option == "b" || opt.option == "blocksize=") {
            blockSize = opt.value;
        } else if (opt.option == "h" || opt.option == "help") {
            return PrintHelp();
        } else if (opt.option == "d" || opt.option == "sha256dump") {
            sha256dump = true;   
        }
    }
    if (sha256dump) {
        return ExecDumpVolumeSha256(volumePath, blockSize, outputDir);
    }
    PrintHelp();
    return 0;
}