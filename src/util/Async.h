#pragma once
// Async.h — run a job on the global thread pool, deliver the result on the
// GUI thread. QtConcurrent::run + QFutureWatcher: destroying the context
// (the watcher's parent) simply drops the delivery — no dangling callbacks.

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>

namespace Async {

template<typename T>
void run(QObject* context, std::function<T()> work,
         std::function<void(const T&)> onDone)
{
    auto* watcher = new QFutureWatcher<T>(context);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, watcher,
                     [watcher, onDone]() {
                         onDone(watcher->result());
                         watcher->deleteLater();
                     });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}

} // namespace Async
