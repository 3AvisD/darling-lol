// Host model of the render-encoder resource arguments (Metal wrapper + Indium).
//
// The wrapper used to hand each resource to Indium by value, `setFragmentTexture(std::shared_ptr<
// Texture>, size_t)`: the caller built a temporary shared_ptr for the argument (one atomic
// increment), stored it so the callee could move it into the recorded op, and the callee's
// parameter handling zeroed that temporary again; the deferred encoder then had to load back what
// the caller had just stored (the store-to-load chain the profile shows in
// DeferredRenderCommandEncoder::setFragmentTexture). The interface now takes
// `const std::shared_ptr<Texture>&`, and the op takes exactly one reference into the record that
// keeps the resource alive until the worker replays it.
//
// What a model can prove here is the lifetime rule that makes both shapes correct - and that had
// to survive the change: the op owns a strong reference, so the resource outlives the game's own
// references and is still alive when the worker replays the op (this is what makes the round-2
// bind record's raw Indium address meaningful: a recorded address cannot be reused while the op
// holds the resource). The model runs both call shapes over a counting reference count and checks
// that property, plus that the reference is released exactly once when the op is.
//
// The cost side (what the change removed) is measured in the report from the two builds'
// disassembly, not from this model: the by-value shape compiles an increment plus a stack copy in
// the caller and a store-to-load chain in the callee, the reference shape neither.
//
// Compile with: c++ -std=c++17 -O2 -Wall -Wextra -o bind-refcount-model bind-refcount-model.cpp
#include <cstdio>
#include <string>
#include <vector>

static unsigned long g_increments = 0, g_decrements = 0, g_destructions = 0, g_replays = 0;

struct Resource {
	int refs = 0;
	bool alive = false;
};

static void IncRef(Resource* r) { ++g_increments; ++r->refs; r->alive = true; }
static void DecRef(Resource* r)
{
	++g_decrements;
	if (--r->refs == 0) { ++g_destructions; r->alive = false; }
}

// a shared_ptr<Resource> in shape: a strong reference, moved without touching the count
struct Shared {
	Resource* p = nullptr;
	Shared() {}
	explicit Shared(Resource* r) : p(r) { if (p) IncRef(p); }
	Shared(const Shared& o) : p(o.p) { if (p) IncRef(p); }
	Shared(Shared&& o) noexcept : p(o.p) { o.p = nullptr; }
	Shared& operator=(const Shared& o) { if (o.p) IncRef(o.p); if (p) DecRef(p); p = o.p; return *this; }
	Shared& operator=(Shared&& o) noexcept { if (p) DecRef(p); p = o.p; o.p = nullptr; return *this; }
	~Shared() { if (p) DecRef(p); }
};

struct Op { Shared ref; };                          // one recorded call: root of the resource's life

struct DeferredEncoder {
	std::vector<Op> ops;
	// the shipping addRef(): `o.p = ref` takes one reference into the op
	void record(const Shared& reference) { ops.emplace_back(); ops.back().ref = reference; }
	// the old by-value signature: the parameter is a shared_ptr the callee moves into the op
	void recordByValue(Shared parameter) { ops.emplace_back(); ops.back().ref = std::move(parameter); }
	void replay() { for (auto& op: ops) if (op.ref.p && op.ref.p->alive) ++g_replays; ops.clear(); }
};

struct Wrapper { Shared resource; };                 // the game's MTLTexture/MTLBuffer object

// the shipping shape (from the wrapper's setFragmentTexture:atIndex: and the deferred encoder's
// setFragmentTexture(const std::shared_ptr<Texture>&)): the wrapper passes its own reference
static void BindByReference(DeferredEncoder& encoder, const Wrapper& wrapper)
{
	encoder.record(wrapper.resource);
}

// the pre-change shape: the by-value argument is a caller-side temporary of the resource (the
// builds without the change compile its store plus an increment here) and the callee moves it
// into the op, so the temporary is empty afterwards and only the *store-to-load* chain - not a
// second reference - separates the two shapes
static void BindByValue(DeferredEncoder& encoder, const Wrapper& wrapper)
{
	Shared argument(wrapper.resource);               // the caller's temporary: one increment
	encoder.recordByValue(std::move(argument));      // the callee moves it into the op
}

static bool Run(const char* name, bool byValue)
{
	g_increments = g_decrements = g_destructions = g_replays = 0;
	Resource resource;
	Shared gameReference(&resource);                 // the game's reference to the Indium object
	bool aliveAtReplay = false;
	unsigned long incrementsDuringBind = 0, decrementsDuringBind = 0;
	{
		Wrapper wrapper;
		wrapper.resource = gameReference;            // the wrapper holds its own reference
		DeferredEncoder encoder;
		const unsigned long inc0 = g_increments, dec0 = g_decrements;
		if (byValue) BindByValue(encoder, wrapper); else BindByReference(encoder, wrapper);
		incrementsDuringBind = g_increments - inc0;
		decrementsDuringBind = g_decrements - dec0;
		// the game drops every reference of its own before the worker replays the op
		wrapper.resource = Shared();
		gameReference = Shared();
		aliveAtReplay = !encoder.ops.empty() && encoder.ops.front().ref.p && encoder.ops.front().ref.p->alive;
		encoder.replay();
	}
	printf("bind-refcount %-10s inc=%lu dec=%lu duringBind (inc=%lu dec=%lu) resourceAliveAtReplay=%s replaySeesResource=%s releasedAfterReplay=%s\n",
		name, g_increments, g_decrements, incrementsDuringBind, decrementsDuringBind,
		aliveAtReplay ? "yes" : "no", g_replays == 1 ? "yes" : "no", g_destructions == 1 ? "yes" : "no");
	// the rule both shapes must keep: the op's reference outlives the game's own references, the
	// replay still sees the resource, and the reference is released exactly once
	return aliveAtReplay && g_replays == 1 && g_destructions == 1;
}

int main()
{
	const bool oldOk = Run("by-value", true);
	const bool newOk = Run("by-ref", false);
	printf("BIND_REFCOUNT_MODEL by-value=%s by-ref=%s checks=%s\n", oldOk ? "ok" : "FAIL", newOk ? "ok" : "FAIL",
		(oldOk && newOk) ? "pass" : "FAIL");
	return (oldOk && newOk) ? 0 : 1;
}
