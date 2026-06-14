//
// Copyright (c) 2026 Gary Huang (ghuang dot nctu at gmail dot com)
//

#include "backupWorker.h"

#include <thread>
#include <chrono>

void BackupWorker::doWork()
{
    shouldStop = false;
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
}

void BackupWorker::stopWork()
{
    shouldStop = true;
}