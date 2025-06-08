/*The thread pool is served by 4 worker threads and has one execution queue.
Tasks are added immediately to the end of the execution queue.
The task is taken from the buffer as soon as a free worker thread is available.
The task takes a random time of 3 to 6 seconds.*/

#include <iostream>
#include <thread>
#include <queue>
#include <shared_mutex>
#include <chrono>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <string>
#include <sstream>
#include <atomic>

using namespace std;
using namespace std::chrono;

const int min_time_ms = 3000;
const int max_time_ms = 6000;

class Task {
    static atomic<int> next_id;
    int id;
    int delay_ms;

public:
    Task() {
        id = ++next_id;
        delay_ms = rand() % (max_time_ms - min_time_ms + 1) + min_time_ms;
        string s = "Task " + to_string(id) + " has been created\n";
        cout << s;
    }

    Task(const Task&) = delete;

    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept : id(exchange(other.id, 0)), delay_ms(exchange(other.delay_ms, 0)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            id = exchange(other.id, 0);
            delay_ms = exchange(other.delay_ms, 0);
        }
        return *this;
    }

    void execute() const {
        string s = "Task " + to_string(id) + " has started executing (" + to_string(delay_ms) + " ms)\n";
        cout << s;
        this_thread::sleep_for(chrono::milliseconds(delay_ms));
        s = "Task " + to_string(id) + " has stopped\n";
        cout << s;
    }

    int get_id() const {
        return id;
    }
};

atomic<int> Task::next_id = 0;


template<typename T>
class ThreadSafeQueue {
    queue<T> q;
    mutable shared_mutex m;

public:
    inline ThreadSafeQueue() = default;

    inline ~ThreadSafeQueue() {
        clear();
    }

    template<typename... Args>
    inline void emplace(Args&&... args) {
        unique_lock<shared_mutex> _(m);
        q.emplace(forward<Args>(args)...);
    }

    inline T pop() {
        unique_lock<shared_mutex> _(m);
        if (q.empty()) {
            throw runtime_error("The queue is empty");
        }
        else {
            T item = move(q.front());
            q.pop();
            return item;
        }
    }

    inline bool empty() const {
        shared_lock<shared_mutex> _(m);
        return q.empty();
    }

    inline int size() const {
        shared_lock<shared_mutex> _(m);
        return q.size();
    }

    inline void clear() {
        unique_lock<shared_mutex> _(m);
        while (!q.empty()) {
            q.pop();
        }
    }
};

class ThreadPool {
    vector<thread> workers;
    size_t workers_num;
    ThreadSafeQueue<Task>* queue;
    bool is_initialized;
    atomic<bool> is_paused;
    atomic<bool> should_continue;//переделать на atomic_flag
    mutex mtx;
    condition_variable cond_var;


public:
    ThreadPool() : queue(nullptr), workers_num(0), is_initialized(false), is_paused(false), should_continue(true) {}

    ~ThreadPool() {
        if (is_initialized) {
            stop();
        }
    }

    void initialize(ThreadSafeQueue<Task>& tasks, int thread_num) {
        if (is_initialized) {
            throw runtime_error("The thread pool is already initialized");
        }
        queue = &tasks;
        workers.resize(thread_num);
        workers_num = thread_num;
        is_initialized = true;
    }

    void start() {
        if (!is_initialized) {
            throw runtime_error("The thread pool has not yet been initialized.");
        }
        cout << "********************************************************\n"
                "Thread pool is started\n"
                "********************************************************\n";
        for (auto &th : workers) {
            th = thread(&ThreadPool::work, this);
        }
    }

    void work() {
        thread::id tid = this_thread::get_id();
        ostringstream id_str;
        id_str << tid;
        string s;
        while (should_continue) {
            unique_lock<mutex> lock(mtx);
            cond_var.wait(lock, [&]() { return !is_paused && (!queue->empty() || !should_continue); });

            if (!should_continue)
                break;

            Task task = queue->pop();
            s = "Worker " + id_str.str() + " took on the task " + to_string(task.get_id()) + "\n";
            cout << s;
            lock.unlock();

            task.execute();
        }
    }

    void pause() {
        is_paused.store(true);
        cout << "********************************************************\n"
                "Thread pool is paused\n"
                "********************************************************\n";
    }

    void resume() {
        cout << "********************************************************\n"
                "Thread pool is resumed\n"
                "********************************************************\n";
        is_paused.store(false);
        cond_var.notify_all();
    }

    void stop() {
        should_continue.store(false);
        cond_var.notify_all();
        for (auto& th : workers) {
            th.join();
        }
        is_initialized = false;
        cout << "********************************************************\n"
                "Thread pool is completely stopped\n"
                "********************************************************\n";
    }

    void add_task() {
        queue->emplace();
        cond_var.notify_one();
    }

};

class TaskGenerator {
    ThreadPool* pool;
    int timeout_ms;
    int lifetime_sec;
    int id;
    static int next_id;
    thread generator;
    atomic<bool> should_continue;
    atomic<bool> is_paused;
    mutex mtx;
    condition_variable cond_var;

public:
    TaskGenerator() : pool(nullptr), timeout_ms(0), lifetime_sec(-1), id(++next_id), should_continue(true), is_paused(false) {}

    ~TaskGenerator() {
        should_continue.store(false);
        cond_var.notify_all();
        if (generator.joinable()) {
            generator.join();
        }
    }

    void start(ThreadPool& pool, int timeout_ms, int lifetime_sec) {
        if (generator.joinable()) {
            throw runtime_error("Generator " + to_string(id) + " is already running");
        }
        this->pool = &pool;
        this->timeout_ms = timeout_ms;
        this->lifetime_sec = lifetime_sec;
        should_continue.store(true);
        is_paused.store(false);
        generator = thread(&TaskGenerator::run, this);
    }

    void stop() {
        should_continue = false;
        generator.join();
    }

    void pause() {
        is_paused = true;
    }

    void resume() {
        is_paused = false;
        cond_var.notify_all();
    }

    inline void join() {
        if (generator.joinable()) {
            generator.join();
        }
    }

private:
    inline void generate_task() {
        pool->add_task();
        string s = "Generator " + to_string(id) + " added task\n";
        cout << s;
    }

    void run() {
        string s = "Generator " + to_string(id) + " is running\n";
        cout << s;
        auto start_time = steady_clock::now();
        while (should_continue) {
            unique_lock<mutex> lock(mtx);
            cond_var.wait(lock, [&]() {  return !is_paused.load(); });
            if (!should_continue) break;

            generate_task();

            if (cond_var.wait_for(lock, milliseconds(timeout_ms), [&]() {
                return !should_continue;
            })) {
                break;
            }

            if (steady_clock::now() - start_time >= seconds(lifetime_sec)) {
                break;
            }
        }
        s = "Generator " + to_string(id) + " has stopped\n";
        cout << s;
        should_continue = false;
    }
};

int TaskGenerator::next_id = 0;

int main() {
    const int generator_thread_num = 2;
    const int worker_thread_num = 4;
    const int lifetime_sec = 20;

    ThreadSafeQueue<Task> tasks;
    ThreadPool thread_pool;
    thread_pool.initialize(tasks, worker_thread_num);
    thread_pool.start();
    vector<TaskGenerator> generators(generator_thread_num);
    for (auto& th : generators) {
        th.start(thread_pool, (rand() % 4 + 1) * 1000, lifetime_sec);
    }

    for (auto& th : generators) {
        th.join();
    }

    thread_pool.pause();

    for (auto& th : generators) {
        th.start(thread_pool, (rand() % 4 + 1) * 1000, lifetime_sec / 2);
    }

    for (auto& th : generators) {
        th.join();
    }

    thread_pool.resume();

    for (auto& th : generators) {
        th.start(thread_pool, (rand() % 4 + 3) * 1000, lifetime_sec);
    }

    for (auto& th : generators) {
        th.join();
    }

    thread_pool.stop();

    return 0;
}
