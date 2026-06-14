//
// Copyright (c) 2026 Gary Huang (ghuang dot nctu at gmail dot com)
//
#include "gh/system/uploader.hpp"
#include "gh/utility/logger.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <fstream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>
#include <array>
#include <stdexcept>
#include <cstring>
#include <ctime>
#include <zlib.h>

namespace fs = boost::filesystem;

struct data_block
{
    std::vector<char> data;
    bool is_last;
};

class progress_reporter
{
public:
    using Callback = gh::system::compress_uploader::Callback;

    explicit progress_reporter(Callback callback = nullptr)
        : callback_(std::move(callback))
    { }

    auto set_callback(Callback callback) -> void
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = std::move(callback);
    }

    auto set_total_input_bytes(size_t bytes) -> void
    {
        std::lock_guard<std::mutex> lock(mutex_);
        total_input_bytes_ = bytes;
    }

    auto add_processed_input(size_t bytes) -> void
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            processed_input_bytes_ += bytes;
        }
        dispatch(false);
    }

    auto add_compressed_output(size_t bytes) -> void
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            total_compressed_bytes_ += bytes;
        }
        dispatch(false);
    }

    auto add_uploaded(size_t bytes) -> void
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            total_uploaded_bytes_ += bytes;
        }
        dispatch(false);
    }

    auto finish() -> void
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
        }
        dispatch(true);
    }

private:
    auto dispatch(bool finished) -> void
    {
        Callback callback;
        size_t processed_input_bytes = 0;
        size_t total_compressed_bytes = 0;
        size_t total_uploaded_bytes = 0;
        size_t total_input_bytes = 0;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callback_;
            processed_input_bytes = processed_input_bytes_;
            total_compressed_bytes = total_compressed_bytes_;
            total_uploaded_bytes = total_uploaded_bytes_;
            total_input_bytes = total_input_bytes_;
        }

        if (!callback) {
            return;
        }

        double compression_percent = 0.0;
        if (total_input_bytes > 0) {
            compression_percent = (static_cast<double>(processed_input_bytes) / total_input_bytes) * 100.0;
        }
        if (compression_percent > 100.0) {
            compression_percent = 100.0;
        }

        double upload_stage_percent = 0.0;
        if (total_compressed_bytes > 0)
        {
            upload_stage_percent = (static_cast<double>(total_uploaded_bytes) / total_compressed_bytes) * 100.0;
            if (upload_stage_percent > 100.0) {
                upload_stage_percent = 100.0;
            }
        }

        double overall_percent = compression_percent * 0.8 + upload_stage_percent * 0.2;
        if (total_compressed_bytes == 0) {
            overall_percent = compression_percent * 0.8;
        }

        if (overall_percent > 100.0) {
            overall_percent = 100.0;
        }

        if (finished) {
            overall_percent = 100.0;
        }

        if (overall_percent < last_overall_percent_) {
            overall_percent = last_overall_percent_;
        }

        last_overall_percent_ = overall_percent;

        callback(processed_input_bytes,
                 total_compressed_bytes,
                 total_uploaded_bytes,
                 compression_percent,
                 upload_stage_percent,
                 overall_percent, finished);
    }

    size_t total_input_bytes_ = 0;
    size_t processed_input_bytes_ = 0;
    size_t total_compressed_bytes_ = 0;
    size_t total_uploaded_bytes_ = 0;
    bool finished_ = false;
    double last_overall_percent_ = 0.0;
    Callback callback_;
    std::mutex mutex_;
};

// Thread-safe queue with a capacity limit
class thread_safe_queue : public gh::utility::loggable
{
public:
    thread_safe_queue(size_t max_memory_mb)
    {
        max_memory_bytes_ = max_memory_mb * 1024 * 1024; // convert to Bytes
    }

    // Called by the compression thread: push data. If memory is full, wait.
    // Returns bool indicating whether push succeeded (false if queue is
    // finished_ or error_).
    bool push(std::vector<char> &&block_data, bool is_last)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        size_t block_size = block_data.size();

        // If the memory limit is exceeded, the compression thread waits here (blocks)
        cv_produce_.wait(lock, [this, block_size]() {
            return finished_ || error_ || cancelled_.load() ||
                    (current_memory_bytes_ + block_size <= max_memory_bytes_) ||
                    (queue_.empty() && block_size > max_memory_bytes_);
        });

        if (finished_ || error_ || cancelled_) {
            return false;
        }

        if (block_size > max_memory_bytes_ && queue_.empty()) {
            log() << "[Queue] warning: pushing single block larger than max_memory_bytes_="
                  << max_memory_bytes_ << " bytes=" << block_size << std::endl;
        }

        current_memory_bytes_ += block_size;
        queue_.push(data_block{std::move(block_data), is_last});

        log() << "[Queue] pushed " << block_size << " bytes, current_memory_bytes="
              << current_memory_bytes_ << std::endl;

        cv_consume_.notify_one(); // Notify upload thread that data is available
        return true;
    }

    // Called by upload thread: take data. If the queue is empty, wait
    bool pop(data_block &value)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_consume_.wait(lock, [this]() {
            return !queue_.empty() || finished_ || error_ || cancelled_.load();
        });

        if (queue_.empty() && (finished_ || error_ || cancelled_)) {
            return false;
        }

        if (queue_.empty()) {
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();

        current_memory_bytes_ -= value.data.size();
        log() << "[Queue] popped " << value.data.size() << " bytes, current_memory_bytes="
              << current_memory_bytes_ << std::endl;

        cv_produce_.notify_one(); // Notify compression thread that free memory
                                  // is available and it can continue compressing

        return true;
    }

    void set_finished()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        cv_produce_.notify_all();
        cv_consume_.notify_all();
    }

    void cancel()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_ = true;
        finished_ = true;
        cv_produce_.notify_all();
        cv_consume_.notify_all();
    }

    void set_error(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = true;
        error_message_ = message;
        finished_ = true;
        cv_produce_.notify_all();
        cv_consume_.notify_all();
    }

    bool has_error() const
    {
        return error_;
    }

    bool is_cancelled() const
    {
        return cancelled_.load();
    }

    std::string error_message() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_message_;
    }

private:
    std::queue<data_block> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_produce_;
    std::condition_variable cv_consume_;
    size_t max_memory_bytes_;
    size_t current_memory_bytes_ = 0;
    bool finished_ = false;
    bool error_ = false;
    std::atomic<bool> cancelled_{false};
    std::string error_message_;
};

void compression_worker(std::string folderPath, thread_safe_queue &queue,
    progress_reporter *progress, std::promise<bool> result_promise)
{
    bool success = false;
    z_stream zStream;
    memset(&zStream, 0, sizeof(zStream));
    if (deflateInit2(&zStream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
            Z_DEFAULT_STRATEGY) != Z_OK) {
        queue.set_error("deflateInit2 failed");
        queue.set_finished();
        result_promise.set_value(false);
        return;
    }
    const size_t chunk_size = 64 * 1024; // Buffer size for each file read (64KB)
    std::vector<char> in_buf(chunk_size);
    std::vector<char> out_buf(chunk_size);

    auto fail_and_return = [&](bool set_finished = true) -> void {
        if (set_finished) {
            deflateEnd(&zStream);
            queue.set_finished();
        }
        result_promise.set_value(false);
    };

    auto deflateFeed = [&](const char *data, size_t len) -> bool {

        zStream.avail_in = static_cast<uInt>(len);
        zStream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data));

        while (zStream.avail_in > 0) {

            if (queue.is_cancelled()) {
                return false;
            }

            zStream.avail_out = static_cast<uInt>(chunk_size);
            zStream.next_out = reinterpret_cast<Bytef *>(out_buf.data());

            int ret = deflate(&zStream, Z_NO_FLUSH);
            if (ret != Z_OK) {
                queue.log() << "[Compression] deflate failed with code " << ret << std::endl;
                throw std::runtime_error("deflate failed");
            }

            size_t compressed_size = chunk_size - zStream.avail_out;
            if (compressed_size > 0) {
                std::vector<char> block(out_buf.begin(), out_buf.begin() + compressed_size);
                if (!queue.push(std::move(block), false)) {
                    if (queue.is_cancelled())
                        return false;
                    throw std::runtime_error("queue closed during push");
                }
                if (progress) {
                    progress->add_compressed_output(compressed_size);
                }
            }
        }
        return true;
    };

    auto makeTarHeader = [&](const std::string &name, uint64_t size)
    {
        std::array<char, 512> header{};
        std::string fname = name;
        std::string prefix;
        if (fname.size() > 100) {
            auto slash_pos = fname.rfind('/', 155);
            if (slash_pos != std::string::npos &&
                    fname.size() - slash_pos - 1 <= 100 &&
                    slash_pos <= 155) {
                prefix = fname.substr(0, slash_pos);
                fname = fname.substr(slash_pos + 1);
            } else {
                fname = fname.substr(fname.size() - 100);
            }
        }

        if (fname.size() > 100) {
            fname = fname.substr(0, 100);
        }
        memcpy(header.data() + 0, fname.c_str(), fname.size());
        if (!prefix.empty()) {
            memcpy(header.data() + 345, prefix.c_str(),
                    std::min(prefix.size(), static_cast<size_t>(155)));
        }

        // mode (8), uid (8), gid (8)
        snprintf(header.data() + 100, 8, "%07o", 0644);
        snprintf(header.data() + 108, 8, "%07o", 0);
        snprintf(header.data() + 116, 8, "%07o", 0);

        // size (12)
        snprintf(header.data() + 124, 12, "%011llo", static_cast<unsigned long long>(size));

        // mtime (12)
        snprintf(header.data() + 136, 12, "%011llo", static_cast<unsigned long long>(time(nullptr)));

        // checksum: fill with spaces for calculation
        for (int i = 0; i < 8; ++i) {
            header[148 + i] = ' ';
        }

        // typeflag
        header[156] = '0';

        // magic and version
        memcpy(header.data() + 257, "ustar", 5);
        header[262] = '\0';
        header[263] = '0';
        header[264] = '0';

        // uname/gname left empty

        // compute checksum
        unsigned int sum = 0;
        for (size_t i = 0; i < header.size(); ++i) {
            sum += static_cast<unsigned char>(header[i]);
        }

        char chk[8] = {};
        snprintf(chk, sizeof(chk), "%06o", sum);
        chk[6] = '\0';
        chk[7] = ' ';
        memcpy(header.data() + 148, chk, 8);

        return header;
    };

    try {
        for (const auto &entry : fs::recursive_directory_iterator(folderPath)) {

            if (!entry.is_regular_file()) {
                continue;
            }

            std::string relpath;
            try {
                relpath = fs::relative(entry.path(), folderPath).string();
            } catch (...) {
                relpath = entry.path().filename().string();
            }

            // open file
            fs::ifstream file(entry.path(), std::ios::binary);
            if (!file.is_open()) {
                continue;
            }

            // get file size
            uint64_t fsize = static_cast<uint64_t>(fs::file_size(entry.path()));

            // write tar header
            auto header = makeTarHeader(relpath, fsize);
            if (!deflateFeed(header.data(), header.size())) {
                fail_and_return();
                return;
            }

            // write file content
            while (!file.eof()) {
                file.read(in_buf.data(), chunk_size);
                std::streamsize readn = file.gcount();
                if (readn > 0) {
                    if (queue.is_cancelled()) {
                        fail_and_return();
                        return;
                    }
                    if (progress) {
                        progress->add_processed_input(static_cast<size_t>(readn));
                    }
                    if (!deflateFeed(in_buf.data(), static_cast<size_t>(readn))) {
                        fail_and_return();
                        return;
                    }
                }
            }

            // pad file content to 512-byte boundary
            size_t pad = (512 - (fsize % 512)) % 512;
            if (pad > 0) {
                std::vector<char> zeros(pad, 0);
                deflateFeed(zeros.data(), zeros.size());
            }

            file.close();
        }

        // two 512-byte zero blocks to mark end of archive
        std::array<char, 1024> zeros{};
        if (!deflateFeed(zeros.data(), zeros.size())) {
            fail_and_return();
            return;
        }

        // end (flush zlib)
        int ret = Z_OK;
        while (ret != Z_STREAM_END) {

            zStream.avail_out = static_cast<uInt>(chunk_size);
            zStream.next_out = reinterpret_cast<Bytef *>(out_buf.data());
            ret = deflate(&zStream, Z_FINISH);

            if (ret != Z_OK && ret != Z_STREAM_END) {
                queue.log() << "[Compression] deflate finish failed with code " << ret << std::endl;
                break;
            }

            size_t compressed_size = chunk_size - zStream.avail_out;
            if (compressed_size > 0) {
                std::vector<char> block(out_buf.begin(), out_buf.begin() + compressed_size);
                if (!queue.push(std::move(block), ret == Z_STREAM_END)) {
                    throw std::runtime_error("queue closed during final push");
                }
                if (progress) {
                    progress->add_compressed_output(compressed_size);
                }
            }
        }
    } catch (const std::exception &ex) {
        queue.log() << "[Compression] Error: " << ex.what() << std::endl;
        queue.set_error(ex.what());
    } catch (...) {
        queue.log() << "[Compression] Unknown error occurred during compression." << std::endl;
        queue.set_error("unknown compression error");
    }

    deflateEnd(&zStream);
    queue.set_finished();
    if (!queue.has_error()) {
        success = true;
    }
    result_promise.set_value(success);
}

static size_t computeTotalInputBytes(const std::string &folderPath)
{
    size_t total_bytes = 0;
    try {
        for (const auto &entry : fs::recursive_directory_iterator(folderPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            total_bytes += static_cast<size_t>(fs::file_size(entry.path()));
        }
    } catch (const std::exception &) {
        // Ignore a single file access failure and continue counting other files
    }
    return total_bytes;
}

struct upload_context
{
    thread_safe_queue &queue;
    data_block current_block;
    size_t block_offset = 0;
    size_t total_uploaded_bytes = 0; // Track total bytes successfully sent globally
};

// curl upload callback
size_t upload_callback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    upload_context *ctx = static_cast<upload_context *>(userdata);
    size_t max_write = size * nitems;

    // 1. If the current data_block is finished, fetch the next one from the queue.
    if (ctx->block_offset >= ctx->current_block.data.size()) {
        if (ctx->current_block.is_last) {
            return 0; // All finished
        }
        if (!ctx->queue.pop(ctx->current_block)) {
            return 0; // Queue finished
        }
        ctx->block_offset = 0;
    }

    // 2. Fill curl's network send buffer
    size_t available = ctx->current_block.data.size() - ctx->block_offset;
    size_t to_write = std::min(max_write, available);

    std::memcpy(buffer, ctx->current_block.data.data() + ctx->block_offset, to_write);
    ctx->block_offset += to_write;
    ctx->total_uploaded_bytes += to_write; // Accumulate total uploaded bytes

    return to_write;
}

class stream_uploader : public gh::utility::loggable
{
public:
    stream_uploader() = default;
    virtual ~stream_uploader() = default;
    virtual auto connect(size_t resume_offset) -> bool = 0;
    virtual auto send_block(const char *data, size_t size, size_t &bytes_sent) -> bool = 0;
    virtual auto disconnect() -> void = 0;
};

// Main function responsible for upload retries and supporting partial writes
void upload_worker(thread_safe_queue &queue, std::shared_ptr<stream_uploader> uploader,
    progress_reporter *progress, std::promise<bool> result_promise)
{
    bool success = false;
    data_block current_block{};
    size_t block_offset = 0;
    size_t total_uploaded_bytes = 0;
    bool upload_complete = false;

    while (!upload_complete) {

        if (queue.has_error()) {
            auto message = queue.error_message();
            uploader->log() << "[Network] Upstream compression failed: " << message << std::endl;
            break;
        }

        if (queue.is_cancelled()) {
            uploader->log() << "[Network] Upload cancelled." << std::endl;
            break;
        }

        if (!uploader->connect(total_uploaded_bytes)) {
            uploader->log() << "[Network] Fail to connect, retrying in 5 seconds...";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        bool connection_alive = true;
        while (connection_alive) {

            if (current_block.data.empty()) {
                if (current_block.is_last) {
                    upload_complete = true;
                    break;
                }
                if (!queue.pop(current_block)) {
                    if (queue.has_error()) {
                        uploader->log() << "[Network] Upstream queue closed due to error." << std::endl;
                        break;
                    }
                    if (queue.is_cancelled()) {
                        uploader->log() << "[Network] Upstream queue closed due to cancellation." << std::endl;
                        break;
                    }
                    upload_complete = true;
                    break;
                }
                block_offset = 0;
            }

            size_t bytes_remaining = current_block.data.size() - block_offset;
            if (bytes_remaining == 0) {
                if (current_block.is_last) {
                    upload_complete = true;
                    break;
                }
                current_block.data.clear();
                continue;
            }

            size_t bytes_sent = 0;
            if (uploader->send_block(current_block.data.data() + block_offset, bytes_remaining, bytes_sent)) {

                if (bytes_sent == 0) {
                    uploader->log() << "[Network] send_block returned zero bytes, treating as failure\n";
                    uploader->disconnect();
                    connection_alive = false;
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }

                block_offset += bytes_sent;
                total_uploaded_bytes += bytes_sent;
                if (progress) {
                    progress->add_uploaded(bytes_sent);
                }

                if (block_offset >= current_block.data.size()) {
                    current_block.data.clear();
                    block_offset = 0;
                }
            } else {
                uploader->log() << "[Network] Upload interrupted! Triggering disconnection mechanism...\n";
                uploader->disconnect();
                connection_alive = false;
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }
    uploader->disconnect();
    if (progress && !queue.is_cancelled()) {
        progress->finish();
    }
    if (!queue.has_error()) {
        success = upload_complete && !queue.is_cancelled();
    }
    uploader->log() << "[Network] Stream upload completed!\n";
    result_promise.set_value(success);
}

class local_file_uploader : public stream_uploader
{
private:
    std::string file_path_;
    std::ofstream out_file_;

public:
    local_file_uploader(const std::string &destination_path)
        : file_path_(destination_path)
    { }

    ~local_file_uploader()
    {
        disconnect();
    }

    bool connect(size_t resume_offset) override
    {
        try {
            fs::path p(file_path_);
            fs::path parent = p.parent_path();
            if (!parent.empty() && !fs::exists(parent)) {
                fs::create_directories(parent);
            }

            // If resume_offset is 0, perform a fresh write (truncate old file)
            if (resume_offset == 0) {
                out_file_.open(file_path_, std::ios::binary | std::ios::out | std::ios::trunc);
            } else {
                // If the file doesn't exist, create an empty file first so seek can be used later
                if (!fs::exists(p)) {
                    std::ofstream tmp(file_path_, std::ios::binary);
                    tmp.close();
                }

                out_file_.open(file_path_, std::ios::binary | std::ios::in | std::ios::out);
                if (out_file_.is_open()) {
                    out_file_.seekp(static_cast<std::streamoff>(resume_offset));
                    if (!out_file_) {
                        log() << "[Uploader] seekp failed for " << file_path_
                              << " offset=" << resume_offset << std::endl;
                        out_file_.close();
                    }
                }
            }

            if (!out_file_.is_open()) {
                log() << "[Uploader] Failed to open output file: " << file_path_ << " (resume_offset="
                      << resume_offset << ")" << std::endl;
                return false;
            } else {
                log() << "[Uploader] Opened output file: " << file_path_ << " (resume_offset="
                      << resume_offset << ")" << std::endl;
                return true;
            }
        } catch (const std::exception &ex) {
            log() << "[Uploader] Exception during connect: " << ex.what() << std::endl;
            return false;
        }
    }

    auto send_block(const char *data, size_t size, size_t &bytes_sent) -> bool override
    {
        if (!out_file_.is_open()) {
            return false;
        }

        if (size == 0) {
            bytes_sent = 0;
            return true;
        }

        out_file_.write(data, size);

        // Ensure data is actually written to disk (prevent cache failure or
        // power loss), and verify write status
        out_file_.flush();
        if (out_file_.fail()) {
            log() << "[Uploader] write failed for " << size << " bytes" << std::endl;
            return false;
        }

        bytes_sent = size;
        log() << "[Uploader] wrote " << bytes_sent << " bytes to " << file_path_ << std::endl;
        return true;
    }

    auto disconnect() -> void override
    {
        if (out_file_.is_open()) {
            out_file_.close();
        }
    }
};

using namespace gh::system;

compress_uploader::compress_uploader()
    : max_memory_limit_mb_(500) // MB
    , logger_(&gh::utility::loggable::empty_logger)
    , progress_(std::make_shared<progress_reporter>())
{ }

auto compress_uploader::set_logger(gh::utility::logger &logger) -> void
{
    logger_ = &logger;
}

auto compress_uploader::set_progress_callback(Callback callback) -> void
{
    if (!progress_) {
        progress_ = std::make_shared<progress_reporter>(std::move(callback));
    } else {
        progress_->set_callback(std::move(callback));
    }
}

auto compress_uploader::set_total_input_bytes(size_t bytes) -> void
{
    if (progress_) {
        progress_->set_total_input_bytes(bytes);
    }
}

auto compress_uploader::cancel() -> void
{
    if (queue_) {
        queue_->cancel();
    }
}

auto compress_uploader::upload(const std::string &folder_path,
                                const std::string &output_path) -> bool
{
    queue_ = std::make_shared<thread_safe_queue>(max_memory_limit_mb_);
    queue_->set_logger(*logger_);

    auto method = std::make_shared<local_file_uploader>(output_path);
    method->set_logger(*logger_);

    size_t total_input_bytes = computeTotalInputBytes(folder_path);
    set_total_input_bytes(total_input_bytes);

    std::promise<bool> compression_result;
    std::promise<bool> upload_result;
    auto compression_future = compression_result.get_future();
    auto upload_future = upload_result.get_future();

    std::thread compressor(compression_worker,
                           folder_path,
                           std::ref(*queue_),
                           progress_.get(),
                           std::move(compression_result));
    std::thread uploader(upload_worker,
                         std::ref(*queue_),
                         method,
                         progress_.get(),
                         std::move(upload_result));

    compressor.join();
    uploader.join();

    bool compression_ok = compression_future.get();
    bool upload_ok = upload_future.get();

    if (!compression_ok) {
        logger_->stream() << "[compress_uploader] Compression failed." << std::endl;
    }
    if (!upload_ok) {
        logger_->stream() << "[compress_uploader] Upload failed." << std::endl;
        if (queue_->has_error()) {
            logger_->stream() << "[compress_uploader] Cause: " << queue_->error_message() << std::endl;
        }
    }

    return compression_ok && upload_ok;
}