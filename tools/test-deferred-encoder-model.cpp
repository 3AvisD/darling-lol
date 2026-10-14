// Host test for the deferred blit/render hand-off model (Indium, Linux build of the real worker header).
//
// The deferred blit encoder (deferred-blit-encoder.private.hpp) records its calls into an op list that owns the
// resources it mentions and posts the list to the ordered worker; the real encoder is created, fed and ended there.
// What has to hold for that to be correct is not the Vulkan part (which needs the guest GPU) but the hand-off:
//
//   1. everything a command buffer defers -- render chunks (posted while the pass is recorded), a blit list (posted at
//      endEncoding), the commit and the present -- is applied in command-buffer order, because one FIFO worker runs it;
//   2. the ops own their resources: a texture/buffer the game releases right after the call stays alive until the
//      worker replayed the op, and is released afterwards;
//   3. a chunk posted after the commit is dropped and the deferred count is balanced, so waitDeferred() cannot hang.
//
// This is a model: it uses the real SubmitThread/Token and the real waits header, and mirrors the structure of the two
// facades (op list owning shared_ptr, deferredBegin/End around the posted replay, replayAllowed() check at post time).
//
// Build (Linux host):
//   g++ -std=c++17 -O1 -pthread -I build/darling-src/src/external/metal/deps/indium/private-include \
//       tools/test-deferred-encoder-model.cpp -o /tmp/opencode/test-deferred-encoder-model
// Run: /tmp/opencode/test-deferred-encoder-model

#include <indium/submit-thread.private.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
	std::mutex logMutex;
	std::vector<std::string> applied;   // what the worker "recorded into the Vulkan command buffer", in order
	std::atomic<int> droppedChunks {0};

	void append(const std::string& entry) {
		std::lock_guard<std::mutex> lock(logMutex);
		applied.push_back(entry);
	}

	std::vector<std::string> snapshot() {
		std::lock_guard<std::mutex> lock(logMutex);
		return applied;
	}

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

	// the parts of PrivateCommandBuffer the facades depend on
	struct ModelCommandBuffer {
		std::mutex mutex;
		std::atomic<bool> committed {false};
		unsigned deferredPending = 0;
		std::mutex deferredMutex;
		std::condition_variable deferredCondvar;
		Gate* replayGate = nullptr;   // test hook: holds the worker inside a replay

		bool replayAllowed() const { return !committed.load(std::memory_order_acquire); }
		void deferredBegin() {
			std::lock_guard<std::mutex> lock(deferredMutex);
			++deferredPending;
		}
		void deferredEnd() {
			{
				std::lock_guard<std::mutex> lock(deferredMutex);
				--deferredPending;
			}
			deferredCondvar.notify_all();
		}
		void waitDeferred() {
			std::unique_lock<std::mutex> lock(deferredMutex);
			Indium::Waits::blockOn("model.waitDeferred", deferredCondvar, lock, [this] { return deferredPending == 0; });
		}
		void commit() {
			committed.store(true, std::memory_order_release);
			Indium::SubmitThread::post([this]() { append("commit"); });
		}
		void present() { Indium::SubmitThread::post([this]() { append("present"); }); }
	};

	// stands in for a Buffer/Texture: the op list owns the last reference
	struct Resource {
		std::string name;
		explicit Resource(std::string name_): name(std::move(name_)) {}
	};

	// mirrors DeferredBlitCommandEncoder: record, own the resources, post once at endEncoding
	class ModelBlitEncoder {
		std::shared_ptr<ModelCommandBuffer> _commandBuffer;
		std::vector<std::shared_ptr<Resource>> _resources;
		std::vector<std::string> _ops;
		bool _begun = false;
		bool _ended = false;

		void flush() {
			auto cb = _commandBuffer;
			if (!_begun) { _begun = true; cb->deferredBegin(); }
			auto ops = std::make_shared<std::vector<std::string>>(std::move(_ops));
			auto resources = std::make_shared<std::vector<std::shared_ptr<Resource>>>(std::move(_resources));
			if (!cb->replayAllowed()) {
				++droppedChunks;
				cb->deferredEnd();
				return;
			}
			Indium::SubmitThread::post([cb, ops, resources]() {
				if (cb->replayGate) cb->replayGate->wait();
				for (const auto& op: *ops) append(op);
				append("blit-end");   // the real encoder's endEncoding(): the end-of-encoder barrier
				// the ops (and the resources they own) die here, on the worker
				resources->clear();
				cb->deferredEnd();
			});
		}

	public:
		explicit ModelBlitEncoder(std::shared_ptr<ModelCommandBuffer> cb): _commandBuffer(std::move(cb)) {}
		~ModelBlitEncoder() { endEncoding(); }

		void copy(const std::shared_ptr<Resource>& source, const std::string& label) {
			_resources.push_back(source);
			_ops.push_back("blit:" + label);
		}
		void endEncoding() {
			if (_ended) return;
			_ended = true;
			flush();
		}
	};

	// mirrors DeferredRenderCommandEncoder's chunking: one op per chunk here, posted while the pass is recorded
	class ModelRenderEncoder {
		std::shared_ptr<ModelCommandBuffer> _commandBuffer;
		bool _begun = false;
		bool _ended = false;

		void flush(const char* entry) {
			auto cb = _commandBuffer;
			if (!_begun) { _begun = true; cb->deferredBegin(); }
			if (!cb->replayAllowed()) {
				++droppedChunks;
				cb->deferredEnd();
				return;
			}
			Indium::SubmitThread::post([cb, entry]() {
				if (cb->replayGate) cb->replayGate->wait();
				append(entry);
			});
		}

	public:
		explicit ModelRenderEncoder(std::shared_ptr<ModelCommandBuffer> cb): _commandBuffer(std::move(cb)) {}
		~ModelRenderEncoder() { endEncoding(); }
		void draw() { flush("render"); }
		void endEncoding() {
			if (_ended) return;
			_ended = true;
			flush("render-end");   // the last chunk also ends the real encoder (end-of-encoder barrier)
		}
	};

	void expectOrder(const std::vector<std::string>& expected, const char* what) {
		auto actual = snapshot();
		bool same = actual.size() == expected.size();
		for (size_t i = 0; same && i < actual.size(); ++i) same = actual[i] == expected[i];
		if (!same) {
			printf("FAIL %s: expected", what);
			for (const auto& entry: expected) printf(" %s", entry.c_str());
			printf("; got");
			for (const auto& entry: actual) printf(" %s", entry.c_str());
			printf("\n");
			fflush(stdout);
			assert(false);
		}
	}

	void testOrder() {
		applied.clear();
		auto cb = std::make_shared<ModelCommandBuffer>();
		{
			ModelRenderEncoder render(cb);
			render.draw();                     // render chunk 1
			render.draw();                     // render chunk 2
			ModelBlitEncoder blit(cb);
			blit.copy(std::make_shared<Resource>("upload"), "upload");
			blit.endEncoding();                // blit list posted at endEncoding
			render.draw();                     // render chunk 3 (after the blit)
			render.endEncoding();
		}
		cb->commit();
		cb->present();
		Indium::SubmitThread::drain("model.drain");
		std::vector<std::string> expected {"render", "render", "blit:upload", "blit-end", "render", "render-end", "commit", "present"};
		expectOrder(expected, "hand-off order");
		printf("PASS hand-off order: render chunks, blit list, later render chunk, commit, present\n");
	}

	void testResourceLifetime() {
		applied.clear();
		auto cb = std::make_shared<ModelCommandBuffer>();
		Gate gate;
		cb->replayGate = &gate;
		auto resource = std::make_shared<Resource>("texture");
		{
			ModelBlitEncoder blit(cb);
			blit.copy(resource, "toDrawable");
			blit.endEncoding();
		}
		resource.reset();                       // the game drops its reference right after the call
		// hold the worker inside the replay, then check that only the posted op keeps the resource alive
		gate.release();
		Indium::SubmitThread::drain("model.drain");
		cb->replayGate = nullptr;
		bool replayed = false;
		for (const auto& entry: snapshot()) if (entry == "blit:toDrawable") replayed = true;
		assert(replayed);
		assert(resource.use_count() == 0);      // released on the worker after the replay
		printf("PASS resource lifetime: op owned the resource until its replay, released afterwards\n");
	}

	void testDropAfterCommit() {
		applied.clear();
		auto cb = std::make_shared<ModelCommandBuffer>();
		cb->commit();                            // the command buffer is closed
		{
			ModelBlitEncoder blit(cb);
			blit.copy(std::make_shared<Resource>("late"), "late");
			blit.endEncoding();                  // must be dropped, not recorded into a closed command buffer
		}
		Indium::SubmitThread::drain("model.drain");
		assert(droppedChunks.load() == 1);
		for (const auto& entry: snapshot()) assert(entry != "blit:late");
		cb->waitDeferred();                      // balanced deferred count: returns immediately
		printf("PASS drop-after-commit: chunk dropped, deferred count balanced\n");
	}

	void testWaitDeferred() {
		applied.clear();
		auto cb = std::make_shared<ModelCommandBuffer>();
		Gate gate;
		cb->replayGate = &gate;
		{
			ModelBlitEncoder blit(cb);
			blit.copy(std::make_shared<Resource>("held"), "held");
			blit.endEncoding();
		}
		std::atomic<bool> returned {false};
		std::thread waiter([&]() {
			cb->waitDeferred();
			returned.store(true);
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		assert(!returned.load());                // still replaying: the waiter must block
		gate.release();
		waiter.join();
		assert(returned.load());
		cb->replayGate = nullptr;
		cb->waitDeferred();                      // and be a no-op once the replay ended
		printf("PASS waitDeferred blocks until the deferred op was replayed\n");
	}
}

int main() {
	testOrder();
	testResourceLifetime();
	testDropAfterCommit();
	testWaitDeferred();
	printf("PASS all deferred-encoder model tests\n");
	return 0;
}
