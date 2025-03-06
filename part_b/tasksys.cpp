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
    : ITaskSystem(num_threads) {}
TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning() {}
void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
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
    : ITaskSystem(num_threads), done(false), next_task_id(0) {
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
        TaskID task_id;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] { return !ready_tasks.empty() || done; });
            if (done) break;
            task_id = ready_tasks.front();
            ready_tasks.pop();
            task = tasks.front();
            tasks.pop();
        }
        task();
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = reverse_dependency_graph.find(task_id);
            if (it != reverse_dependency_graph.end()) {
                for (auto dependent : it->second) {
                    auto& deps = dependency_graph[dependent];
                    deps.erase(std::remove(deps.begin(), deps.end(), task_id), deps.end());
                    if (deps.empty()) {
                        ready_tasks.push(dependent);
                        cv.notify_all();
                    }
                }
                reverse_dependency_graph.erase(it);
            }
            if (tasks.empty() && ready_tasks.empty()) {
                cv_done.notify_all();
            }
        }
    }
}
void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {
    std::vector<TaskID> no_deps;
    TaskID task_id = runAsyncWithDeps(runnable, num_total_tasks, no_deps);
    sync();
}
TaskID TaskSystemParallelThreadPoolSleeping::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                              const std::vector<TaskID>& deps) {
    TaskID task_id = next_task_id++;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (int i = 0; i < num_total_tasks; ++i) {
            tasks.push([runnable, i, num_total_tasks] {
                runnable->runTask(i, num_total_tasks);
            });
        }
        dependency_graph[task_id] = deps;
        for (auto dep : deps) {
            reverse_dependency_graph[dep].insert(task_id);
        }
        if (deps.empty()) {
            ready_tasks.push(task_id);
        }
    }
    cv.notify_all();
    return task_id;
}
void TaskSystemParallelThreadPoolSleeping::sync() {
    std::unique_lock<std::mutex> lock(mutex);
    cv_done.wait(lock, [this] { return tasks.empty() && ready_tasks.empty(); });
}