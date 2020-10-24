# TPL(Task Parallel Library)

## 1. What is it?

The full name of TPL is Task Parallel Library, which is named after the `Task Parallel Library` of C#.

As the name implies, this library is used to write parallel pattern code. It makes your life easier when writing parallel code.


## 2. Features

- `Light weight`: The only dependency is a C++ 17 compatible compiler.
- `Task based`: Represents a background task with tpl::Task object
- Task dependency support
- `Then semantic`: Similar to C#, which means 'execute the task after it's precede complete'
- `Unwrap semantic`: Similar to C#, it converts a `Task<Task<T>>`(Task of Task) to a proxy task of type `Task<T>`(Task), which means you can do a serials of asynchronous operation with `Then chain`, instead of embeded multi-level callback (so called `callback hell`).
- Automatic callback type check in compile time.
- A simple parallel scheduler is provided.
- Custom task schdulers are supported.
- Each background task may have different schdulers.

## 3. Show me the code

**NOTE1**: The `Task` object is just a thin wrapper of a reference counted of `TaskImpl`, you can pass `Task` object by value.

**NOTE2**: Don't manually invoke the `Start` function unless the corresponding task has no precede.

The runnable code can be found in `example/example1.cpp` in this repository.

### 3.1. Example1: Basic usage

The following code does:

1. Execute `task1`(1 second), `task2`(2 seconds) and `task3`(0.5 second) simultaneously in the background.
2. Once the above 3 tasks done (2 seconds later), the `task4` will be executed in the background, and the result of the former 3 tasks will be passed as `task4`'s parameter.
3. Once the `task4` done, the `task5` will be executed in background.
4. The main thread waits untill `task5` done.

```C++
tpl::ParallelTaskScheduler scheduler(8);
tpl::Task<int> task5 {};
{
    tpl::Task<int> task(
        []() {
            SleepFor(1000);
            LOG << "Task1 ";
            return 1;
        },
        scheduler);
    tpl::Task<float> task2(
        []() {
            SleepFor(2000);
            LOG << "Task2 ";
            return 3.4f;
        },
        scheduler);
    tpl::Task<void> task3(
        []() {
            SleepFor(500);
            LOG << "Task3 ";
        },
        scheduler);

    tpl::Task<int> task4(
        [](const tpl::Task<int>& a, const tpl::Task<float>& b, const tpl::Task<void>& c) -> int {
            LOG << "Task 4, value: " << a.GetFuture().GetValue() << ", " << b.GetFuture().GetValue();
            return 2;
        },
        scheduler, task, task2, task3);
    task5 = task4.Then(
        [](const tpl::Task<int>& a) -> int {
            LOG << "Task 5, value: " << a.GetFuture().GetValue();
            SleepFor(3000);
            return 2;
        });
    task.Start();
    task2.Start();
    task3.Start();
}

auto result = task5.GetFuture().GetValue();
LOG << "Result is: " << result;
```

The output is:

```
Task3
Task1
Task2
// The order of 3 lines above is random
Task 4, value: 1, 3.4
Task 5, value: 2
Result is: 2
```

### 3.2. Example2: Unwrap semantic

The following code does:

1. Execute a task in background (denoted as Task1).

2. The task1 executes another asynchronous API, which will return "Hello from inner task" 1 seoncd late. (denoted as Task2)

3. Print the result after Task2 returns. (demoted as Task3)

4. The main thread waits untill `Task3` done.

```C++
auto afterInnerTaskReturn = tpl::MakeTaskAndStart(
    [this]() { // Task1
        // This is another sync operation
        tpl::Task<std::string> wrappedTask = tpl::MakeTask(
            [this]() -> std::string { // Task2
                SleepFor(1000);
                return "Hello from inner task";
            },
            &scheduler);
        wrappedTask.Start();
        return wrappedTask; // * Position 1
    },
    &scheduler
) // <---- Here, the origianl return value's type is Task<type of wrapperTask>
.Unwrap() // <---- Create a proxy task, you can think of the returned task is the wrappedTask above (Position 1). It's a proxy task because the wrappedTask is likely not available yet
.Then( // <---- This task will be executed after wrappedTask (Position 1) done.
    [](const tpl::Task<std::string>& innerTask) { // Task3
        LOG << "Then message from inner task is: " << innerTask.GetFuture().GetValue();
        return 100;
    }
);

LOG << "Waiting for tasks";
auto& fut = afterInnerTaskReturn.GetFuture();
fut.Wait();
LOG << "After inner task return, we get " << fut.GetValue();
```

The output is:

```
Waiting for tasks
Then message from inner task is: Hello from inner task
After inner task return, we get 100
```

## 4. TODO

- [ ] Exception support

- [ ] Cancellation support

- [ ] Concept support (if C++20 available)

- [ ] co_await operator support (if C++20 avalable)
