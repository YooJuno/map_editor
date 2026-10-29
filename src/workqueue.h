#pragma once
#include <QObject>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>

// One reusable worker. Callers keep their transactional control flow while the
// GUI event loop continues painting and handling view navigation. Editing and
// reentrant file operations are disabled for the complete run() interval.
class WorkQueue : public QObject {
    Q_OBJECT
public:
    explicit WorkQueue(QObject *parent = nullptr);
    bool isRunning() const { return running_; }
    QString operation() const { return operation_; }
    template<class Function> auto run(const QString &operation, Function function) -> std::invoke_result_t<Function> {
        using Result = std::invoke_result_t<Function>;
        static_assert(!std::is_void_v<Result>);
        if (QThread::currentThread() != thread()) throw std::logic_error("Document operations must start on the GUI thread");
        if (running_) throw std::logic_error("A document operation is already running");
        std::optional<Result> result;
        std::exception_ptr error;
        QEventLoop loop;
        QFutureWatcher<void> watcher;
        connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
        running_ = true; operation_ = operation; emit busyChanged(true);
        try {
            watcher.setFuture(QtConcurrent::run(&pool_, [function = std::move(function), &result, &error]() mutable {
                try { result.emplace(function()); } catch (...) { error = std::current_exception(); }
            }));
            if (!watcher.isFinished()) loop.exec();
            // Also join when application shutdown exits a nested event loop.
            watcher.waitForFinished();
        } catch (...) {
            running_ = false; emit busyChanged(false); throw;
        }
        running_ = false; emit busyChanged(false);
        if (error) std::rethrow_exception(error);
        if (!result) throw std::runtime_error("The document operation did not complete");
        return std::move(*result);
    }
signals:
    void busyChanged(bool busy);
private:
    QThreadPool pool_;
    QString operation_;
    bool running_ = false;
};
