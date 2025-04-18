#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

#include <windows.h>
#include <ktmw32.h>


#include "logger.h"

HELP::Logger HELP::log;

namespace fs = std::filesystem;

static int printHelp()
{
    std::cout << "test.exe -h                                     - print this text\n";
    std::cout << "test.exe <folder name>                          - access files in the directory in an endless loop\n";
    std::cout << "test.exe <file name> DeleteFileA                - delete the file with DeleteFileA()\n";
    std::cout << "test.exe <file name> DELETE_ON_CLOSE            - delete the file with DELETE_ON_CLOSE\n";
    std::cout << "test.exe <file name> NtSetInformationFile       - delete the file with NtSetInformationFile()\n";
    std::cout << "test.exe <file name> DeleteFileTransactedA      - delete the file with DeleteFileTransactedA()\n";

    return 1;
}

static std::string UTF16ToUTF8(std::wstring_view input) {
    const auto size = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        return {};
    }

    std::string output(size, '\0');

    if (size != WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
        &output[0], static_cast<int>(output.size()), nullptr,
        nullptr)) {
        output.clear();
    }

    return output;
}


static int cycle(const std::string& path)
{
    for (;;) {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (!entry.is_directory()) {
                auto pathu8 = UTF16ToUTF8(entry.path().native());
                try {
                    std::ifstream ifs(entry.path());
                    if (ifs.is_open()) {
                        HELP::log.log_(HELP::make_time_stamp("%Y:%m:%d %H.%M.%S_") + " File opened Ok: " + pathu8 + "\n");
                    }
                    else {
                        HELP::log.log_(HELP::make_time_stamp("%Y:%m:%d %H.%M.%S_") + " Access denied!: " + pathu8 + "\n");
                    }
                }
                catch (std::exception& e) {
                    HELP::log.log_(HELP::make_time_stamp("%Y:%m:%d %H.%M.%S_") + " Error opening " + pathu8 + ": " +  e.what() + "\n");
                }
            }
        }
    }

    return 0;
}

static int deleteWithDeleteFileA(const std::string& path)
{
    std::cout << "Deleting with DeleteFileA()\n";

    if (!::DeleteFileA(path.c_str())) {
        auto e = ::GetLastError();
        std::cerr << "Failed to delete " << path << ": " << e << "\n";
        return -1;
    }

    return 0;
}

static int deleteWithDELETE_ON_CLOSE(const std::string& path)
{
    std::cout << "Deleting with DELETE_ON_CLOSE\n";

    auto h = ::CreateFileA(path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (h == INVALID_HANDLE_VALUE) {
        auto e = ::GetLastError();
        std::cerr << "Failed to open " << path << ": " << e << "\n";
        return -1;
    }

    ::CloseHandle(h);

    return 0;
}

static int deleteWithNtSetInformationFile(const std::string& path)
{
    std::cout << "Deleting with NtSetInformationFile()\n";

    auto h = ::CreateFileA(path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) {
        auto e = ::GetLastError();
        std::cerr << "Failed to open " << path << ": " << e << "\n";
        return -1;
    }

    FILE_DISPOSITION_INFO di = {};
    di.DeleteFile = TRUE;

    int res = 0;

    if (!::SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof(di))) {
        auto e = ::GetLastError();
        std::cerr << "SetFileInformationByHandle on " << path << " failed: " << e << "\n";
        
        res = -1;
    }

    ::CloseHandle(h);
    return res;
}

static int deleteWithDeleteFileTransactedA(const std::string& path)
{
    std::cout << "Deleting with DeleteFileTransactedA()\n";

    auto hTrans = ::CreateTransaction(
        nullptr,
        nullptr,
        TRANSACTION_DO_NOT_PROMOTE,
        0,
        0,
        INFINITE,
        nullptr
    );

    if (hTrans == INVALID_HANDLE_VALUE) {
        auto e = ::GetLastError();
        std::cerr << "CreateTransaction failed: " << e << "\n";
        return -1;
    }

    int res = 0;
    if (!::DeleteFileTransactedA(path.c_str(), hTrans)) {
        auto e = ::GetLastError();
        std::cerr << "DeleteFileTransactedA failed: " << e << "\n";
        
        res = -1;
    }

    if (!::CommitTransaction(hTrans)) {
        auto e = ::GetLastError();
        std::cerr << "CommitTransaction failed: " << e << "\n";

        res = -1;
    }

    ::CloseHandle(hTrans);
    return res;
}


int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2)
        return printHelp();

    if (!std::strcmp(argv[1], "-h")) {
        return printHelp();
    }

    std::string path = argv[1];
    
    if (argc < 3) {
        std::error_code ec;
        if (!fs::is_directory(path, ec)) {
            std::cerr << "Path is not a directory or is inaccessible: " << path << std::endl;
            return printHelp();
        }
        if (ec) {
            std::cerr << "Path is not a directory or is inaccessible: " << path << ": " << ec.message() << std::endl;
            return printHelp();
        }

        return cycle(path);
    }

    if (!std::strcmp(argv[2], "DeleteFileA")) {
        return deleteWithDeleteFileA(path);
    }
    else if (!std::strcmp(argv[2], "DELETE_ON_CLOSE")) {
        return deleteWithDELETE_ON_CLOSE(path);
    }
    else if (!std::strcmp(argv[2], "NtSetInformationFile")) {
        deleteWithNtSetInformationFile(path);
    }
    else if (!std::strcmp(argv[2], "DeleteFileTransactedA")) {
        deleteWithDeleteFileTransactedA(path);
    }
    else {
        std::cerr << "Unknown argument: " << argv[2] << std::endl;
        return printHelp();
    }
    
    return 0;
}