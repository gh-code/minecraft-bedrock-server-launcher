//
// Copyright (c) 2026 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef GH_SYSTEM_UPLOADER_HPP
#define GH_SYSTEM_UPLOADER_HPP

#include <string>
#include <functional>
#include <memory>

class progress_reporter;
class thread_safe_queue;

namespace gh {

namespace utility {
class logger;
} // namespace utility

namespace system {

class compress_uploader
{
public:
    using Callback = std::function<void(size_t processed_input_bytes,
                                        size_t total_compressed_bytes,
                                        size_t total_uploaded_bytes,
                                        double compression_percent,
                                        double upload_percent,
                                        double overall_percent,
                                        bool finished)>;

    compress_uploader();

    auto set_logger(gh::utility::logger& log) -> void;

    auto set_progress_callback(Callback callback) -> void;

    auto set_total_input_bytes(std::size_t bytes) -> void;

    auto cancel() -> void;

    auto upload(const std::string &folder_path,
                const std::string &output_path) -> bool;

private:
    std::size_t max_memory_limit_mb_; // MB
    gh::utility::logger *logger_;
    std::shared_ptr<progress_reporter> progress_;
    std::shared_ptr<thread_safe_queue> queue_;
};

} // namespace system
} // namespace gh

#endif // GH_SYSTEM_UPLOADER_HPP