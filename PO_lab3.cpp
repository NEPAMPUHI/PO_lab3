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

	void execute() {
		this_thread::sleep_for(chrono::milliseconds(delay_ms));
	}
};

int Task::next_id = 0;

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


template <typename T>
class ThreadSafeQueue {
	queue<T> q;
	shared_mutex m;




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

int main() {
	ThreadSafeQueue<Task> queue;
	ThreadPool pool;
	vector<TaskGenerator> generators;
	return 0;
}
