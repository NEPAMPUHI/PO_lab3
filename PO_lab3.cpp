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
#include <map>

using namespace std;
using namespace std::chrono;

const int min_time_ms = 3000;
const int max_time_ms = 6000;

class Task {
	static atomic<size_t> next_id;
	size_t id;
	size_t duration_ms;

public:
	Task() {
		id = ++next_id;
		duration_ms = rand() % (max_time_ms - min_time_ms + 1) + min_time_ms;
	}

	Task(const Task&) = delete;

	Task& operator=(const Task&) = delete;

	Task(Task&& other) noexcept : id(exchange(other.id, 0)), duration_ms(exchange(other.duration_ms, 0)) {}

	Task& operator=(Task&& other) noexcept {
		if (this != &other) {
			id = exchange(other.id, 0);
			duration_ms = exchange(other.duration_ms, 0);
		}
		return *this;
	}

	void execute() const {
		this_thread::sleep_for(chrono::milliseconds(duration_ms));
	}

	size_t get_id() const {
		return id;
	}

	size_t get_duration() {
		return duration_ms;
	}
};

atomic<size_t> Task::next_id = 0;


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

class IThreadPoolEventListener {
public:
	virtual void on_task_added(size_t task_id, size_t current_queue_size) = 0;
	virtual void on_task_taken(size_t thread_id, size_t task_id, size_t task_duration_ms, size_t current_queue_size) = 0;
	virtual void on_task_completed(size_t thread_id, size_t task_id) = 0;
	virtual void on_thread_started(size_t thread_id) = 0;
};

class ThreadPoolStatistics : public IThreadPoolEventListener {
	map<size_t, steady_clock::time_point> task_added_time;
	map<size_t, steady_clock::time_point> task_exec_started_time;
	map<size_t, steady_clock::time_point> thread_last_task_completed_time;
	size_t total_completed_tasks = 0;
	size_t total_created_tasks = 0;
	long long total_task_duration_ms = 0;
	long long total_task_waiting_ms = 0;
	size_t thread_waiting_count = 0;
	long long thread_waiting_total_duration_ms = 0;
	size_t change_queue_size_counter = 0;
	size_t queue_size_sum = 0;
	mutex mtx;

public:
	void on_thread_started(size_t thread_id) override {
		lock_guard<mutex> _(mtx);
		thread_last_task_completed_time.emplace(thread_id, steady_clock::now());
	}

	void on_task_added(size_t task_id, size_t current_queue_size) override {
		lock_guard<mutex> _(mtx);
		task_added_time.emplace(task_id, steady_clock::now());
		total_created_tasks++;
		queue_size_sum += current_queue_size;
		change_queue_size_counter++;
	}

	void on_task_taken(size_t thread_id, size_t task_id, size_t task_duration_ms, size_t current_queue_size) override {
		lock_guard<mutex> _(mtx);
		auto now = steady_clock::now();

		long long wait_duration_ms = duration_cast<milliseconds>(now - thread_last_task_completed_time.at(thread_id)).count();
		thread_waiting_total_duration_ms += wait_duration_ms;
		thread_waiting_count++;

		task_exec_started_time.emplace(task_id, now);
		wait_duration_ms = duration_cast<milliseconds>(now - task_added_time.at(task_id)).count();
		total_task_waiting_ms += wait_duration_ms;

		queue_size_sum += current_queue_size;
		change_queue_size_counter++;
	}

	void on_task_completed(size_t thread_id, size_t task_id) override {
		lock_guard<mutex> _(mtx);
		auto now = steady_clock::now();

		long long wait_duration_ms = duration_cast<milliseconds>(now - task_exec_started_time.at(task_id)).count();
		total_task_duration_ms += wait_duration_ms;

		thread_last_task_completed_time.at(thread_id) = now;
		total_completed_tasks++;
	}

	double get_average_queue_size() {
		if (change_queue_size_counter == 0) return 0.0;
		return (double) queue_size_sum / (double) change_queue_size_counter;
	}

	size_t get_average_task_duration() {
		if (total_completed_tasks == 0) return 0;
		return total_task_duration_ms / total_completed_tasks;
	}

	size_t get_average_threads_waiting_time() {
		if (thread_waiting_count == 0) return 0;
		return thread_waiting_total_duration_ms / thread_waiting_count;
	}

	size_t get_average_task_waiting_time() {
		if (total_completed_tasks == 0) return 0;
		return total_task_waiting_ms / total_completed_tasks;
	}

	void print_stat() {
		string stat = "Total tasks: " + to_string(total_created_tasks) +
			"\nCompleted tasks: " + to_string(total_completed_tasks) +
			"\nAverage task duration: " + to_string(get_average_task_duration()) + " ms" +
			"\nAverage time between adding a task and starting its execution: " + to_string(get_average_task_waiting_time()) + " ms" +
			"\nThe threads' average waiting time: " + to_string(get_average_threads_waiting_time()) + " ms" +
			"\nAverage queue size: " + to_string(get_average_queue_size()) + "\n";
		cout << stat;
	}
};

class ThreadPoolConsoleLogger : public IThreadPoolEventListener {
	void on_task_added(size_t task_id, size_t current_queue_size) override {
		string log = "Task " + to_string(task_id) + " has been added\n";
		cout << log;
	}
	
	void on_task_taken(size_t thread_id, size_t task_id, size_t task_duration_ms, size_t current_queue_size) override {
		string log = "Task " + to_string(task_id) + " has started to be executed by the thread " + to_string(thread_id) + " (" + to_string(task_duration_ms) + "ms )\n";
		cout << log;
	}

	void on_task_completed(size_t thread_id, size_t task_id) override {
		string log = "Task " + to_string(task_id) + " has stopped\n";
		cout << log;
	}

	void on_thread_started(size_t thread_id) override {}
};

class ThreadPool {
	vector<thread> workers;
	size_t workers_num = 0;
	ThreadSafeQueue<Task>* queue = nullptr;
	bool is_initialized = false;
	atomic<bool> is_paused = false;
	atomic<bool> should_continue = true;
	mutex mtx;
	condition_variable cond_var;
	vector<IThreadPoolEventListener*> listeners;

public:
	ThreadPool() = default;

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

		for (auto& th : workers) {
			th = thread(&ThreadPool::work, this);
		}
	}

	void pause() {
		is_paused.store(true);
	}

	void resume() {
		is_paused.store(false);
		/*for (auto listener : listeners) {
			for (auto& worker : workers) {
				thread::id tid = worker.get_id();
				size_t tid_int = hash<thread::id>{}(tid);
				listener->on_thread_started(tid_int);
			}
		}*/
		cond_var.notify_all();
	}

	void stop() {
		should_continue.store(false);
		cond_var.notify_all();
		for (auto& th : workers) {
			th.join();
		}
		is_initialized = false;
	}

	void add_task() {
		Task task;
		size_t task_id = task.get_id();
		queue->emplace(move(task));
		for (auto listener : listeners) {
			listener->on_task_added(task_id, queue->size());
		}
		cond_var.notify_one();
	}

	void add_listener(IThreadPoolEventListener* listener) {
		listeners.push_back(listener);
	}

private:
	void work() {
		thread::id tid = this_thread::get_id();
		size_t tid_int = hash<thread::id>{}(tid);
		for (auto listener : listeners) {
			listener->on_thread_started(tid_int);
		}
		while (should_continue) {
			unique_lock<mutex> lock(mtx);
			cond_var.wait(lock, [&]() { return !is_paused && (!queue->empty() || !should_continue); });

			if (!should_continue)
				break;

			Task task = queue->pop();
			for (auto listener : listeners) {
				listener->on_task_taken(tid_int, task.get_id(), task.get_duration(), queue->size());
			}
			lock.unlock();

			task.execute();
			for (auto listener : listeners) {
				listener->on_task_completed(tid_int, task.get_id());
			}
		}
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
		string s = "Generator " + to_string(id) + " added task\n";
		cout << s;
		pool->add_task();
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
	ThreadPoolConsoleLogger logger;
	ThreadPoolStatistics stats;

	thread_pool.add_listener(&logger);
	thread_pool.add_listener(&stats);
	thread_pool.initialize(tasks, worker_thread_num);

	cout << "********************************************************\n"
		"Thread pool is started\n"
		"********************************************************\n";
	thread_pool.start();

	vector<TaskGenerator> generators(generator_thread_num);
	for (auto& th : generators) {
		th.start(thread_pool, (rand() % 4 + 1) * 1000, lifetime_sec);
	}

	for (auto& th : generators) {
		th.join();
	}

	thread_pool.pause();
	cout << "********************************************************\n"
		"Thread pool is paused\n"
		"********************************************************\n";

	for (auto& th : generators) {
		th.start(thread_pool, (rand() % 4 + 1) * 1000, lifetime_sec / 2);
	}

	for (auto& th : generators) {
		th.join();
	}

	cout << "********************************************************\n"
		"Thread pool is resumed\n"
		"********************************************************\n";
	thread_pool.resume();

	for (auto& th : generators) {
		th.start(thread_pool, (rand() % 4 + 3) * 1000, lifetime_sec);
	}

	for (auto& th : generators) {
		th.join();
	}

	thread_pool.stop();
	cout << "********************************************************\n"
		"Thread pool is completely stopped\n"
		"********************************************************\n";
	
	stats.print_stat();

	cout << "********************************************************\n"
		"Thread pool is started\n"
		"********************************************************\n";

	ThreadSafeQueue<Task> another_tasks;
	thread_pool.initialize(another_tasks, worker_thread_num);
	thread_pool.start();

	for (auto& th : generators) {
		th.start(thread_pool, (rand() % 2 + 1) * 1000, lifetime_sec / 2);
	}

	for (auto& th : generators) {
		th.join();
	}

	thread_pool.stop();
	cout << "********************************************************\n"
		"Thread pool is completely stopped\n"
		"********************************************************\n";

	stats.print_stat();
	return 0;
}
