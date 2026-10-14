// Host test for the ordered submit worker's hand-off primitives (Indium, Linux build of the real headers).
//
// The game's thread and the submit worker exchange work through submit-thread.private.hpp; the two things that decide
// frame pacing are (a) FIFO order of everything posted, and (b) how long a caller waits at the frame end. This test
// exercises the real header on the host (no Vulkan, no guest):
//
//   1. post() runs tasks on one worker thread, in post order;
//   2. the present-dispatch Token returns as soon as *its own* task ran, even with a later task still queued, while
//      drain() waits for everything;
//   3. the wait-site counters (waits.private.hpp) count only waits that actually blocked, and produce the documented
//      summary line (the test points DARLING_WAITS_LOG at a temporary file and creates /tmp/metal-debug-waits);
//   4. enabling them writes exactly one "waits enabled" line at the first check, and without the flag (a child
//      process forked before the flag file is created) they stay off and write nothing, even for a blocking wait.
//
// Build (Linux host):
//   g++ -std=c++17 -O1 -pthread -I build/darling-src/src/external/metal/deps/indium/private-include \
//       tools/test-deferred-handoff.cpp -o /tmp/opencode/test-deferred-handoff
// Run: /tmp/opencode/test-deferred-handoff

#include <indium/submit-thread.private.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
	std::mutex logMutex;
	std::vector<std::string> log;
	std::atomic<bool> sawWorkerThread {false};

	void append(const std::string& entry) {
		std::lock_guard<std::mutex> lock(logMutex);
		log.push_back(entry);
	}

	bool contains(const std::string& entry) {
		std::lock_guard<std::mutex> lock(logMutex);
		for (const auto& item: log) if (item == entry) return true;
		return false;
	}

	std::vector<std::string> snapshot() {
		std::lock_guard<std::mutex> lock(logMutex);
		return log;
	}

	void sleepMs(int ms) {
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	}

	// a task that only runs once the test opens its gate
	struct Gate {
		std::mutex mutex;
		std::condition_variable condvar;
		bool open = false;
		void wait() {
			std::unique_lock<std::mutex> lock(mutex);
			condvar.wait(lock, [this] { return open; });
		}
		void release() {
			{
				std::lock_guard<std::mutex> lock(mutex);
				open = true;
			}
			condvar.notify_all();
		}
	};

	void testFifo() {
		constexpr int count = 200;
		for (int i = 0; i < count; ++i) {
			Indium::SubmitThread::post([i]() {
				sawWorkerThread = Indium::SubmitThread::onWorker();
				append("t" + std::to_string(i));
			});
		}
		sleepMs(100);   // let the worker finish, so the drain below is the idle (uncounted) kind
		Indium::SubmitThread::drain("test.fifo.idle");
		auto entries = snapshot();
		assert(entries.size() == (size_t) count);
		for (int i = 0; i < count; ++i) assert(entries[i] == "t" + std::to_string(i));
		assert(sawWorkerThread.load());
		assert(!Indium::SubmitThread::onWorker());   // the test thread is the caller, not the worker
		printf("PASS fifo-order (%d tasks, post order preserved)\n", count);
	}

	void testNarrowToken() {
		Gate gate;
		Indium::SubmitThread::Token token;
		// this commit's task: the "present dispatch" happens here, then the token signals
		Indium::SubmitThread::post([&token]() {
			sleepMs(2);
			append("present");
			token.signal();
		});
		// a later task (another frame's upload) that the caller does not depend on
		Indium::SubmitThread::post([&gate]() {
			gate.wait();
			append("upload");
		});
		token.wait("test.token");
		assert(contains("present"));
		assert(!contains("upload"));   // the narrow wait must not wait for tasks queued after its own
		printf("PASS present-dispatch token (returned while a later task was still queued)\n");
		gate.release();
		Indium::SubmitThread::drain("test.gated");
		assert(contains("upload"));    // drain does wait for it
		printf("PASS drain-waits-for-later-tasks\n");
	}

	void testIdleDrainIsNotCounted() {
		Indium::SubmitThread::drain("test.idle");
		printf("PASS idle drain returns immediately\n");
	}

	std::vector<std::string> waitsLines() {
		std::ifstream file(getenv("DARLING_WAITS_LOG"));
		std::vector<std::string> lines;
		std::string line;
		while (std::getline(file, line)) if (line.compare(0, 6, "waits@") == 0) lines.push_back(line);
		return lines;
	}

	size_t enabledLines() {
		std::ifstream file(getenv("DARLING_WAITS_LOG"));
		size_t count = 0;
		std::string line;
		while (std::getline(file, line)) if (line.compare(0, 14, "waits enabled@") == 0) ++count;
		return count;
	}

	bool anyLineHas(const std::vector<std::string>& lines, const std::string& needle) {
		for (const auto& line: lines) if (line.find(needle) != std::string::npos) return true;
		return false;
	}

	void testCounters() {
		// a blocking drain: one task sleeps, so the caller really blocks
		Indium::SubmitThread::post([]() { sleepMs(20); append("slow"); });
		Indium::SubmitThread::drain("test.blocking");
		assert(contains("slow"));

		// a fresh dump window, then one idle drain (which must not be counted) and a probe record
		sleepMs(5200);
		Indium::SubmitThread::drain("test.idle2");
		Indium::Waits::record("test.probe", 1000);

		auto lines = waitsLines();
		assert(!lines.empty());
		assert(enabledLines() == 1);   // the first check announced the counters, exactly once
		assert(anyLineHas(lines, "test.blocking[n=1 "));
		assert(anyLineHas(lines, "test.probe[n=1 "));
		assert(anyLineHas(lines, "test.token[n=1 "));
		assert(anyLineHas(lines, "test.gated[n=1 "));
		// every wait that found its predicate already true: never recorded
		assert(!anyLineHas(lines, "test.idle"));
		printf("PASS counters: only blocking waits recorded, summary format ok\n");
		{
			std::ifstream file(getenv("DARLING_WAITS_LOG"));
			std::string line;
			while (std::getline(file, line)) if (line.compare(0, 14, "waits enabled@") == 0) printf("  %s\n", line.c_str());
		}
		for (const auto& line: lines) printf("  %s\n", line.c_str());
	}
}

int main() {
	// The counters latch their flag at the first use; point the summary at a temporary file and enable the counters.
	std::string logPath = "/tmp/opencode/waits-test.log";
	unlink(logPath.c_str());
	setenv("DARLING_WAITS_LOG", logPath.c_str(), 1);
	bool flagExisted = access("/tmp/metal-debug-waits", F_OK) == 0;
	if (!flagExisted) {
		// default (no flag file): a child that has never checked the flag must see the counters off, and a blocking
		// wait must leave the log untouched (no "waits enabled" line, no summary)
		pid_t child = fork();
		assert(child >= 0);
		if (child == 0) {
			if (Indium::Waits::enabled()) _exit(2);
			Indium::SubmitThread::post([]() { sleepMs(20); });
			Indium::SubmitThread::drain("test.disabled");
			{ Indium::Waits::Block block("test.disabled.block"); sleepMs(1); }
			_exit(access(logPath.c_str(), F_OK) == 0 ? 3 : 0);
		}
		int status = 0;
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		printf("PASS counters off without the flag file (nothing written)\n");

		FILE* flag = fopen("/tmp/metal-debug-waits", "w");
		assert(flag != nullptr);
		fclose(flag);
	}
	assert(Indium::Waits::enabled());

	testIdleDrainIsNotCounted();
	testFifo();
	testNarrowToken();
	testCounters();

	if (!flagExisted) unlink("/tmp/metal-debug-waits");
	printf("PASS all deferred hand-off tests\n");
	return 0;
}
