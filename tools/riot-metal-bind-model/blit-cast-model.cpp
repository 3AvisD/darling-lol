// Model of the two mechanical rewrites in Indium's blit encoder.
//
// The shipping change (deps/indium/src/indium/blit-command-encoder.cpp) is:
//   1. the BlitCommandEncoder entry points take `const std::shared_ptr<Buffer>&`
//      instead of `std::shared_ptr<Buffer>`, so calling them no longer copies the
//      shared_ptr (one atomic increment at the call, one atomic decrement at return);
//   2. `auto privateSource = std::dynamic_pointer_cast<PrivateBuffer>(source)` became
//      `auto* privateSource = dynamic_cast<PrivateBuffer*>(source.get())`: the same
//      dynamic_cast on the same object, without building a shared_ptr for the result.
//
// The real classes need a Vulkan device, so this models the two rewrites on stand-ins
// with the same shape (private classes derived from the public ones) and checks them
// against the previous formulation for the cases the wrapper can produce: a real
// resource, a null pointer, and a public object that is not the private class.
//
// It also counts the atomic operations, which is what the change is for.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Buffer {
	long handle = 1;
	virtual ~Buffer() = default;
};
struct PrivateBuffer: Buffer {
	long privateHandle = 2;
};
// a public Buffer the private class does not answer for (only the dynamic cast can tell)
struct ForeignBuffer: Buffer {
	virtual ~ForeignBuffer() = default;
};

using BufferPtr = std::shared_ptr<Buffer>;

// ---------------------------------------------------------------- before the change
static long oldPrivateHandle(const BufferPtr& source) {
	auto privateSource = std::dynamic_pointer_cast<PrivateBuffer>(source);
	long handle = privateSource ? privateSource->privateHandle : -1;
	return handle;
}
static long oldCopy(BufferPtr source, BufferPtr destination) {   // by value, as before
	long result = oldPrivateHandle(source) + oldPrivateHandle(destination);
	return result;
}

// ---------------------------------------------------------------- after the change
static long newPrivateHandle(const BufferPtr& source) {
	auto* privateSource = dynamic_cast<PrivateBuffer*>(source.get());
	long handle = privateSource ? privateSource->privateHandle : -1;
	return handle;
}
static long newCopy(const BufferPtr& source, const BufferPtr& destination) {   // by reference now
	long result = newPrivateHandle(source) + newPrivateHandle(destination);
	return result;
}

// ------------------------------------------------------------------ copy counts
// The two formulations again, with a stand-in for shared_ptr that counts copies: this
// is the difference the signature change removes (each copy is an atomic refcount pair
// in a real shared_ptr). Moves are free in both formulations, as they are in libc++.
template<class T> struct CountingPtr {
	std::shared_ptr<T> impl;
	long* copies;
	CountingPtr(std::shared_ptr<T> value, long* counter) : impl(std::move(value)), copies(counter) {}
	CountingPtr(const CountingPtr& other) : impl(other.impl), copies(other.copies) { ++*copies; }
	CountingPtr(CountingPtr&& other) noexcept : impl(std::move(other.impl)), copies(other.copies) {}
	CountingPtr& operator=(const CountingPtr&) = delete;
	T* get() const { return impl.get(); }
};

static long oldCopyCount(const CountingPtr<Buffer> source, const CountingPtr<Buffer> destination) {
	CountingPtr<Buffer> sourceLocal = source;            // the local the cast built
	long handle = sourceLocal.get() ? 1 : 0;
	return handle + (destination.get() ? 1 : 0);
}
static long newCopyCount(const CountingPtr<Buffer>& source, const CountingPtr<Buffer>& destination) {
	auto* privateSource = dynamic_cast<PrivateBuffer*>(source.get());
	return (privateSource ? 1 : 0) + (destination.get() ? 1 : 0);
}

int main() {
	const BufferPtr real = std::make_shared<PrivateBuffer>();
	const BufferPtr foreign = std::make_shared<ForeignBuffer>();
	const BufferPtr empty;

	// the dynamic cast answers exactly what the shared_ptr cast answered
	CHECK(oldPrivateHandle(real) == newPrivateHandle(real), "real private resource");
	CHECK(oldPrivateHandle(foreign) == newPrivateHandle(foreign), "public resource that is not private");
	CHECK(oldPrivateHandle(empty) == newPrivateHandle(empty), "null resource");
	CHECK(newPrivateHandle(foreign) == -1, "a non-private resource must not be treated as private");

	// ... and through a full copy call, with every kind of argument the wrapper produces
	const long expected = oldCopy(real, real);
	CHECK(newCopy(real, real) == expected, "copy(real, real)");
	CHECK(newCopy(real, empty) == oldCopy(real, empty), "copy(real, null)");
	CHECK(newCopy(foreign, real) == oldCopy(foreign, real), "copy(foreign, real)");
	// temporaries bind to the const references and live for the whole call
	CHECK(newCopy(BufferPtr(new PrivateBuffer), BufferPtr(new PrivateBuffer)) == expected, "copy(temporary, temporary)");

	// the point of the change: the by-value call copies the shared_ptr twice (two atomic
	// refcount pairs per buffer), the by-reference call copies none
	long oldCopies = 0, newCopies = 0;
	{
		CountingPtr<Buffer> a(real, &oldCopies), b(real, &oldCopies);
		oldCopyCount(a, b);
	}
	{
		CountingPtr<Buffer> a(real, &newCopies), b(real, &newCopies);
		newCopyCount(a, b);
	}
	// the model copies once per by-value parameter plus once for the cast's result
	CHECK(oldCopies >= 3, "by-value parameters must copy the shared_ptr (%ld copies)", oldCopies);
	CHECK(newCopies == 0, "by-reference parameters must not copy it (%ld copies)", newCopies);
	std::printf("blit signature model: copies per buffer->buffer copy: old=%ld new=%ld\n", oldCopies, newCopies);

	std::printf("BLIT_CAST_MODEL failures=%d\n", failures);
	return failures != 0;
}
