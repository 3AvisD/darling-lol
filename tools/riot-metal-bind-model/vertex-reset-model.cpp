// Host model of the MTLVertexDescriptor reset marks (Metal wrapper, MTLVertexDescriptor.mm).
//
// The wrapper now writes the defaults only into the slots whose values may differ from them:
// every item carries the mask word and bit of the descriptor slot it fills, its setters mark that
// slot, and the -setObject:/copy paths pin the slots whose items cannot be tracked. Over-marking
// is the only safe direction, so the invariant this model checks after every operation is:
//   * the values of the descriptor's items after reset are the defaults (like the pre-change
//     "reset everything" behaviour), and
//   * a slot whose item holds non-default values is marked (dirty or pinned).
// Two worlds are driven with the same operations: `shipped` runs the shipping helpers (extracted
// verbatim into vertex-reset.extract.inc) and `reference` uses the pre-change reset (every present
// item). Any missed mark makes the two worlds' values differ after a reset, which is counted as a
// divergence and printed.
//
// Compile with: c++ -std=c++17 -O2 -Wall -Wextra -o vertex-reset-model vertex-reset-model.cpp
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------
// The model world: the same shapes as the wrapper's classes (ivar names match the shipping code),
// plus one flag the wrapper does not have (`_subclass`: an item a caller installed that is not the
// bridge class, i.e. an object whose setters this file does not control).
// ---------------------------------------------------------------------------------------------
using NSUInteger = unsigned long;
#define nil nullptr   // the shipping helpers assign nil to mean "no owner/descriptor any more"
enum { MTLVertexFormatInvalid = 0, MTLVertexStepFunctionPerVertex = 1, MTLVertexStepFunctionPerInstance = 2 };
enum { MTLVertexFormatFloat4 = 31 };   // the game's setup fills Float4 into every attribute

// defined by the extracted shipping helpers (vertex-reset.extract.inc, included below)
static inline void MTLVertexDescriptorItemTouched(uint32_t* word, uint32_t bit);

struct MTLVertexDescriptor;

struct MTLVertexBufferLayoutDescriptor {
	// ivars, in the shipping order (MTLVertexDescriptor.mm, MTLVertexBufferLayoutDescriptor ())
	NSUInteger _stride = 0;
	NSUInteger _stepFunction = MTLVertexStepFunctionPerVertex;
	NSUInteger _stepRate = 1;
	uint32_t* _dirtyWord = nullptr;
	uint32_t _dirtyBit = 0;
	bool _subclass = false;              // model only: setters do not mark (the wrapper cannot see them)

	// the shipping setters: they mark the slot (MTLVertexDescriptor.mm). The model adds the
	// _subclass case: an item a caller installed is not the bridge class, so its own setters are
	// the ones that run and they may not mark at all - that is what the pinned mask is for.
	void setStride(NSUInteger v) { if (!_subclass) MTLVertexDescriptorItemTouched(_dirtyWord, _dirtyBit); _stride = v; }
	void setStepFunction(NSUInteger v) { if (!_subclass) MTLVertexDescriptorItemTouched(_dirtyWord, _dirtyBit); _stepFunction = v; }
	void setStepRate(NSUInteger v) { if (!_subclass) MTLVertexDescriptorItemTouched(_dirtyWord, _dirtyBit); _stepRate = v; }
};
struct MTLVertexAttributeDescriptor {
	NSUInteger _format = MTLVertexFormatInvalid;
	NSUInteger _offset = 0;
	NSUInteger _bufferIndex = 0;
	uint32_t* _dirtyWord = nullptr;
	uint32_t _dirtyBit = 0;
	bool _subclass = false;

	void setFormat(NSUInteger v) { if (!_subclass) MTLVertexDescriptorItemTouched(_dirtyWord, _dirtyBit); _format = v; }
	void setOffset(NSUInteger v) { if (!_subclass) MTLVertexDescriptorItemTouched(_dirtyWord, _dirtyBit); _offset = v; }
	void setBufferIndex(NSUInteger v) { if (!_subclass) MTLVertexDescriptorItemTouched(_dirtyWord, _dirtyBit); _bufferIndex = v; }
};

struct MTLVertexBufferLayoutDescriptorArray {
	MTLVertexBufferLayoutDescriptor* _items[31] = {};
	uint32_t _present = 0;
	uint32_t _concrete = 0;
	MTLVertexDescriptor* _owner = nullptr;
};
struct MTLVertexAttributeDescriptorArray {
	MTLVertexAttributeDescriptor* _items[31] = {};
	uint32_t _present = 0;
	uint32_t _concrete = 0;
	MTLVertexDescriptor* _owner = nullptr;
};
struct MTLVertexDescriptor {
	MTLVertexBufferLayoutDescriptorArray* _layouts = nullptr;
	MTLVertexAttributeDescriptorArray* _attributes = nullptr;
	uint32_t _attrDirty = 0, _attrPinned = 0, _layoutDirty = 0, _layoutPinned = 0;
};

// the hooks the extracted helpers use (the shipping file defines them with object_getClass)
static inline bool MTLVertexBufferLayoutItemIsExactClass(const MTLVertexBufferLayoutDescriptor* item) { return !item->_subclass; }
static inline bool MTLVertexAttributeItemIsExactClass(const MTLVertexAttributeDescriptor* item) { return !item->_subclass; }
// the model has no trace log to write to
static bool MTLVertexDescriptorResetCounters() { return false; }
static void MTLVertexDescriptorResetCount(unsigned long, unsigned long) {}

// the item's own setters (the shipping SetDefaults helpers call the public property setters)
static void MTLVertexBufferLayoutSetDefaults(MTLVertexBufferLayoutDescriptor* item);
static void MTLVertexAttributeSetDefaults(MTLVertexAttributeDescriptor* item);

// the shipping helpers, verbatim (extract-vertex-reset.py)
#include "vertex-reset.extract.inc"

// The shipping MTLVertexBufferLayoutSetDefaults/MTLVertexAttributeSetDefaults (MTLVertexDescriptor.mm)
// write the defaults through the item's own property setters, which is what an item that is not the
// bridge class needs; the same three values here.
static void MTLVertexBufferLayoutSetDefaults(MTLVertexBufferLayoutDescriptor* item)
{
	item->setStride(0); item->setStepFunction(MTLVertexStepFunctionPerVertex); item->setStepRate(1);
}
static void MTLVertexAttributeSetDefaults(MTLVertexAttributeDescriptor* item)
{
	item->setFormat(MTLVertexFormatInvalid); item->setOffset(0); item->setBufferIndex(0);
}

// ---------------------------------------------------------------------------------------------
// World: a descriptor plus the items the driver holds on to (to graft them, as the game may hold
// an item across a reset and install it elsewhere), and a write counter so a scenario can report
// how many items a reset touched.
// ---------------------------------------------------------------------------------------------
struct World {
	MTLVertexDescriptor* d = nullptr;
	std::vector<MTLVertexBufferLayoutDescriptor*> heldLayouts;
	std::vector<MTLVertexAttributeDescriptor*> heldAttrs;

	void alloc()                         // -reset's fresh path
	{
		d = new MTLVertexDescriptor;
		d->_layouts = new MTLVertexBufferLayoutDescriptorArray;
		d->_attributes = new MTLVertexAttributeDescriptorArray;
		d->_layouts->_owner = d;
		d->_attributes->_owner = d;
		d->_attrDirty = d->_attrPinned = d->_layoutDirty = d->_layoutPinned = 0;
	}

	// the model's -objectAtIndexedSubscript: (creation; a fresh item is at the defaults, so it is
	// not marked, but it knows its slot)
	MTLVertexBufferLayoutDescriptor* layoutAt(NSUInteger index)
	{
		MTLVertexBufferLayoutDescriptorArray* a = d->_layouts;
		if (!a->_items[index]) {
			a->_items[index] = new MTLVertexBufferLayoutDescriptor;
			a->_present |= 1u << index;
			a->_concrete |= 1u << index;
			if (a->_owner) { a->_items[index]->_dirtyWord = &a->_owner->_layoutDirty; a->_items[index]->_dirtyBit = 1u << index; }
		}
		return a->_items[index];
	}
	MTLVertexAttributeDescriptor* attrAt(NSUInteger index)
	{
		MTLVertexAttributeDescriptorArray* a = d->_attributes;
		if (!a->_items[index]) {
			a->_items[index] = new MTLVertexAttributeDescriptor;
			a->_present |= 1u << index;
			a->_concrete |= 1u << index;
			if (a->_owner) { a->_items[index]->_dirtyWord = &a->_owner->_attrDirty; a->_items[index]->_dirtyBit = 1u << index; }
		}
		return a->_items[index];
	}

	// the tracking half of -setObject:atIndexedSubscript: / removal with nil
	void setLayout(NSUInteger index, MTLVertexBufferLayoutDescriptor* item)
	{
		MTLVertexBufferLayoutDescriptorArray* a = d->_layouts;
		const uint32_t bit = 1u << index;
		MTLVertexBufferLayoutDescriptor* old = a->_items[index];
		a->_items[index] = item;
		MTLVertexBufferLayoutArrayRetireItem(a, old);   // -setObject: retires the replaced item
		if (item) a->_present |= bit; else a->_present &= ~bit;
		if (item && !item->_subclass) a->_concrete |= bit; else a->_concrete &= ~bit;
		if (a->_owner) {
			if (!item) {
				a->_owner->_layoutDirty &= ~bit;
				a->_owner->_layoutPinned &= ~bit;
			} else {
				const int tracking = MTLVertexBufferLayoutItemIsExactClass(item)
					? MTLVertexSlotTrackingFor(item->_dirtyWord, item->_dirtyBit, &a->_owner->_layoutDirty, bit)
					: 2;
				if (tracking == 1) a->_owner->_layoutDirty |= bit; else a->_owner->_layoutPinned |= bit;
			}
		}
	}
	void setAttr(NSUInteger index, MTLVertexAttributeDescriptor* item)
	{
		MTLVertexAttributeDescriptorArray* a = d->_attributes;
		const uint32_t bit = 1u << index;
		MTLVertexAttributeDescriptor* old = a->_items[index];
		a->_items[index] = item;
		MTLVertexAttributeArrayRetireItem(a, old);      // -setObject: retires the replaced item
		if (item) a->_present |= bit; else a->_present &= ~bit;
		if (item && !item->_subclass) a->_concrete |= bit; else a->_concrete &= ~bit;
		if (a->_owner) {
			if (!item) {
				a->_owner->_attrDirty &= ~bit;
				a->_owner->_attrPinned &= ~bit;
			} else {
				const int tracking = MTLVertexAttributeItemIsExactClass(item)
					? MTLVertexSlotTrackingFor(item->_dirtyWord, item->_dirtyBit, &a->_owner->_attrDirty, bit)
					: 2;
				if (tracking == 1) a->_owner->_attrDirty |= bit; else a->_owner->_attrPinned |= bit;
			}
		}
	}

	// the shipping reset (marked path); the fresh path is alloc()
	void reset() { if (d->_layouts && d->_attributes) MTLVertexDescriptorResetMarks(d); }
	// -dealloc: revoke the items that point at this descriptor's masks
	void revoke() { MTLVertexBufferLayoutArrayRevokeItems(d->_layouts, &d->_layoutDirty); MTLVertexAttributeArrayRevokeItems(d->_attributes, &d->_attrDirty); }

	// -copyWithZone: the arrays are copied (new items), then the copy binds its arrays and pins
	// every present slot: nothing tracks the copies
	World copy() const
	{
		World c;
		c.d = new MTLVertexDescriptor;
		MTLVertexBufferLayoutDescriptorArray* layouts = new MTLVertexBufferLayoutDescriptorArray;
		MTLVertexAttributeDescriptorArray* attributes = new MTLVertexAttributeDescriptorArray;
		for (int i = 0; i < 31; ++i) {
			if (d->_layouts->_items[i]) layouts->_items[i] = new MTLVertexBufferLayoutDescriptor(*d->_layouts->_items[i]);
			if (d->_attributes->_items[i]) attributes->_items[i] = new MTLVertexAttributeDescriptor(*d->_attributes->_items[i]);
			if (layouts->_items[i]) { layouts->_items[i]->_dirtyWord = nullptr; layouts->_items[i]->_dirtyBit = 0; }
			if (attributes->_items[i]) { attributes->_items[i]->_dirtyWord = nullptr; attributes->_items[i]->_dirtyBit = 0; }
		}
		layouts->_present = d->_layouts->_present;
		layouts->_concrete = d->_layouts->_concrete;
		attributes->_present = d->_attributes->_present;
		attributes->_concrete = d->_attributes->_concrete;
		c.d->_layouts = layouts;
		c.d->_attributes = attributes;
		layouts->_owner = c.d;
		attributes->_owner = c.d;
		c.d->_layoutPinned = layouts->_present;
		c.d->_attrPinned = attributes->_present;
		return c;
	}
};

// reference implementation: the pre-change reset, i.e. write the defaults into every present item
static void ReferenceReset(MTLVertexDescriptor* d)
{
	for (uint32_t bits = d->_layouts->_present; bits; bits &= bits - 1) {
		const int i = __builtin_ctz(bits);
		MTLVertexBufferLayoutDescriptor* item = d->_layouts->_items[i];
		if (!item) continue;
		item->_stride = 0; item->_stepFunction = MTLVertexStepFunctionPerVertex; item->_stepRate = 1;
	}
	for (uint32_t bits = d->_attributes->_present; bits; bits &= bits - 1) {
		const int i = __builtin_ctz(bits);
		MTLVertexAttributeDescriptor* item = d->_attributes->_items[i];
		if (!item) continue;
		item->_format = MTLVertexFormatInvalid; item->_offset = 0; item->_bufferIndex = 0;
	}
}

static bool IsDefault(const MTLVertexBufferLayoutDescriptor* item)
{
	return item->_stride == 0 && item->_stepFunction == MTLVertexStepFunctionPerVertex && item->_stepRate == 1;
}
static bool IsDefault(const MTLVertexAttributeDescriptor* item)
{
	return item->_format == MTLVertexFormatInvalid && item->_offset == 0 && item->_bufferIndex == 0;
}

// every present slot whose item is not at the defaults must be marked (dirty or pinned); a slot
// holding a subclass instance must be pinned, because its setters cannot be seen
static bool Invariant(const World& w, std::string& why)
{
	for (uint32_t bits = w.d->_layouts->_present; bits; bits &= bits - 1) {
		const int i = __builtin_ctz(bits);
		MTLVertexBufferLayoutDescriptor* item = w.d->_layouts->_items[i];
		if (!item) continue;
		const uint32_t bit = 1u << i;
		const bool marked = (w.d->_layoutDirty | w.d->_layoutPinned) & bit;
		if (item->_subclass && !(w.d->_layoutPinned & bit)) { why = "subclass layout slot not pinned"; return false; }
		if (!IsDefault(item) && !marked) { why = "layout slot " + std::to_string(i) + " not marked"; return false; }
	}
	for (uint32_t bits = w.d->_attributes->_present; bits; bits &= bits - 1) {
		const int i = __builtin_ctz(bits);
		MTLVertexAttributeDescriptor* item = w.d->_attributes->_items[i];
		if (!item) continue;
		const uint32_t bit = 1u << i;
		const bool marked = (w.d->_attrDirty | w.d->_attrPinned) & bit;
		if (item->_subclass && !(w.d->_attrPinned & bit)) { why = "subclass attribute slot not pinned"; return false; }
		if (!IsDefault(item) && !marked) { why = "attribute slot " + std::to_string(i) + " not marked"; return false; }
	}
	return true;
}

// the observable state: present flags and every item's values
static bool SameValues(const World& a, const World& b, std::string& why)
{
	if (a.d->_layouts->_present != b.d->_layouts->_present) { why = "layout present differs"; return false; }
	if (a.d->_attributes->_present != b.d->_attributes->_present) { why = "attribute present differs"; return false; }
	for (int i = 0; i < 31; ++i) {
		MTLVertexBufferLayoutDescriptor* la = a.d->_layouts->_items[i];
		MTLVertexBufferLayoutDescriptor* lb = b.d->_layouts->_items[i];
		if ((la == nullptr) != (lb == nullptr)) { why = "layout presence differs at " + std::to_string(i); return false; }
		if (la && lb && (la->_stride != lb->_stride || la->_stepFunction != lb->_stepFunction || la->_stepRate != lb->_stepRate)) {
			why = "layout value differs at " + std::to_string(i); return false;
		}
		MTLVertexAttributeDescriptor* aa = a.d->_attributes->_items[i];
		MTLVertexAttributeDescriptor* ab = b.d->_attributes->_items[i];
		if ((aa == nullptr) != (ab == nullptr)) { why = "attribute presence differs at " + std::to_string(i); return false; }
		if (aa && ab && (aa->_format != ab->_format || aa->_offset != ab->_offset || aa->_bufferIndex != ab->_bufferIndex)) {
			why = "attribute value differs at " + std::to_string(i); return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Driver: two worlds (shipped / reference) get the same operations; after every one the values
// must match and the shipped world's marks must cover every non-default slot.
// ---------------------------------------------------------------------------------------------
struct Op {
	enum Kind { Reset, CreateLayout, CreateAttr, ValueLayout, ValueAttr, GraftLayout, GraftAttr,
		GraftSubclassLayout, GraftSubclassAttr, RemoveLayout, RemoveAttr, Copy, RevokeUse } kind;
	int slot = 0;
	int held = 0;
	NSUInteger value = 0;
};

struct Counters {
	unsigned long ops = 0, resets = 0, written = 0, atDefaults = 0, divergences = 0;
};

static unsigned long MarkedSlots(const World& w)
{
	const uint32_t l = (w.d->_layoutDirty | w.d->_layoutPinned) & w.d->_layouts->_present;
	const uint32_t a = (w.d->_attrDirty | w.d->_attrPinned) & w.d->_attributes->_present;
	return __builtin_popcount(l) + __builtin_popcount(a);
}

static std::vector<std::string> g_history;   // the last ops, for a divergence report

static std::string Describe(const Op& op)
{
	static const char* kinds[] = {"reset", "create-layout", "create-attr", "value-layout", "value-attr",
		"graft-layout", "graft-attr", "graft-subclass-layout", "graft-subclass-attr",
		"remove-layout", "remove-attr", "copy", "revoke+use"};
	std::string s = kinds[(int) op.kind];
	if (op.kind == Op::ValueLayout || op.kind == Op::ValueAttr)
		s += " held=" + std::to_string(op.held) + " field=" + std::to_string(op.slot) + " value=" + std::to_string(op.value);
	else if (op.kind == Op::GraftLayout || op.kind == Op::GraftAttr)
		s += std::string(" slot=") + std::to_string(op.slot) + " held=" + std::to_string(op.held);
	else if (op.kind == Op::GraftSubclassLayout || op.kind == Op::GraftSubclassAttr)
		s += std::string(" slot=") + std::to_string(op.slot) + " (caller-made item)";
	else if (op.kind == Op::CreateLayout || op.kind == Op::CreateAttr || op.kind == Op::RemoveLayout || op.kind == Op::RemoveAttr)
		s += " slot=" + std::to_string(op.slot);
	return s;
}

// applies one op to both worlds and checks the invariants; returns false on a divergence
static bool Apply(std::vector<World>& worlds, const Op& op, Counters& c, std::string& why)
{
	g_history.push_back(Describe(op));
	if (g_history.size() > 40) g_history.erase(g_history.begin());
	World& shipped = worlds[0];
	World& reference = worlds[1];
	c.ops++;
	switch (op.kind) {
		case Op::Reset: {
			const unsigned long marked = MarkedSlots(shipped);
			shipped.reset();
			ReferenceReset(reference.d);
			c.resets++;
			c.written += marked;
			c.atDefaults += __builtin_popcount(shipped.d->_layouts->_present) + __builtin_popcount(shipped.d->_attributes->_present) - marked;
			break;
		}
		case Op::CreateLayout:
			shipped.heldLayouts.push_back(shipped.layoutAt(op.slot));
			reference.heldLayouts.push_back(reference.layoutAt(op.slot));
			break;
		case Op::CreateAttr:
			shipped.heldAttrs.push_back(shipped.attrAt(op.slot));
			reference.heldAttrs.push_back(reference.attrAt(op.slot));
			break;
		case Op::ValueLayout: {
			MTLVertexBufferLayoutDescriptor* a = shipped.heldLayouts.at(op.held);
			MTLVertexBufferLayoutDescriptor* b = reference.heldLayouts.at(op.held);
			switch (op.slot & 3) {
				case 0: a->setStride(op.value); b->setStride(op.value); break;
				case 1: a->setStepFunction(op.value); b->setStepFunction(op.value); break;
				default: a->setStepRate(op.value); b->setStepRate(op.value); break;
			}
			break;
		}
		case Op::ValueAttr: {
			MTLVertexAttributeDescriptor* a = shipped.heldAttrs.at(op.held);
			MTLVertexAttributeDescriptor* b = reference.heldAttrs.at(op.held);
			switch (op.slot & 3) {
				case 0: a->setFormat(op.value); b->setFormat(op.value); break;
				case 1: a->setOffset(op.value); b->setOffset(op.value); break;
				default: a->setBufferIndex(op.value); b->setBufferIndex(op.value); break;
			}
			break;
		}
		case Op::GraftLayout:
			shipped.setLayout(op.slot, shipped.heldLayouts.at(op.held));
			reference.setLayout(op.slot, reference.heldLayouts.at(op.held));
			break;
		case Op::GraftAttr:
			shipped.setAttr(op.slot, shipped.heldAttrs.at(op.held));
			reference.setAttr(op.slot, reference.heldAttrs.at(op.held));
			break;
		case Op::GraftSubclassLayout: {
			// a caller-made layout descriptor (not the bridge class): its setters are not ours, so
			// the slot has to be pinned
			shipped.heldLayouts.push_back(new MTLVertexBufferLayoutDescriptor);
			shipped.heldLayouts.back()->_subclass = true;
			reference.heldLayouts.push_back(new MTLVertexBufferLayoutDescriptor);
			reference.heldLayouts.back()->_subclass = true;
			shipped.setLayout(op.slot, shipped.heldLayouts.back());
			reference.setLayout(op.slot, reference.heldLayouts.back());
			break;
		}
		case Op::GraftSubclassAttr: {
			shipped.heldAttrs.push_back(new MTLVertexAttributeDescriptor);
			shipped.heldAttrs.back()->_subclass = true;
			reference.heldAttrs.push_back(new MTLVertexAttributeDescriptor);
			reference.heldAttrs.back()->_subclass = true;
			shipped.setAttr(op.slot, shipped.heldAttrs.back());
			reference.setAttr(op.slot, reference.heldAttrs.back());
			break;
		}
		case Op::RemoveLayout:
			shipped.setLayout(op.slot, nullptr);
			reference.setLayout(op.slot, nullptr);
			break;
		case Op::RemoveAttr:
			shipped.setAttr(op.slot, nullptr);
			reference.setAttr(op.slot, nullptr);
			break;
		case Op::Copy: {
			// -[MTLVertexDescriptor copyWithZone:]: the copy holds new items (nothing tracks them,
			// so the descriptor pins every present slot). The model re-seats its held pointers to the
			// copies' items so later operations can still mutate through a held item. The assign keeps
			// the vector elements - and the references above - in place.
			for (size_t w = 0; w < 2; ++w) {
				World& old = worlds[w];
				World copy = old.copy();
				std::vector<MTLVertexBufferLayoutDescriptor*> heldLayouts;
				std::vector<MTLVertexAttributeDescriptor*> heldAttrs;
				for (auto* item: old.heldLayouts)
					for (int i = 0; i < 31; ++i) if (old.d->_layouts->_items[i] == item && copy.d->_layouts->_items[i]) { heldLayouts.push_back(copy.d->_layouts->_items[i]); break; }
				for (auto* item: old.heldAttrs)
					for (int i = 0; i < 31; ++i) if (old.d->_attributes->_items[i] == item && copy.d->_attributes->_items[i]) { heldAttrs.push_back(copy.d->_attributes->_items[i]); break; }
				copy.heldLayouts = heldLayouts;
				copy.heldAttrs = heldAttrs;
				old = copy;
			}
			break;
		}
		case Op::RevokeUse: {
			// a caller keeps one of the items past the descriptor's death: the revocation (and the
			// retirement an earlier -setObject: did) must have cleared the item's pointer, so a
			// later mutation cannot write into the dead descriptor's storage - modelled as masks
			// reset to zero, which any write would set a bit of
			shipped.revoke();
			reference.revoke();
			shipped.d->_attrDirty = shipped.d->_attrPinned = shipped.d->_layoutDirty = shipped.d->_layoutPinned = 0;
			if (!shipped.heldLayouts.empty()) {
				shipped.heldLayouts[0]->setStride(7);
				reference.heldLayouts[0]->setStride(7);
			}
			if (!shipped.heldAttrs.empty()) {
				shipped.heldAttrs[0]->setFormat(MTLVertexFormatFloat4);
				reference.heldAttrs[0]->setFormat(MTLVertexFormatFloat4);
			}
			if (shipped.d->_attrDirty != 0 || shipped.d->_layoutDirty != 0 || shipped.d->_attrPinned != 0 || shipped.d->_layoutPinned != 0) {
				why = "revoked item still marks the descriptor";
				return false;
			}
			break;
		}
	}
	if (op.kind == Op::RevokeUse) {
		// the descriptor is dead (and its masks are poisoned below): the values of a dead
		// descriptor are not comparable to the reference, only the poison check above matters
		return true;
	}
	std::string reason;
	if (!Invariant(shipped, reason)) {
		why = reason;
		for (const auto& line: g_history) why += "\n    " + line;
		c.divergences++;
		return false;
	}
	if (!SameValues(shipped, reference, reason)) {
		why = reason;
		for (const auto& line: g_history) why += "\n    " + line;
		c.divergences++;
		return false;
	}
	return true;
}

static void Report(const char* name, const Counters& c, bool ok, const std::string& why)
{
	printf("scenario %-34s ops=%6lu resets=%6lu slotsWritten=%7lu slotsAtDefaults=%7lu %s\n",
		name, c.ops, c.resets, c.written, c.atDefaults, ok ? "ok" : ("DIVERGED: " + why).c_str());
}

static bool RunDirected(const char* name, const std::vector<Op>& ops)
{
	std::vector<World> worlds(2);
	for (World& w: worlds) w.alloc();
	Counters c;
	std::string why;
	bool ok = true;
	g_history.clear();
	for (const Op& op: ops) {
		if (!Apply(worlds, op, c, why)) { ok = false; break; }
	}
	Report(name, c, ok, why);
	return ok;
}

int main()
{
	unsigned failures = 0;

	// 1. the game's descriptor: a setup that fills 16 attributes and 16 layouts once, then rebuilds
	//    a few slots per pass. The reset must write only the slots the rebuild touched.
	{
		std::vector<Op> ops;
		for (int i = 0; i < 16; ++i) {
			ops.push_back({Op::CreateAttr, i, 0, MTLVertexFormatFloat4});
			ops.push_back({Op::ValueAttr, 2, i, (NSUInteger) i});          // bufferIndex = i
			ops.push_back({Op::CreateLayout, i, 0, 0});
			ops.push_back({Op::ValueLayout, 0, i, 16});                    // stride = 16
		}
		for (int pass = 0; pass < 8; ++pass) {
			ops.push_back({Op::Reset, 0, 0, 0});
			for (int i = 0; i < 4; ++i) {                                  // the pass's elements
				const int slot = (pass + i) % 16;
				ops.push_back({Op::ValueAttr, 0, slot, 3});
				ops.push_back({Op::ValueAttr, 1, slot, (NSUInteger) (4 * i)});
				ops.push_back({Op::ValueAttr, 2, slot, (NSUInteger) (30 - i)});
				ops.push_back({Op::CreateLayout, 30 - i, 0, 0});
				ops.push_back({Op::ValueLayout, 0, 15 - i, 32});
			}
		}
		if (!RunDirected("game-rebuild", ops)) failures++;
	}

	// 2. reset with nothing ever mutated: only the fresh items exist, so a reset writes nothing
	{
		std::vector<Op> ops;
		for (int i = 0; i < 8; ++i) ops.push_back({Op::CreateAttr, i, 0, 0});
		for (int i = 0; i < 4; ++i) ops.push_back({Op::Reset, 0, 0, 0});
		if (!RunDirected("never-mutated", ops)) failures++;
	}

	// 3. a subclass instance a caller installed keeps its own setters: the slot must stay pinned,
	//    and a mutation through it (which cannot mark) must still be reset
	{
		std::vector<Op> ops;
		ops.push_back({Op::CreateAttr, 3, 0, 0});
		ops.push_back({Op::Reset, 0, 0, 0});
		ops.push_back({Op::CreateAttr, 5, 0, 0});
		ops.push_back({Op::RemoveAttr, 5, 0, 0});
		ops.push_back({Op::ValueAttr, 0, 0, MTLVertexFormatFloat4});     // through the held item
		ops.push_back({Op::Reset, 0, 0, 0});
		ops.push_back({Op::GraftAttr, 5, 0, 0});                        // install the held item
		ops.push_back({Op::Reset, 0, 0, 0});
		ops.push_back({Op::GraftSubclassAttr, 6, 0, 0});                // a caller-made item
		ops.push_back({Op::Reset, 0, 0, 0});
		ops.push_back({Op::ValueAttr, 0, 1, MTLVertexFormatFloat4});    // through the subclass item
		ops.push_back({Op::Reset, 0, 0, 0});
		if (!RunDirected("graft-held-and-subclass", ops)) failures++;
	}

	// 4. the same item in two slots: the second slot cannot be tracked by the item, so it must be
	//    pinned (the reset still writes it)
	{
		std::vector<Op> ops;
		ops.push_back({Op::CreateLayout, 1, 0, 0});
		ops.push_back({Op::ValueLayout, 0, 0, 64});
		ops.push_back({Op::GraftLayout, 2, 0, 0});                      // layout[2] = the item of 1
		ops.push_back({Op::Reset, 0, 0, 0});
		ops.push_back({Op::ValueLayout, 1, 0, 5});                      // mutate through slot 1's item
		ops.push_back({Op::Reset, 0, 0, 0});
		if (!RunDirected("item-in-two-slots", ops)) failures++;
	}

	// 5. remove and re-create a slot between resets
	{
		std::vector<Op> ops;
		ops.push_back({Op::CreateAttr, 7, 0, 0});
		ops.push_back({Op::ValueAttr, 0, 0, MTLVertexFormatFloat4});
		ops.push_back({Op::RemoveAttr, 7, 0, 0});
		ops.push_back({Op::Reset, 0, 0, 0});
		ops.push_back({Op::CreateAttr, 7, 0, 0});
		ops.push_back({Op::ValueAttr, 1, 1, 12});
		ops.push_back({Op::Reset, 0, 0, 0});
		if (!RunDirected("remove-recreate", ops)) failures++;
	}

	// 6. copy the descriptor in the middle (the copy pins everything it holds), keep mutating both
	{
		std::vector<Op> ops;
		for (int i = 0; i < 5; ++i) { ops.push_back({Op::CreateAttr, i, 0, 0}); ops.push_back({Op::ValueAttr, 0, i, MTLVertexFormatFloat4}); }
		ops.push_back({Op::Copy, 0, 0, 0});
		ops.push_back({Op::Reset, 0, 0, 0});
		ops.push_back({Op::ValueAttr, 1, 2, 8});
		ops.push_back({Op::Reset, 0, 0, 0});
		if (!RunDirected("copy-descriptor", ops)) failures++;
	}

	// 7. a caller keeps an item past the descriptor's death: the revocation must clear its slot
	//    pointer, so the later mutation cannot write into the dead descriptor's masks
	{
		std::vector<Op> ops;
		ops.push_back({Op::CreateLayout, 0, 0, 0});
		ops.push_back({Op::CreateAttr, 0, 0, 0});
		ops.push_back({Op::Reset, 0, 0, 0});
		ops.push_back({Op::RevokeUse, 0, 0, 0});
		if (!RunDirected("revoke-then-mutate", ops)) failures++;
	}

	// 7b. an item a caller removed and kept, then the descriptor dies: the removal must have
	//     cleared its mask pointer (the revocation only walks the slots the array still holds)
	{
		std::vector<Op> ops;
		ops.push_back({Op::CreateAttr, 0, 0, 0});
		ops.push_back({Op::ValueAttr, 0, 0, MTLVertexFormatFloat4});
		ops.push_back({Op::RemoveAttr, 0, 0, 0});
		ops.push_back({Op::Reset, 0, 0, 0});
		ops.push_back({Op::RevokeUse, 0, 0, 0});
		if (!RunDirected("remove-then-revoke", ops)) failures++;
	}

	// 8. randomised: every operation class, random slots and values
	{
		std::mt19937 rng(20261014);
		std::vector<World> worlds(2);
		for (World& w: worlds) w.alloc();
		Counters c;
		std::string why;
		bool ok = true;
		g_history.clear();
		for (int i = 0; i < 200000 && ok; ++i) {
			Op op;
			const unsigned pick = rng() % 100;
			if (pick < 18) op.kind = Op::Reset;
			else if (pick < 30) { op.kind = (rng() & 1) ? Op::CreateLayout : Op::CreateAttr; op.slot = rng() % 12; }
			else if (pick < 50 && !worlds[0].heldLayouts.empty() && !worlds[0].heldAttrs.empty()) {
				op.kind = (rng() & 1) ? Op::ValueLayout : Op::ValueAttr;
				op.held = rng() % (op.kind == Op::ValueLayout ? worlds[0].heldLayouts.size() : worlds[0].heldAttrs.size());
				op.slot = rng() % 3; op.value = rng() % 40;
			} else if (pick < 72 && !worlds[0].heldLayouts.empty() && !worlds[0].heldAttrs.empty()) {
				op.slot = rng() % 12;
				const unsigned flavour = rng() % 6;
				op.kind = flavour == 0 ? Op::GraftSubclassLayout
					: flavour == 1 ? Op::GraftSubclassAttr
					: (flavour & 1) ? Op::GraftLayout : Op::GraftAttr;
				op.held = rng() % (op.kind == Op::GraftLayout ? worlds[0].heldLayouts.size() : worlds[0].heldAttrs.size());
			} else if (pick < 88) { op.kind = (rng() & 1) ? Op::RemoveLayout : Op::RemoveAttr; op.slot = rng() % 12; }
			else op.kind = Op::Copy;
			if (!Apply(worlds, op, c, why)) { ok = false; break; }
			if (!worlds[0].heldLayouts.empty() && !worlds[0].heldAttrs.empty() && (rng() % 4096) == 0) {
				// occasionally end a world's life (the revocation path) and start a fresh one
				Op revoke {Op::RevokeUse, 0, 0, 0};
				if (!Apply(worlds, revoke, c, why)) { ok = false; break; }
				for (World& w: worlds) { w = World(); w.alloc(); }
			}
		}
		Report("randomized", c, ok, why);
		if (!ok) failures++;
	}

	printf("VERTEX_RESET_MODEL failures=%u\n", failures);
	return failures == 0 ? 0 : 1;
}
