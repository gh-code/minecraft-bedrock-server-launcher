//
// Copyright (c) 2026 Gary Huang (ghuang dot nctu at gmail dot com)
//

#include "backupWorker.h"

#include "gh/system/uploader.hpp"
#include "gh/utility/logger.hpp"

#include <iomanip>
#include <thread>
#include <chrono>

static std::string currentDateTime()
{
    std::time_t now = std::time(nullptr);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return std::string(buf);
}

class MyLogger : public gh::utility::logger
{
public:
    using logger::logger;

    void flush(const std::string &message) override
    {
        std::string timestamped_message = "[" + currentDateTime() + "] " + message;
        logger::flush(timestamped_message);
    }
};

void BackupWorker::doWork()
{
    const auto input_folder = "../current/";
    const auto compressed_file = "\\\\NAS\\backup\\minecraft.tar.gz";
    bool verbose = true;

    int result = backupFunction();
    if (!shouldStop) {
        for (int i = 0; i <= 100; i += 10) {
            if (shouldStop) {
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            emit progressUpdated(i);
        }
    }
    emit finished();
    return;

    shouldStop = false;
    if (!shouldStop) {
        MyLogger shared_logger(std::cerr);
        gh::system::compress_uploader uploader;
        if (verbose) {
            uploader.set_logger(shared_logger);
        }

        uploader.set_progress_callback([this,&uploader,&shared_logger](
                size_t processed_input_bytes,
                size_t total_compressed_bytes,
                size_t total_uploaded_bytes,
                double compression_percent,
                double upload_percent,
                double overall_percent,
                bool finished) {

            if (shouldStop) {
                uploader.cancel();
                return;
            }
            auto message = shared_logger.stream();
            message << std::fixed << std::setprecision(1)
                    << "Progress: input=" << processed_input_bytes
                    << " bytes (" << compression_percent << "% compressed), "
                    << "uploaded=" << total_uploaded_bytes
                    << " bytes (" << upload_percent << "% uploaded, "
                    << overall_percent << "% overall)";
            emit progressUpdated(static_cast<int>(overall_percent));
            if (finished) {
                message << " [done]";
            }
            message << std::endl;
        });

        bool result = uploader.upload(input_folder, compressed_file);
        if (!result) {
            shared_logger.stream() << "[Main] Operation failed." << std::endl;
            return;
        }
    }

    emit finished();
}

void BackupWorker::stopWork()
{
    shouldStop = true;
}