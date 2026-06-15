//
// Copyright (c) 2026 Gary Huang (ghuang dot nctu at gmail dot com)
//

#include "backupWorker.h"

#include <thread>
#include <chrono>

BackupWorker::~BackupWorker()
{
    shouldStop.store(true);
    if (thread.joinable()) {
        thread.join();
    }
}

void BackupWorker::doWork()
{
    if (!backupFunction) {
        emit finished();
    }

    shouldStop = false;

    thread = std::thread([this]() {
        backupFunction([this](int value) {
            emit progressUpdated(value);
        }, shouldStop);

        emit finished();
    });
}

void BackupWorker::stopWork()
{
    shouldStop.store(true);
}