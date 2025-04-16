#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include "logger.h"

HELP::Logger HELP::log;

namespace fs = std::filesystem;

int main()
{
    std::string path = "c:\\tmp\\";
    for(;;)
    for (const auto& entry : fs::directory_iterator(path)) {
        if (!entry.is_directory()) {
            try {
                std::ifstream ifs(entry.path().string());
                if (ifs.is_open()) {
                    //                std::cout << "File opened Ok: " << entry.path().string() << std::endl;
                    HELP::log.log_(HELP::make_time_stamp("%Y:%m:%d %H.%M.%S_") + " File opened Ok: " + entry.path().filename().string() + "\n");
                }
                else {
                    HELP::log.log_(HELP::make_time_stamp("%Y:%m:%d %H.%M.%S_") + " Access denied!: " + entry.path().filename().string() + "\n");
                }
            }
            catch (...) {
                HELP::log.log_(HELP::make_time_stamp("%Y:%m:%d %H.%M.%S_") + " Error: Access denied!: " + entry.path().filename().string() + "\n");
 //               std::cout << "Access denied! File: " << entry.path().string() << std::endl;
            }

        }
    }
    return 0;
}