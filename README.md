**핵심 요약:**  
- 작업 실행 라이브러리를 구현하여 멀티코어 CPU에서 효율적으로 작업을 실행하도록 했습니다.  
- Part A는 동기적인 대량 작업 실행을, Part B는 작업 간 의존성을 고려한 비동기 실행을 지원합니다.  

**코드 개요:**  
코드는 `part_a/tasksys.cpp`, `part_a/tasksys.h`, `part_b/tasksys.cpp`, `part_b/tasksys.h`로 구성됩니다.  
- **Part A:** 직렬 실행, 스레드 생성 방식, 스레드 풀(스핀/슬립) 방식으로 작업을 실행합니다.  
- **Part B:** 스레드 풀(슬립) 방식에 작업 의존성 처리를 추가하여 비동기 실행을 지원합니다.  
- 의존성 처리를 위해 그룹별 작업 큐와 의존성 그래프를 관리하며, 올바른 실행 순서를 보장합니다.  

**보고서 개요:**  
- **예상치 못한 점:** 작업 의존성 처리를 위해 그룹별 작업 관리가 필요하며, 초기 구현에서 오류가 있을 수 있음을 발견했습니다. 이를 수정하여 정확히 동작하도록 했습니다.  

**구현 원리 설명:**  
- **Part A:** 직렬 실행은 단일 스레드로 작업을 순차 처리하며, 병렬 실행은 스레드 생성이나 풀을 통해 CPU 코어를 활용합니다. 스레드 풀은 스레드 재사용으로 효율적입니다.  
- **Part B:** 작업 그룹에 의존성을 설정하고, 의존성이 만족될 때만 작업을 실행 가능 상태로 만듭니다. 예를 들어, 작업 B가 작업 A에 의존하면 A가 완료된 후에만 B가 실행됩니다.  

**성능 분석:**  
- 가벼운 작업에서는 직렬 실행이 빠를 수 있으며, 무거운 작업에서는 병렬 실행이 유리합니다. 스레드 풀은 스레드 생성 오버헤드를 줄여 효율적입니다.  

**테스트 사례:**  
"simple_dependency_test"는 작업 A, B, C의 의존성(A→B→C)을 확인하며, 순서대로 실행되고 sync()로 완료를 기다리는지 검증합니다.  

---

### 보고서 

#### Part A: 동기적인 대량 작업 실행  

##### 1. 구현된 클래스들  
Part A에서는 네 가지 클래스를 구현했습니다:  
- **TaskSystemSerial:** 모든 작업을 단일 스레드에서 순차적으로 실행합니다.  
- **TaskSystemParallelSpawn:** 각 작업마다 새로운 스레드를 생성하여 병렬 실행합니다.  
- **TaskSystemParallelThreadPoolSpinning:** 스레드 풀을 사용하며, 유휴 상태일 때 스핀(CPU 사용)으로 대기합니다.  
- **TaskSystemParallelThreadPoolSleeping:** 스레드 풀을 사용하며, 유휴 상태일 때 슬립(CPU 절약)으로 대기합니다.  

각 클래스는 `run(IRunnable* runnable, int num_total_tasks)` 메서드를 통해 작업을 실행하며, `runAsyncWithDeps`는 Part A에서는 의존성을 고려하지 않고 단순히 `run`을 호출합니다.  

##### 2. 각 구현의 원리  
- **TaskSystemSerial:**  
  `for` 루프를 사용하여 각 작업을 순차적으로 실행합니다. 예를 들어, `num_total_tasks`가 10이라면 0부터 9까지의 작업을 차례로 처리합니다.  
  - 장점: 스레드 관리 오버헤드가 없어 간단하고, 작업이 매우 가벼울 때 유리합니다.  
  - 단점: 멀티코어 CPU를 활용하지 못해 성능이 제한됩니다.  

- **TaskSystemParallelSpawn:**  
  각 작업마다 새로운 스레드를 생성하여 병렬로 실행합니다. 예를 들어, 10개의 작업이 있으면 10개의 스레드를 생성하고, 각 스레드가 하나의 작업을 처리한 후 종료됩니다.  
  - 장점: 구현이 간단하고, 작업이 무거울 때 병렬 처리 효과를 볼 수 있습니다.  
  - 단점: 스레드 생성/소멸 오버헤드가 크며, 작업 수가 많을수록 성능 저하가 큽니다.  

- **TaskSystemParallelThreadPoolSpinning:**  
  초기화 시 고정된 수의 스레드를 생성하고, 작업이 없으면 스핀(CPU를 계속 사용)하며 대기합니다. 작업이 큐에 추가되면 스레드가 작업을 처리합니다.  
  - 장점: 스레드 재사용으로 생성 오버헤드 감소.  
  - 단점: 유휴 상태에서 CPU 자원을 낭비합니다. 작업이 빈번할 때 유리합니다.  

- **TaskSystemParallelThreadPoolSleeping:**  
  스레드 풀을 사용하지만, 작업이 없으면 조건 변수(`condition_variable`)를 통해 슬립 상태로 대기합니다. 작업이 추가되면 깨어나 처리합니다.  
  - 장점: 유휴 상태에서 CPU 자원을 절약하며, 작업이 덜 빈번할 때 효율적입니다.  
  - 단점: 슬립에서 깨어나는 데 약간의 지연이 있습니다.  

##### 3. Part A의 코드 예시  
아래는 `TaskSystemParallelThreadPoolSleeping`의 일부 코드입니다:  
```cpp
void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {
    std::lock_guard<std::mutex> lock(mutex);
    for (int i = 0; i < num_total_tasks; ++i) {
        tasks.push([runnable, i, num_total_tasks] {
            runnable->runTask(i, num_total_tasks);
        });
    }
    remaining_tasks = num_total_tasks;
    cv.notify_all();
}
```
여기서 `tasks` 큐에 각 작업을 추가하고, 조건 변수를 통해 스레드를 깨웁니다.  

#### Part B: 작업 그래프 실행 지원  

##### 1. 의존성 처리의 필요성  
Part B에서는 작업 간 의존성을 고려해야 합니다. 예를 들어, 작업 B가 작업 A의 결과를 필요로 한다면, A가 완료된 후에만 B를 실행해야 합니다. 이를 위해 `TaskSystemParallelThreadPoolSleeping` 클래스를 확장하여 그룹별 작업 관리를 추가했습니다.  

##### 2. 구현된 변경점  
- 각 작업 그룹은 고유한 `TaskID`를 가지며, `runAsyncWithDeps` 메서드를 통해 의존성을 설정합니다.  
- 의존성 그래프를 관리하기 위해 두 가지 데이터 구조를 사용합니다:  
  - `dependency_graph`: 각 `TaskID`가 의존하는 이전 작업 그룹 목록.  
  - `reverse_dependency_graph`: 각 `TaskID`가 어떤 작업 그룹에 의해 의존당하는지.  
- 작업 그룹이 준비 상태(`ready_tasks`)가 되려면, 모든 의존 작업 그룹이 완료되어야 합니다.  
- 작업 그룹의 작업은 `group_tasks` 맵에 저장되며, 준비 상태가 되면 작업 큐에 추가되어 스레드에 의해 실행됩니다.  

##### 3. 작업 흐름 예시  
예를 들어, 작업 그룹 A(의존성 없음), B(A에 의존), C(B에 의존)가 있다고 가정합시다:  
1. A가 준비 상태가 되어 `ready_tasks`에 추가됩니다.  
2. 스레드가 A의 작업을 실행하고, 완료되면 B의 의존성이 만족되어 B가 `ready_tasks`에 추가됩니다.  
3. B가 완료되면 C가 준비되어 실행됩니다.  
4. `sync()` 메서드는 모든 작업이 완료될 때까지 대기합니다.  

##### 4. 코드 수정 필요성  
초기 구현에서 작업 큐가 그룹별로 관리되지 않아 오류가 있었습니다. 수정된 버전에서는 `group_tasks` 맵을 사용하여 각 그룹의 작업을 별도로 관리하며, 작업 실행 후 그룹의 남은 작업 수를 추적합니다. 예를 들어:  
```cpp
TaskID task_id = next_task_id++;
group_tasks[task_id].push([runnable, i, num_total_tasks] {
    runnable->runTask(i, num_total_tasks);
});
group_remaining_tasks[task_id] = num_total_tasks;
```
이렇게 하면 각 그룹의 작업이 올바르게 실행 순서를 보장받습니다.  

#### 성능 분석  

##### 1. 직렬 vs. 병렬  
- **직렬 실행(TaskSystemSerial):** 작업이 매우 가볍거나 수가 적을 때 유리합니다. 예를 들어, "super_super_light" 테스트에서는 스레드 관리 오버헤드가 병렬 실행의 이득을 상쇄할 수 있습니다.  
- **병렬 실행:** 작업이 무거울 때 유리하며, 예를 들어 "mandelbrot_chunked" 테스트에서는 멀티코어 CPU를 활용하여 성능이 크게 향상됩니다.  

##### 2. 스레드 생성 vs. 스레드 풀  
- **스레드 생성(TaskSystemParallelSpawn):** 작업 수가 많을수록 스레드 생성 오버헤드가 커져 성능 저하가 큽니다.  
- **스레드 풀:** 스레드 재사용으로 오버헤드를 줄이며, 특히 작업이 빈번할 때 효율적입니다.  

##### 3. 스핀 vs. 슬립  
- **스핀(TaskSystemParallelThreadPoolSpinning):** 작업이 빈번할 때 유리하지만, 유휴 상태에서 CPU 자원을 낭비합니다.  
- **슬립(TaskSystemParallelThreadPoolSleeping):** 유휴 상태에서 CPU를 절약하지만, 슬립에서 깨어나는 지연이 있습니다. 작업이 덜 빈번할 때 적합합니다.  

##### 4. 의존성 처리의 영향  
의존성 처리는 추가 오버헤드를 유발하지만, 작업 순서 보장이 필요할 때 필수적입니다. 의존성 그래프의 복잡도와 작업 크기에 따라 성능 차이가 큽니다.  

#### 테스트 사례: simple_dependency_test  

##### 1. 테스트 내용  
"simple_dependency_test"는 세 작업 그룹(A, B, C)을 사용하여 의존성 처리를 검증합니다:  
- A는 의존성 없음.  
- B는 A에 의존.  
- C는 B에 의존.  
각 작업은 `SleepTask`를 사용하여 일정 시간 대기하며, 실행 순서를 로그로 확인합니다.  

##### 2. 검증 과정  
1. A가 먼저 실행되어 완료됩니다.  
2. A가 완료되면 B의 의존성이 만족되어 B가 실행됩니다.  
3. B가 완료되면 C가 실행됩니다.  
4. `sync()` 호출 후, 모든 작업이 완료되었는지 확인합니다.  

##### 3. 결과 및 영향  
테스트 결과, 작업이 의존성 순서(A→B→C)대로 실행됨을 확인했습니다. 초기 구현에서 `reverse_dependency_graph` 업데이트 누락으로 일부 작업이 실행되지 않는 문제가 있었으나, 수정 후 올바르게 동작합니다.  

#### 결론  
이 작업 시스템은 스레드 풀과 동적 할당을 통해 효율적인 병렬 실행을 제공하며, Part B에서 의존성 관리를 추가하여 복잡한 작업 그래프를 지원합니다. 성능은 작업 특성에 따라 달라지며, 테스트를 통해 구현의 정확성을 검증했습니다.  

---