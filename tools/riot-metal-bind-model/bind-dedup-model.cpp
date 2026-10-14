// Differential model of the Metal wrapper's per-encoder bind tracking.
//
// The tracking helpers are extracted verbatim from the shipping source by
// extract-bind-tracking.py (see bind-tracking.extract.inc); the decision sequences
// below are transcriptions of the setter methods in
// build/darling-src/src/external/metal/src/Metal/MTLRenderCommandEncoder.mm, with the
// source line references, and they call those extracted helpers.
//
// Two encoder states are driven by the same call sequence:
//   * `all`       applies every call (what a wrapper without tracking would record)
//   * `forwarded` applies only the calls the tracking forwards
// and the two are compared after *every* call. Equal states means every dropped call
// was redundant: nothing downstream (Indium's per-stage binding tables, the deferred
// op stream replayed from them, the Vulkan descriptor sets built from those) can tell
// the two apart.
//
// The modelled state mirrors Indium's binding tables
// (deps/indium/private-include/indium/command-encoder.private.hpp: FunctionResources):
// a buffer slot holds an owner plus an offset and inline bytes can replace it, a
// texture/sampler slot holds an owner, and a table only covers slots something grew it
// to (growToIndex). Calls the model does not need are not part of the changed paths.
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

typedef uint64_t NSUInteger;   // Darwin's typedef; same width here

#include "bind-tracking.extract.inc"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// ---------------------------------------------------------------- encoder state

struct SlotState {
	enum Kind { unbound, buffer, bytes } kind = unbound;
	const void* object = nullptr;   // buffer owner, or the inline bytes' content pointer
	size_t offset = 0;

	bool operator==(const SlotState& other) const {
		return kind == other.kind && object == other.object && offset == other.offset;
	}
	bool operator!=(const SlotState& other) const { return !(*this == other); }
};

struct EncoderState {
	std::vector<SlotState> vertexBuffers;
	std::vector<SlotState> fragmentBuffers;
	std::vector<const void*> vertexTextures;
	std::vector<const void*> fragmentTextures;
	std::vector<const void*> vertexSamplers;
	std::vector<const void*> fragmentSamplers;
	const void* pipeline = nullptr;
	const void* depthStencil = nullptr;

	// Indium grows a table to the index it is asked about; setXBufferOffset only
	// touches a slot that already exists (it logs and returns otherwise).
	static void grow(std::vector<SlotState>& slots, NSUInteger index) {
		if (slots.size() <= index) slots.resize(index + 1);
	}
	static void grow(std::vector<const void*>& slots, NSUInteger index) {
		if (slots.size() <= index) slots.resize(index + 1);
	}
};

static bool equal(const EncoderState& a, const EncoderState& b) {
	return a.vertexBuffers == b.vertexBuffers && a.fragmentBuffers == b.fragmentBuffers &&
		a.vertexTextures == b.vertexTextures && a.fragmentTextures == b.fragmentTextures &&
		a.vertexSamplers == b.vertexSamplers && a.fragmentSamplers == b.fragmentSamplers &&
		a.pipeline == b.pipeline && a.depthStencil == b.depthStencil;
}

// ---------------------------------------------------------------- calls

struct Call {
	enum Kind {
		VertexTexture, FragmentTexture, VertexSampler, FragmentSampler,
		VertexBuffer, FragmentBuffer, VertexBufferOffset, FragmentBufferOffset,
		VertexBytes, FragmentBytes, VertexBuffers, FragmentBuffers,
		Pipeline, DepthStencil,
	} kind = Pipeline;
	const void* object = nullptr;    // the Indium resource's address (what the shipping key compares)
	size_t offset = 0;
	NSUInteger index = 0;
	NSUInteger length = 0;
	const void* wrapper = nullptr;   // the Objective-C wrapper's address; reusable (round-1 key)
	const void* id = nullptr;        // the object's identity (what the encoder state holds); an address
	                                 // can host several identities over time, that is what ABA is
};

// Applies a call to the model state, in the order the wrapper would forward it.
static void apply(const Call& call, EncoderState& state) {
	// what the encoder state holds is the object's identity, not its address: an address can host
	// more than one object over time (that is the ABA case), and the state must tell them apart
	const void* object = call.id ? call.id : call.object;
	const void* buffers[8];
	size_t offsets[8];
	switch (call.kind) {
		case Call::VertexTexture:
			EncoderState::grow(state.vertexTextures, call.index);
			state.vertexTextures[call.index] = object;
			break;
		case Call::FragmentTexture:
			EncoderState::grow(state.fragmentTextures, call.index);
			state.fragmentTextures[call.index] = object;
			break;
		case Call::VertexSampler:
			EncoderState::grow(state.vertexSamplers, call.index);
			state.vertexSamplers[call.index] = object;
			break;
		case Call::FragmentSampler:
			EncoderState::grow(state.fragmentSamplers, call.index);
			state.fragmentSamplers[call.index] = object;
			break;
		case Call::VertexBuffer:
			EncoderState::grow(state.vertexBuffers, call.index);
			state.vertexBuffers[call.index] = SlotState { SlotState::buffer, object, call.offset };
			break;
		case Call::FragmentBuffer:
			EncoderState::grow(state.fragmentBuffers, call.index);
			state.fragmentBuffers[call.index] = SlotState { SlotState::buffer, object, call.offset };
			break;
		case Call::VertexBufferOffset:
			if (call.index >= state.vertexBuffers.size()) break;
			state.vertexBuffers[call.index].offset = call.offset;
			break;
		case Call::FragmentBufferOffset:
			if (call.index >= state.fragmentBuffers.size()) break;
			state.fragmentBuffers[call.index].offset = call.offset;
			break;
		case Call::VertexBytes:
			EncoderState::grow(state.vertexBuffers, call.index);
			state.vertexBuffers[call.index] = SlotState { SlotState::bytes, object, 0 };
			break;
		case Call::FragmentBytes:
			EncoderState::grow(state.fragmentBuffers, call.index);
			state.fragmentBuffers[call.index] = SlotState { SlotState::bytes, object, 0 };
			break;
		case Call::VertexBuffers:
			for (NSUInteger i = 0; i < call.length; ++i) {
				buffers[i] = object; offsets[i] = call.offset + i;
			}
			for (NSUInteger i = 0; i < call.length; ++i) {
				EncoderState::grow(state.vertexBuffers, call.index + i);
				state.vertexBuffers[call.index + i] = SlotState { SlotState::buffer, buffers[i], offsets[i] };
			}
			break;
		case Call::FragmentBuffers:
			for (NSUInteger i = 0; i < call.length; ++i) {
				buffers[i] = object; offsets[i] = call.offset + i;
			}
			for (NSUInteger i = 0; i < call.length; ++i) {
				EncoderState::grow(state.fragmentBuffers, call.index + i);
				state.fragmentBuffers[call.index + i] = SlotState { SlotState::buffer, buffers[i], offsets[i] };
			}
			break;
		case Call::Pipeline:
			state.pipeline = object;
			break;
		case Call::DepthStencil:
			state.depthStencil = object;
			break;
	}
}

// The wrapper's decision: does the call reach Indium? Transcribed from the shipping
// methods (line numbers are those of MTLRenderCommandEncoder.mm) and calling the
// extracted tracking helpers from bind-tracking.extract.inc.
//
// `keySource` selects what a record is compared against. The shipping code uses Indium
// (MTLBindKey(MTLXGetIndium(...)), the resource's inner pointer); Wrapper is the round-1
// key the ABA scenario shows failing (the Objective-C address the caller passed in).
enum class BindKeySource {
	Indium,
	Wrapper,
};

struct TrackedEncoder {
	MTLBindObjectSlots vertexTextures;
	MTLBindObjectSlots fragmentTextures;
	MTLBindObjectSlots vertexSamplers;
	MTLBindObjectSlots fragmentSamplers;
	MTLBindObjectSlots pipelineState;
	MTLBindObjectSlots depthStencilState;
	MTLBindBufferSlots vertexBuffers;
	MTLBindBufferSlots fragmentBuffers;

	BindKeySource keySource = BindKeySource::Indium;
	// what the shipping setDepthStencilState: stores in _depthStencilOwner (the depth-stencil record
	// is the one record whose resource Indium itself does not keep alive)
	const void* depthStencilOwner = nullptr;

	std::map<std::string, unsigned long> drops;
	void drop(const char* what) { ++drops[what]; }

	const void* key(const Call& call) const {
		return keySource == BindKeySource::Wrapper ? call.wrapper : call.object;
	}

	// endEncoding (MTLBindForgetAll + _depthStencilOwner.reset()): records do not survive the pass
	void clear() {
		MTLBindForgetAll(vertexTextures);
		MTLBindForgetAll(fragmentTextures);
		MTLBindForgetAll(vertexSamplers);
		MTLBindForgetAll(fragmentSamplers);
		MTLBindForgetAll(pipelineState);
		MTLBindForgetAll(depthStencilState);
		MTLBindForgetAll(vertexBuffers);
		MTLBindForgetAll(fragmentBuffers);
		depthStencilOwner = nullptr;
	}

	bool decide(const Call& call) {
		switch (call.kind) {
			case Call::VertexTexture:            // :989
				if (MTLBindObjectSlot(vertexTextures, call.index, key(call))) { drop("VertexTexture"); return false; }
				return true;
			case Call::FragmentTexture:          // :994
				if (MTLBindObjectSlot(fragmentTextures, call.index, key(call))) { drop("FragmentTexture"); return false; }
				return true;
			case Call::VertexSampler:            // :1000
				if (MTLBindObjectSlot(vertexSamplers, call.index, key(call))) { drop("VertexSamplerState"); return false; }
				return true;
			case Call::FragmentSampler:          // :1005
				if (MTLBindObjectSlot(fragmentSamplers, call.index, key(call))) { drop("FragmentSamplerState"); return false; }
				return true;
			case Call::DepthStencil:             // :1015: record, then retain the state in the record
				if (MTLBindObjectSlot(depthStencilState, 0, key(call))) { drop("DepthStencilState"); return false; }
				depthStencilOwner = call.object;
				return true;
			case Call::Pipeline:                 // :1052
				if (MTLBindObjectSlot(pipelineState, 0, key(call))) { drop("PipelineState"); return false; }
				return true;
			case Call::VertexBuffer:             // :1116
				if (MTLBindBufferSlot(vertexBuffers, call.index, key(call), call.offset)) { drop("VertexBuffer"); return false; }
				return true;
			case Call::FragmentBuffer:           // :1191
				if (MTLBindBufferSlot(fragmentBuffers, call.index, key(call), call.offset)) { drop("FragmentBuffer"); return false; }
				return true;
			case Call::VertexBufferOffset:       // :1162
				if (MTLBindBufferOffset(vertexBuffers, call.index, call.offset)) { drop("VertexBufferOffset"); return false; }
				return true;
			case Call::FragmentBufferOffset:     // :1219
				if (MTLBindBufferOffset(fragmentBuffers, call.index, call.offset)) { drop("FragmentBufferOffset"); return false; }
				return true;
			case Call::VertexBytes:              // :1181
				MTLBindForgetBufferSlot(vertexBuffers, call.index);
				return true;
			case Call::FragmentBytes:            // :1230
				MTLBindForgetBufferSlot(fragmentBuffers, call.index);
				return true;
			case Call::VertexBuffers:            // :1155: plurals always forward, and record every slot
				for (NSUInteger i = 0; i < call.length; ++i)
					MTLBindBufferSlot(vertexBuffers, call.index + i, call.object, call.offset + i);
				return true;
			case Call::FragmentBuffers:          // :1213
				for (NSUInteger i = 0; i < call.length; ++i)
					MTLBindBufferSlot(fragmentBuffers, call.index + i, call.object, call.offset + i);
				return true;
		}
		return true;
	}
};

// ------------------------------------------------------------------ the harness

struct Harness {
	EncoderState all;          // every call applied
	EncoderState forwarded;    // only the forwarded calls applied
	TrackedEncoder tracked;
	unsigned long calls = 0, forwardedCalls = 0, dropped = 0;

	// The ABA scenario drives a sequence in which a divergence is the *result* under test (the round-1
	// key is expected to produce one), so it can turn the CHECK off and count them instead.
	bool expectDivergence = false;
	unsigned long divergences = 0;

	void run(const Call& call) {
		bool forward = tracked.decide(call);
		apply(call, all);
		if (forward) { apply(call, forwarded); ++forwardedCalls; } else ++dropped;
		++calls;
		// the game only reads binding state at a draw, so it is checked after every call: stricter
		if (!equal(all, forwarded)) {
			++divergences;
			if (!expectDivergence)
				CHECK(false, "binding state diverged after call %lu (kind %d, slot %lu)", calls, (int) call.kind, (unsigned long) call.index);
			all = forwarded;   // report once per divergence
		}
	}
};

int main() {
	static int objects[16];
	const void* o[16];
	for (int i = 0; i < 16; ++i) o[i] = &objects[i];
	const void* bytes = "inline-bytes";

	struct Scenario { const char* name; std::vector<Call> calls; unsigned long minDrops; };
	std::vector<Scenario> scenarios;

	{	// 1. a run of draws with identical bindings: everything after the first set is redundant
		std::vector<Call> calls;
		for (int draw = 0; draw < 8; ++draw) {
			calls.push_back({ Call::Pipeline, o[0] });
			calls.push_back({ Call::VertexBuffer, o[1], 0, 3 });
			calls.push_back({ Call::FragmentBuffer, o[2], 64, 4 });
			calls.push_back({ Call::FragmentTexture, o[3], 0, 0 });
			calls.push_back({ Call::FragmentTexture, o[4], 0, 1 });
			calls.push_back({ Call::FragmentSampler, o[5], 0, 0 });
		}
		scenarios.push_back({ "redundant-binds", calls, 8 * 6 - 6 });
	}
	{	// 2. rebinding after a pipeline change: the slot state is unchanged, so the bind is still redundant
		std::vector<Call> calls = {
			{ Call::FragmentTexture, o[3], 0, 0 },
			{ Call::Pipeline, o[0] },
			{ Call::FragmentTexture, o[3], 0, 0 },
			{ Call::Pipeline, o[0] },
			{ Call::Pipeline, o[1] },
			{ Call::Pipeline, o[1] },
			{ Call::FragmentTexture, o[4], 0, 0 },
			{ Call::FragmentTexture, o[4], 0, 0 },
			{ Call::DepthStencil, o[8] },
			{ Call::DepthStencil, o[8] },
		};
		scenarios.push_back({ "rebind-after-pipeline-change", calls, 5 });
	}
	{	// 3. inline bytes replace the slot's buffer: the next bind must go through, the one after it must not
		std::vector<Call> calls = {
			{ Call::VertexBuffer, o[1], 0, 3 },
			{ Call::VertexBytes, bytes, 0, 3 },
			{ Call::VertexBuffer, o[1], 0, 3 },
			{ Call::VertexBuffer, o[1], 0, 3 },
			{ Call::VertexBytes, bytes, 0, 3 },
			{ Call::VertexBuffer, o[1], 0, 3 },
		};
		scenarios.push_back({ "bytes-then-buffer", calls, 1 });
	}
	{	// 4. the offset belongs to the slot state
		std::vector<Call> calls = {
			{ Call::VertexBuffer, o[1], 0, 4 },
			{ Call::VertexBufferOffset, nullptr, 1024, 4 },
			{ Call::VertexBufferOffset, nullptr, 1024, 4 },
			{ Call::VertexBuffer, o[1], 0, 4 },
			{ Call::VertexBuffer, o[1], 1024, 4 },
			{ Call::VertexBufferOffset, nullptr, 1024, 4 },
			{ Call::VertexBufferOffset, nullptr, 4096, 4 },
			{ Call::VertexBuffer, o[1], 1024, 4 },
			{ Call::VertexBufferOffset, nullptr, 4096, 4 },
			{ Call::VertexBufferOffset, nullptr, 4096, 5 },   // never bound: Indium logs and changes nothing
			{ Call::VertexBuffer, o[1], 4096, 5 },
			{ Call::VertexBuffer, o[1], 4096, 5 },
		};
		scenarios.push_back({ "buffer-offset-state", calls, 3 });
	}
	{	// 5. nil is a value: nil over nil is redundant, a real texture over nil is not
		std::vector<Call> calls = {
			{ Call::FragmentTexture, nullptr, 0, 2 },
			{ Call::FragmentTexture, nullptr, 0, 2 },
			{ Call::FragmentTexture, o[3], 0, 2 },
			{ Call::FragmentTexture, nullptr, 0, 2 },
			{ Call::FragmentTexture, nullptr, 0, 2 },
			{ Call::FragmentBuffer, nullptr, 0, 6 },
			{ Call::FragmentBuffer, nullptr, 0, 6 },
			{ Call::FragmentBuffer, o[2], 0, 6 },
		};
		scenarios.push_back({ "nil-slots", calls, 3 });
	}
	{	// 6. out-of-range slots are never tracked (the tables are 31 entries, like the rest of the file)
		std::vector<Call> calls;
		for (int i = 0; i < 4; ++i) calls.push_back({ Call::FragmentTexture, o[3], 0, 31 });
		for (int i = 0; i < 4; ++i) calls.push_back({ Call::VertexBuffer, o[1], 0, 200 });
		calls.push_back({ Call::VertexBufferOffset, nullptr, 8, 200 });
		scenarios.push_back({ "out-of-range", calls, 0 });
	}
	{	// 7. plural setters record every slot they cover
		std::vector<Call> calls = {
			{ Call::VertexBuffers, o[1], 100, 2, 3 },
			{ Call::VertexBuffer, o[1], 100, 2 },
			{ Call::VertexBuffer, o[1], 100, 3 },
			{ Call::VertexBuffer, o[9], 100, 4 },
			{ Call::VertexBuffer, o[9], 100, 4 },
			{ Call::FragmentBuffers, o[2], 0, 0, 4 },
			{ Call::FragmentBuffer, o[2], 0, 0 },
			{ Call::FragmentBuffer, o[2], 0, 3 },
		};
		scenarios.push_back({ "plural-setters", calls, 3 });
	}
	{	// 9. what a material batch looks like: one state setup, then the same draws re-binding it
		std::vector<Call> calls;
		for (int draw = 0; draw < 16; ++draw) {
			calls.push_back({ Call::Pipeline, o[0] });
			calls.push_back({ Call::VertexBuffer, o[1], 128, 3 });
			calls.push_back({ Call::VertexBuffer, o[6], 0, 4 });
			calls.push_back({ Call::FragmentBuffer, o[2], 64, 0 });
			calls.push_back({ Call::FragmentTexture, o[3], 0, 0 });
			calls.push_back({ Call::FragmentSampler, o[5], 0, 0 });
		}
		scenarios.push_back({ "batches-sharing-a-material", calls, 15 * 6 - 6 });
	}
	{	// 10. two materials alternating, differing only in one texture: the shared slots still drop
		std::vector<Call> calls;
		for (int draw = 0; draw < 16; ++draw) {
			calls.push_back({ Call::Pipeline, o[0] });
			calls.push_back({ Call::VertexBuffer, o[1], 128, 3 });
			calls.push_back({ Call::FragmentBuffer, o[2], 64, 0 });
			calls.push_back({ Call::FragmentTexture, (draw % 2) ? o[3] : o[4], 0, 0 });
			calls.push_back({ Call::FragmentSampler, o[5], 0, 0 });
		}
		scenarios.push_back({ "two-materials-alternating", calls, 12 });
	}

	{	// 8. interleaved stages, slots and indices keep separate records
		std::vector<Call> calls = {
			{ Call::VertexTexture, o[7], 0, 0 },
			{ Call::FragmentTexture, o[7], 0, 0 },
			{ Call::VertexTexture, o[7], 0, 0 },
			{ Call::FragmentTexture, o[7], 0, 0 },
			{ Call::VertexTexture, o[7], 0, 1 },
			{ Call::FragmentTexture, o[7], 0, 1 },
			{ Call::VertexTexture, o[7], 0, 1 },
			{ Call::VertexTexture, o[7], 0, 0 },
			{ Call::VertexTexture, o[7], 0, 0 },
		};
		scenarios.push_back({ "separate-records", calls, 4 });
	}

	for (auto& scenario: scenarios) {
		Harness harness;
		for (const auto& call: scenario.calls) harness.run(call);
		CHECK(harness.dropped >= scenario.minDrops, "%s: %lu dropped (expected at least %lu)",
			scenario.name, harness.dropped, scenario.minDrops);
		CHECK(harness.forwardedCalls + harness.dropped == scenario.calls.size(), "%s: %lu + %lu != %zu",
			scenario.name, harness.forwardedCalls, harness.dropped, scenario.calls.size());
		std::printf("scenario %-30s calls=%3zu forwarded=%3lu dropped=%3lu\n",
			scenario.name, scenario.calls.size(), harness.forwardedCalls, harness.dropped);
	}

	{	// 11. endEncoding clears the records (MTLBindForgetAll), so a later bind is forwarded again
		Harness harness;
		Call bind;
		bind.kind = Call::FragmentTexture;
		bind.object = o[3];
		bind.index = 0;
		harness.run(bind);
		CHECK(harness.forwardedCalls == 1 && harness.dropped == 0, "endEncoding-clear: first bind forwarded");
		harness.tracked.clear();
		harness.run(bind);
		CHECK(harness.forwardedCalls == 2, "endEncoding-clear: the bind after the clear must be forwarded");
		std::printf("scenario %-30s calls=%3lu forwarded=%3lu dropped=%3lu\n",
			"endEncoding-clear", harness.calls, harness.forwardedCalls, harness.dropped);
	}

	// ------------------------------------------------- ABA: a released address comes back
	//
	// The game binds a resource, releases its wrapper, and the allocator hands the wrapper's address to
	// a new wrapper around a different Indium resource (transient per-draw buffers do exactly that).
	// The model makes the two address spaces explicit:
	//
	//  * the wrapper space is malloc-like: a released wrapper address is handed out again;
	//  * the Indium space only reuses an address once the object there is dead, and the objects the
	//    encoder holds are not dead: `forwarded` (what actually reached Indium) is the holder, the way
	//    FunctionResources keeps bound textures/buffers/samplers and _privatePSO keeps the pipeline
	//    (REPORT.md round 2, "lifetime per kind"). Depth-stencil is the exception -- Indium's setter
	//    keeps no reference -- so the shipping record retains it (_depthStencilOwner), modelled by
	//    `tracked.depthStencilOwner`; `retainDepthStencil` turns that on or off.
	//
	// Both keys run the same sequence. The round-1 key (the wrapper address) cannot tell the new
	// object from the old one at the reused address, so it drops the bind and the encoder-state
	// diverges from the game's (the bug). The shipping round-2 key (the Indium address) sees that the
	// old resource's address cannot have come back while the encoder holds it, so it forwards.
	struct AbaResult {
		unsigned long dropped = 0;
		unsigned long divergences = 0;
		bool addressReused = false;      // the wrapper address really came back (malloc behaviour)
		bool identitiesDistinct = false; // the Indium addresses differ (nothing held the old one)
	};

	// what the Indium encoder keeps alive after `state` was forwarded: textures, buffers, samplers and
	// the pipeline, but not the depth-stencil state (see the citation above)
	static const auto heldByEncoder = [](const EncoderState& state, bool withDepthStencil) {
		std::vector<const void*> held;
		for (const void* identity: state.vertexTextures) if (identity) held.push_back(identity);
		for (const void* identity: state.fragmentTextures) if (identity) held.push_back(identity);
		for (const void* identity: state.vertexSamplers) if (identity) held.push_back(identity);
		for (const void* identity: state.fragmentSamplers) if (identity) held.push_back(identity);
		for (const SlotState& slot: state.vertexBuffers) if (slot.kind == SlotState::buffer && slot.object) held.push_back(slot.object);
		for (const SlotState& slot: state.fragmentBuffers) if (slot.kind == SlotState::buffer && slot.object) held.push_back(slot.object);
		if (state.pipeline) held.push_back(state.pipeline);
		if (withDepthStencil && state.depthStencil) held.push_back(state.depthStencil);
		return held;
	};

	// `kind` is FragmentTexture or DepthStencil; `retainDepthStencil` models _depthStencilOwner
	static const auto runAba = [](BindKeySource keySource, Call::Kind kind, bool retainDepthStencil) {
		static const char wrapperAddresses[4] = {};
		static const char indiumAddresses[4] = {};
		static const char identities[8] = {};   // the objects themselves, independent of any address
		std::vector<const void*> wrapperHeap, indiumHeap;
		for (int i = 0; i < 4; ++i) { wrapperHeap.push_back(&wrapperAddresses[i]); indiumHeap.push_back(&indiumAddresses[i]); }

		AbaResult result;
		Harness harness;
		harness.tracked.keySource = keySource;
		harness.expectDivergence = true;   // the round-1 key is expected to diverge here

		// X: a new object (wrapper + Indium resource), bound
		const void* xId = &identities[0];
		const void* xIndium = indiumHeap.back(); indiumHeap.pop_back();
		const void* xWrapper = wrapperHeap.back(); wrapperHeap.pop_back();
		Call bindX;
		bindX.kind = kind;
		bindX.object = xIndium;
		bindX.wrapper = xWrapper;
		bindX.id = xId;
		harness.run(bindX);

		// The game releases X's wrapper (malloc gets its address back). The Indium object dies as soon
		// as nothing holds it: the encoder state holds textures/buffers/samplers/pipeline, the record's
		// retained reference holds the depth-stencil state; only a dead object's address returns to the
		// allocator, which is exactly why a recorded Indium address cannot come back around.
		wrapperHeap.push_back(xWrapper);
		bool xHeld = kind == Call::DepthStencil && retainDepthStencil && harness.tracked.depthStencilOwner == xIndium;
		for (const void* id: heldByEncoder(harness.forwarded, kind == Call::DepthStencil && retainDepthStencil))
			xHeld = xHeld || id == xId;
		if (!xHeld) indiumHeap.push_back(xIndium);

		// Y is allocated: malloc hands the wrapper address back, and the Indium resource takes the
		// address the allocator has free (X's, if X was dead)
		const void* yId = &identities[1];
		const void* yIndium = indiumHeap.back(); indiumHeap.pop_back();
		const void* yWrapper = wrapperHeap.back(); wrapperHeap.pop_back();
		CHECK(yWrapper == xWrapper, "aba: the wrapper allocator must reuse the released address");
		result.addressReused = (yWrapper == xWrapper);
		result.identitiesDistinct = (yIndium != xIndium);

		Call bindY;
		bindY.kind = kind;
		bindY.object = yIndium;
		bindY.wrapper = yWrapper;
		bindY.id = yId;
		harness.run(bindY);

		result.dropped = harness.dropped;
		result.divergences = harness.divergences;
		return result;
	};

	// the shipping key: the same address cannot come back while the encoder holds the resource, so the
	// new bind is forwarded and nothing diverges
	static const auto abaRejects = [](const AbaResult& result) {
		CHECK(result.dropped == 0, "aba (indium key): %lu binds dropped", result.dropped);
		CHECK(result.divergences == 0, "aba (indium key): %lu divergences", result.divergences);
		CHECK(result.identitiesDistinct, "aba (indium key): the new resource must not reuse the held address");
	};
	{
		AbaResult texture = runAba(BindKeySource::Indium, Call::FragmentTexture, false);
		CHECK(texture.addressReused, "aba: the wrapper address must be reused");
		abaRejects(texture);
		AbaResult vertexBuffer = runAba(BindKeySource::Indium, Call::VertexBuffer, false);
		CHECK(vertexBuffer.addressReused, "aba: the wrapper address must be reused");
		abaRejects(vertexBuffer);
		AbaResult depthStencil = runAba(BindKeySource::Indium, Call::DepthStencil, true);
		CHECK(depthStencil.addressReused, "aba: the wrapper address must be reused");
		abaRejects(depthStencil);

		// the round-1 key is what this fixes: at the reused wrapper address it drops Y's bind, the
		// forwarded state keeps X, and the two diverge (one on the Y bind, then the resync)
		AbaResult round1Texture = runAba(BindKeySource::Wrapper, Call::FragmentTexture, false);
		CHECK(round1Texture.dropped >= 1 && round1Texture.divergences >= 1,
			"aba (round-1 key): expected the reused-address bind to be dropped and to diverge (dropped=%lu divergences=%lu)",
			round1Texture.dropped, round1Texture.divergences);
		AbaResult round1VertexBuffer = runAba(BindKeySource::Wrapper, Call::VertexBuffer, false);
		CHECK(round1VertexBuffer.dropped >= 1 && round1VertexBuffer.divergences >= 1,
			"aba (round-1 key, buffer): expected the same failure (dropped=%lu divergences=%lu)",
			round1VertexBuffer.dropped, round1VertexBuffer.divergences);
		// retaining the depth-stencil state does not save the wrapper key: the wrapper address is still
		// free to be reused, because the record holds the Indium object, not the wrapper
		AbaResult round1DepthStencil = runAba(BindKeySource::Wrapper, Call::DepthStencil, true);
		CHECK(round1DepthStencil.dropped >= 1 && round1DepthStencil.divergences >= 1,
			"aba (round-1 key, depth-stencil): expected the same failure (dropped=%lu divergences=%lu)",
			round1DepthStencil.dropped, round1DepthStencil.divergences);
		// and without the retained record the Indium key would fail the same way, which is why the
		// shipping setDepthStencilState: keeps _depthStencilOwner
		AbaResult noRetain = runAba(BindKeySource::Indium, Call::DepthStencil, false);
		CHECK(!noRetain.identitiesDistinct, "aba (indium key, no retention): the freed address must be reusable");
		CHECK(noRetain.dropped >= 1 && noRetain.divergences >= 1,
			"aba (indium key, no retention): expected the failure the retained record prevents (dropped=%lu divergences=%lu)",
			noRetain.dropped, noRetain.divergences);

		std::printf("aba texture        indium key: dropped=%lu divergences=%lu | round-1 key: dropped=%lu divergences=%lu\n",
			texture.dropped, texture.divergences, round1Texture.dropped, round1Texture.divergences);
		std::printf("aba vertex buffer  indium key: dropped=%lu divergences=%lu | round-1 key: dropped=%lu divergences=%lu\n",
			vertexBuffer.dropped, vertexBuffer.divergences, round1VertexBuffer.dropped, round1VertexBuffer.divergences);
		std::printf("aba depth-stencil  indium key + retained record: dropped=%lu divergences=%lu | without retention: dropped=%lu divergences=%lu\n",
			depthStencil.dropped, depthStencil.divergences, noRetain.dropped, noRetain.divergences);
	}

	// ------------------------------------------------------------- randomized runs
	std::mt19937_64 rng(20261014);
	unsigned long totalDropped = 0, totalCalls = 0;
	for (int round = 0; round < 200; ++round) {
		Harness harness;
		const int callsPerRound = 400 + (int) (rng() % 400);
		for (int i = 0; i < callsPerRound; ++i) {
			Call call;
			call.kind = static_cast<Call::Kind>(rng() % 14);
			call.object = (rng() % 8 == 0) ? nullptr : o[rng() % 6];
			call.offset = (rng() % 4) * 256;
			call.index = (rng() % 40 == 0) ? (NSUInteger) (31 + rng() % 8) : (NSUInteger) (rng() % 5);
			call.length = 1 + rng() % 3;
			if (call.kind == Call::VertexBytes || call.kind == Call::FragmentBytes) call.object = bytes;
			if (call.kind == Call::VertexBuffers || call.kind == Call::FragmentBuffers) {
				call.index = rng() % 4;
				call.length = 1 + rng() % 3;
			}
			harness.run(call);
		}
		totalDropped += harness.dropped;
		totalCalls += harness.calls;
	}
	std::printf("randomized calls=%lu forwarded=%lu dropped=%lu (%.1f%%)\n",
		totalCalls, totalCalls - totalDropped, totalDropped, 100.0 * totalDropped / totalCalls);
	std::printf("BIND_DEDUP_MODEL failures=%d\n", failures);
	return failures != 0;
}
