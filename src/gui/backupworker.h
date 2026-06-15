//
// Copyright (c) 2026 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef BACKUPWORKER_H
#define BACKUPWORKER_H

#include <QObject>

#include <atomic>
#include <functional>
#include <thread>

class BackupWorker : public QObject
{
    Q_OBJECT
public:
    BackupWorker(std::function<int(std::function<void(int)>, std::atomic<bool>&)> backupFunc)
    : backupFunction(std::move(backupFunc))
    , shouldStop(false)
    { }
    ~BackupWorker();

signals:
    void progressUpdated(int value);
    void finished();

public slots:
    void doWork();
    void stopWork();

private:
    std::function<int(std::function<void(int)>, std::atomic<bool>&)> backupFunction;
    std::thread thread;
    std::atomic<bool> shouldStop;
};

#endif // BACKUPWORKER_H