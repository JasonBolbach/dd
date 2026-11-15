# ddArena.h

*Arena allocator.*  Source: [`src/ddArena.h`](https://github.com/JasonBolbach/dd/blob/main/src/ddArena.h)

This is where every scene primitive lives. It exists so that a scene of thousands of small objects does not fragment
the heap of a microcontroller and so that a hard memory ceiling can be enforced.

**How it works**

- Memory is requested from the system in CHUNKS (default 8 KB) and carved up by a bump pointer. On ESP32 the chunks come from PSRAM first, then normal heap. Chunks are never moved, so object addresses stay valid.
- The total of all chunks is capped (default 256 KB). When the cap would be exceeded, New() returns nullptr instead of throwing or crashing.
- Reset() destroys everything and keeps the chunks, so rebuilding a scene every frame costs no malloc/free.
- Delete() destroys one object and puts its block on a size-class free list (blocks up to 1 KB), so a long-lived scene that adds and removes objects reuses memory instead of growing.
- Objects with non-trivial destructors are tracked and destroyed by Delete() and Reset().

**Block layout**

[Ext (only if a destructor is needed)][16-byte Base header][object ...], always 16-byte aligned. The header is how
Delete() can free an object knowing only its address.

**Pitfalls**

- Blocks are recycled by SIZE CLASS (16-byte steps, a block may also serve a request up to 128 bytes smaller). A scene whose mix of object sizes changes drastically over time can still creep upward in Capacity().
- Not thread-safe. Use one arena per thread, or lock around it.

**Tips and notes**

- Over-aligned types (alignof > 16) are rejected at compile time.

## Contents

- [DD_ARENA_DEFAULT_MAX](#dd_arena_default_max)
- [DD_ARENA_CHUNK](#dd_arena_chunk)
- [ddRound16()](#ddround16)
- [class ddArena](#class-ddarena)

## DD_ARENA_DEFAULT_MAX

```cpp
#define DD_ARENA_DEFAULT_MAX
```

[DD_ARENA_DEFAULT_MAX](API-ddArena#dd_arena_default_max): default byte cap of an arena (256 KB). Define it before including dd to change the default
for every [RenderBlock](API-ddVector#class-renderblock), or pass a cap to the [RenderBlock](API-ddVector#class-renderblock) / [ddArena](API-ddArena#class-ddarena) constructor per instance.

**Tips and notes**

- The cap counts chunk memory obtained from the system, not bytes handed out, so it bounds real RAM use.

---
## DD_ARENA_CHUNK

```cpp
#define DD_ARENA_CHUNK
```

[DD_ARENA_CHUNK](API-ddArena#dd_arena_chunk): growth step in bytes (8 KB). Smaller wastes less on tiny scenes; larger means fewer system allocations.
A request bigger than a chunk gets a chunk of its own size.

---
## ddRound16()

```cpp
constexpr size_t ddRound16(size_t n)
```

Rounds a size up to the next multiple of 16 (the arena's block granularity). Compile-time usable.

---
## class ddArena

```cpp
class ddArena
```

The arena allocator. See the file comment for the model.

```cpp
ddArena arena(64 * 1024);                 // cap at 64 KB
ddCircle *c = arena.New<ddCircle>(ddP(0, 0), 5.0f);
if (!c) handleFull();
arena.Delete(c);                          // destroy + recycle just this one
arena.Reset();                            // destroy everything, keep the memory
```

**Pitfalls**

- Non-copyable (copying would double-free the chunks).

**Tips and notes**

- You rarely use [ddArena](API-ddArena#class-ddarena) directly: [RenderBlock](API-ddVector#class-renderblock) owns one and exposes Emplace / Remove / Clear.

---
### ddArena::ddArena()

```cpp
explicit ddArena(size_t maxBytes = DD_ARENA_DEFAULT_MAX, size_t chunkBytes = DD_ARENA_CHUNK)
```

Creates an empty arena. No memory is allocated until the first New().

| Parameter | Description |
|---|---|
| `maxBytes` | Hard cap on total chunk memory (default [DD_ARENA_DEFAULT_MAX](API-ddArena#dd_arena_default_max)). |
| `chunkBytes` | Growth step (default [DD_ARENA_CHUNK](API-ddArena#dd_arena_chunk)). |


### ddArena::~ddArena()

```cpp
~ddArena()
```

Destroys every object and returns all chunk memory to the system.


### ddArena::ddArena() (overload 2)

```cpp
ddArena(const ddArena &) = delete
```

Copying is disabled.


### ddArena::New()

```cpp
template <class T, class... A> T *New(A &&...a)
```

Constructs a T inside the arena.

| Parameter | Description |
|---|---|
| `T` | Any type with alignof <= 16. If it has a non-trivial destructor the arena will call it later. |
| `a` | Constructor arguments, perfectly forwarded. |

**Returns:** Pointer to the new object, or nullptr if the byte cap would be exceeded (Overflowed() then returns true).

**Pitfalls**

- ALWAYS check for nullptr when the cap is small or the scene is large; dereferencing it crashes.
- Never `delete` the result. Use Delete(), Reset() or let the arena be destroyed.

**Tips and notes**

- Objects deriving from [ddVectorPrimitive](API-ddVector#struct-ddvectorprimitive) must derive singly so the object's address equals the arena block's.


### ddArena::NewArray()

```cpp
template <class T> T *NewArray(size_t n)
```

Allocates an uninitialised array of n trivially-destructible items (e.g. the points of a polygon).

| Parameter | Description |
|---|---|
| `T` | A trivially destructible type (checked at compile time). |
| `n` | Element count. |

**Returns:** The array, or nullptr if the cap would be exceeded. Release it with Delete().

**Pitfalls**

- The elements are NOT constructed: assign or placement-new each one before reading it.


### ddArena::Delete()

```cpp
void Delete(void *obj)
```

Destroys one object (or frees one array) from New / NewArray and recycles its block.

| Parameter | Description |
|---|---|
| `obj` | The address New / NewArray returned. A base-class pointer to a singly-derived object is fine.               nullptr is ignored. |

**Pitfalls**

- Deleting an address that did not come from this arena, or deleting twice, is detected by a header check and ignored in most cases, but it is still a bug in the caller.

**Tips and notes**

- Does nothing while the arena is inside Reset() (the reset is already destroying everything).


### ddArena::Reset()

```cpp
void Reset()
```

Destroys every live object (newest first) and rewinds all chunks, keeping the memory for reuse.

**Pitfalls**

- Every pointer previously returned by New / NewArray is invalid afterwards.

**Tips and notes**

- This is what makes a per-frame scene free: Reset(), rebuild, draw.


### ddArena::Release()

```cpp
void Release()
```

Reset(), then gives all chunk memory back to the system. The arena can be used again afterwards (it will re-grow).


### ddArena::IsResetting()

```cpp
bool IsResetting() const
```

True while Reset() is running its destructors. Destructors that call back into the arena (e.g. a group destroying its
children) use it to avoid freeing things twice.


### ddArena::Capacity()

```cpp
size_t Capacity() const
```

**Returns:** Bytes of chunk memory currently held from the system (never exceeds Max()).


### ddArena::Max()

```cpp
size_t Max() const
```

**Returns:** The byte cap.


### ddArena::SetMax()

```cpp
void SetMax(size_t m)
```

Changes the byte cap.

**Pitfalls**

- Lowering it below Capacity() does not free anything; it only prevents further growth.


### ddArena::InUse()

```cpp
size_t InUse() const
```

**Returns:** Bytes in live blocks, headers included. Rises with New(), falls with Delete(), zero after Reset().


### ddArena::Peak()

```cpp
size_t Peak() const
```

**Returns:** The largest InUse() ever reached. Use it to size the cap: run your heaviest scene, read Peak(), add headroom.


### ddArena::Overflowed()

```cpp
bool Overflowed() const
```

**Returns:** true if any allocation has ever been refused because of the cap (until ClearOverflow()).


### ddArena::ClearOverflow()

```cpp
void ClearOverflow()
```

Clears the Overflowed() flag.
