# app-util

`app-util`은 여러 application module에서 공통으로 사용하는 작은 유틸리티 모듈이다. 현재 주요 구성은 주기 작업용 `IntervalTimer`, 비동기 작업 실행용 `ThreadPool`, 문자열 helper, 동기 로그 helper다.

## IntervalTimer

`IntervalTimer`는 지정한 interval마다 callback을 반복 실행하는 RAII timer다. 객체가 생성되면 내부 worker thread가 바로 시작되고, `stop()` 또는 소멸자에서 thread를 종료한다.

```cpp
#include "IntervalTimer.h"

#include <chrono>
#include <iostream>

IntervalTimer timer(
    std::chrono::milliseconds(1000),
    [] {
        std::cout << "tick\n";
    });

// application running...

timer.stop();
```

기본값은 다음과 같다.

- `run_immediately = true`: 생성 직후 callback을 한 번 실행한 뒤 interval 대기
- `policy = SchedulePolicy::FixedRate`: 예정된 tick 기준으로 다음 실행 시간을 계산

즉 아래 코드는 생성 즉시 한 번 실행하고, 이후 5초마다 실행한다.

```cpp
IntervalTimer discoveryTimer(
    std::chrono::seconds(5),
    [] {
        // periodic discovery, cache refresh, health check 등
    });
```

생성 직후 실행하지 않고 첫 interval 이후부터 실행하려면 세 번째 인자를 `false`로 준다.

```cpp
IntervalTimer timer(
    std::chrono::seconds(5),
    [] {
        // first run after 5 seconds
    },
    false);
```

### SchedulePolicy

`SchedulePolicy`는 callback 실행 시간이 길어졌을 때 다음 tick을 어떻게 계산할지 정한다.

- `FixedRate`: 이전 예정 시각에 interval을 더한다. 장기 드리프트를 줄이고 싶을 때 사용한다.
- `FixedDelay`: callback이 끝난 시점부터 interval을 다시 잰다. 실제 실행 사이 간격을 일정하게 유지한다.
- `SkipMissed`: 작업이 많이 늦어졌으면 현재 시각 기준으로 다음 tick을 재정렬한다.

```cpp
IntervalTimer timer(
    std::chrono::seconds(10),
    [] {
        // slow task
    },
    true,
    SchedulePolicy::SkipMissed);
```

callback에서 예외가 발생해도 timer thread가 죽지 않도록 내부에서 잡고 stderr에 로그를 남긴다. 그래도 callback 안에서는 필요한 오류 처리를 직접 하는 편이 좋다.

## ThreadPool

`ThreadPool`은 여러 worker thread가 priority queue에서 작업을 꺼내 실행하는 간단한 thread pool이다. 우선순위 값이 클수록 먼저 실행된다.

```cpp
#include "ThreadPool.hpp"

#include <iostream>

ThreadPool pool(4, 1024);

std::future<int> result = pool.submit(10, [] {
    return 42;
});

std::cout << result.get() << "\n";

pool.shutdown(ThreadPool::ShutdownMode::Graceful);
```

생성자 인자는 다음과 같다.

```cpp
ThreadPool pool(threadCount, maxQueueSize);
```

- `threadCount`: worker thread 수. 기본값은 `std::thread::hardware_concurrency()`다.
- `maxQueueSize`: 대기열 최대 작업 수. 기본값은 `1024`다.

### submit

`submit(priority, callable, args...)`는 작업을 queue에 넣고 `std::future`를 반환한다.

```cpp
auto future = pool.submit(5, [](int a, int b) {
    return a + b;
}, 10, 20);

int value = future.get();
```

작업 함수에서 예외가 발생하면 worker thread에서 바로 터지지 않고, `future.get()` 호출 시 재전파된다.

```cpp
auto future = pool.submit(1, []() -> int {
    throw std::runtime_error("failed");
});

try {
    future.get();
} catch (const std::exception& e) {
    // handle task error
}
```

queue가 가득 차면 `submit()`은 공간이 날 때까지 대기한다. shutdown 이후 호출하면 `std::runtime_error("ThreadPool stopped")`를 던진다.

### submit_with_timeout

과부하 상황에서 무한 대기를 피하려면 `submit_with_timeout()`을 사용한다. 지정한 시간 안에 queue에 넣지 못하면 `std::runtime_error("submit timeout")`을 던진다.

```cpp
auto future = pool.submit_with_timeout(
    10,
    std::chrono::milliseconds(100),
    [] {
        // bounded enqueue wait
    });
```

### shutdown

`shutdown()`은 두 가지 모드를 지원한다.

- `ShutdownMode::Graceful`: 새 작업은 막고, 이미 queue에 있는 작업을 처리한 뒤 종료한다.
- `ShutdownMode::Immediate`: 새 작업은 막고, 대기 중인 작업은 버린 뒤 가능한 빨리 종료한다.

```cpp
pool.shutdown(ThreadPool::ShutdownMode::Graceful);
```

소멸자는 `Graceful` 모드로 자동 shutdown을 호출한다. 명시적으로 빨리 종료해야 하는 경우에만 `Immediate`를 사용한다.

## 사용 기준

- 일정 주기로 같은 작업을 반복해야 하면 `IntervalTimer`를 사용한다.
- 많은 독립 작업을 병렬 처리해야 하면 `ThreadPool`을 사용한다.
- 주기적으로 작업을 만들고 실제 처리는 병렬화해야 하면 둘을 함께 사용한다.

```cpp
ThreadPool pool(4);

IntervalTimer timer(
    std::chrono::seconds(5),
    [&pool] {
        pool.submit(1, [] {
            // periodic async work
        });
    });
```

이 경우 timer callback이 너무 오래 blocking되지 않도록, 무거운 작업은 thread pool에 넘기고 timer callback은 빠르게 반환하는 편이 좋다.
