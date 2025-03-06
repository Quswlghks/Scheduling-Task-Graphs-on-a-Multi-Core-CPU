#include "tasksys.h"

IRunnable::~IRunnable() {}
ITaskSystem::ITaskSystem(int num_threads) {}
ITaskSystem::~ITaskSystem() {}

const char* TaskSystemSerial::name() { return "Serial"; }
TaskSystemSerial::TaskSystemSerial(int num_threads) : ITaskSystem(num_threads) {}
TaskSystemSerial::~TaskSystemSerial() {}
void TaskSystemSerial::run(IRunnable* runnable, int num_total_tasks) {
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}
TaskID TaskSystemSerial::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                          const std::vector<TaskID>& deps) {
    run(runnable, num_total_tasks);
    return 0;
}
void TaskSystemSerial::sync() {}

const char* TaskSystemParallelSpawn::name() { return "Parallel + Always Spawn"; }
TaskSystemParallelSpawn::TaskSystemParallelSpawn(int num_threads) : ITaskSystem(num_threads) {}
TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}
void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {
    std::vector<std::thread> threads;
    for (int i = 0; i < num_total_tasks; ++i) {
        threads.emplace_back([runnable, i, num_total_tasks] {
            runnable->runTask(i, num_total_tasks);
        });
    }
    for (auto& t : threads) {
        t.join();
    }
}
TaskID TaskSystemParallelSpawn::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                 const std::vector<TaskID>& deps) {
    run(runnable, num_total_tasks);
    return 0;
}
void TaskSystemParallelSpawn::sync() {}

const char* TaskSystemParallelThreadPoolSpinning::name() { return "Parallel + Thread Pool + Spin"; }
TaskSystemParallelThreadPoolSpinning::TaskSystemParallelThreadPoolSpinning(int num_threads)
    : ITaskSystem(num_threads), done(false) {
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(&TaskSystemParallelThreadPoolSpinning::worker, this);
    }
}
TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning() {
    done = true;
    for (auto& t : threads) {
        t.join();
    }
}
void TaskSystemParallelThreadPoolSpinning::worker() {
    while (!done) {
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!tasks.empty()) {
                task = tasks.front();
                tasks.pop();
            }
        }
        if (task) {
            task();
            {
                std::lock_guard<std::mutex> lock(mutex);
                --remaining_tasks;
            }
        } else {
            std::this_thread::yield();
        }
    }
}
void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (int i = 0; i < num_total_tasks; ++i) {
            tasks.push([runnable, i, num_total_tasks] {
                runnable->runTask(i, num_total_tasks);
            });
        }
        remaining_tasks = num_total_tasks;
    }
    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (remaining_tasks == 0) break;
        }
        std::this_thread::yield();
    }
}
TaskID TaskSystemParallelThreadPoolSpinning::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                              const std::vector<TaskID>& deps) {
    run(runnable, num_total_tasks);
    return 0;
}
void TaskSystemParallelThreadPoolSpinning::sync() {}

const char* TaskSystemParallelThreadPoolSleeping::name() { return "Parallel + Thread Pool + Sleep"; }
TaskSystemParallelThreadPoolSleeping::TaskSystemParallelThreadPoolSleeping(int num_threads)
    : ITaskSystem(num_threads), done(false) {
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(&TaskSystemParallelThreadPoolSleeping::worker, this);
    }
}
TaskSystemParallelThreadPoolSleeping::~TaskSystemParallelThreadPoolSleeping() {
    done = true;
    cv.notify_all();
    for (auto& t : threads) {
        t.join();
    }
}
void TaskSystemParallelThreadPoolSleeping::worker() {
    while (!done) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] { return !tasks.empty() || done; });
            if (done) break;
            task = tasks.front();
            tasks.pop();
        }
        task();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (--remaining_tasks == 0) {
                cv_done.notify_all();
            }
        }
    }
}
void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (int i = 0; i < num_total_tasks; ++i) {
            tasks.push([runnable, i, num_total_tasks] {
                runnable->runTask(i, num_total_tasks);
            });
        }
        remaining_tasks = num_total_tasks;
    }
    cv.notify_all();
    std::unique_lock<std::mutex> lock(mutex);
    cv_done.wait(lock, [this] { return remaining_tasks == 0; });
}
TaskID TaskSystemParallelThreadPoolSleeping::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                              const std::vector<TaskID>& deps) {
    run(runnable, num_total_tasks);
    return 0;
}
void TaskSystemParallelThreadPoolSleeping::sync() {}