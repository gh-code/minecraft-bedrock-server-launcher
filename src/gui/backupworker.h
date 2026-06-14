//
// Copyright (c) 2026 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef BACKUPWORKER_H
#define BACKUPWORKER_H

#include <QObject>

#include <atomic>
#include <functional>

class BackupWorker : public QObject
{
    Q_OBJECT
public:
    BackupWorker(std::function<int()> backupFunc)
    : backupFunction(backupFunc)
    , shouldStop(false)
    { }

signals:
    void progressUpdated(int value);
    void finished();

public slots:
    void doWork();
    void stopWork();

private:
    std::function<int()> backupFunction;
    std::atomic<bool> shouldStop;
};

#endif // BACKUPWORKER_H