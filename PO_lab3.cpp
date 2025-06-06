#include <iostream>
#include <thread>
#include <queue>
#include <shared_mutex>
#include <chrono>
#include <vector>

using namespace std;

const int min_time_ms = 3000;
const int max_time_ms = 6000;
const int generator_thread_num = 2;
const int worker_thread_num = 4;

class Task {
	static int next_id;
	int id;
	int delay_ms;

public:
	Task() {
		id = ++next_id;
		delay_ms = rand() % (max_time_ms - min_time_ms + 1) + min_time_ms;
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

	void execute() {
		this_thread::sleep_for(chrono::milliseconds(delay_ms));
	}
};

int Task::next_id = 0;


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

	inline bool pop(T& item) {
		unique_lock<shared_mutex> _(m);
		if (q.empty()) {
			return false;
		} else {
			item = move(q.front());
			q.pop();
			return true;
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
	ThreadSafeQueue<Task> queue;

public:
	ThreadPool() {}

	void start() {}

	void pause() {}

	void resume() {}

	void stop() {}

	void complete_and_stop() {}

};

//TODO
class TaskGenerator {
	ThreadSafeQueue<Task> queue;
	thread th;
	atomic<bool> should_continue;

public:
	void start() {
		while (should_continue.load()) {
			Task t;

		}
	}
};

int main() {
	ThreadSafeQueue<Task> q;
	ThreadPool pool;
	vector<TaskGenerator> generators;

	ThreadSafeQueue<int> queue;
	q.emplace();
	q.emplace();
	Task t;
	q.pop(t);

	
	return 0;
}
