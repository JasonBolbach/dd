#pragma once
/**
 * @file ddArena.h
 * Chunked arena allocator with per-object free and a hard byte cap. Graphics-free.
 *
 * This is where every scene primitive lives. It exists so that a scene of thousands of small objects does not fragment
 * the heap of a microcontroller and so that a hard memory ceiling can be enforced.
 *
 * @par How it works
 *   - Memory is requested from the system in CHUNKS (default 8 KB) and carved up by a bump pointer. On ESP32 the chunks
 *     come from PSRAM first, then normal heap. Chunks are never moved, so object addresses stay valid.
 *   - The total of all chunks is capped (default 256 KB). When the cap would be exceeded, New() returns nullptr instead
 *     of throwing or crashing.
 *   - Reset() destroys everything and keeps the chunks, so rebuilding a scene every frame costs no malloc/free.
 *   - Delete() destroys one object and puts its block on a size-class free list (blocks up to 1 KB), so a long-lived
 *     scene that adds and removes objects reuses memory instead of growing.
 *   - Objects with non-trivial destructors are tracked and destroyed by Delete() and Reset().
 *
 * @par Block layout
 *   [Ext (only if a destructor is needed)][16-byte Base header][object ...], always 16-byte aligned. The header is how
 *   Delete() can free an object knowing only its address.
 *
 * @warning Blocks are recycled by SIZE CLASS (16-byte steps, a block may also serve a request up to 128 bytes smaller).
 *          A scene whose mix of object sizes changes drastically over time can still creep upward in Capacity().
 * @warning Not thread-safe. Use one arena per thread, or lock around it.
 * @note Over-aligned types (alignof > 16) are rejected at compile time.
 */
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

/**
 * DD_ARENA_DEFAULT_MAX: default byte cap of an arena (256 KB). Define it before including dd to change the default
 * for every RenderBlock, or pass a cap to the RenderBlock / ddArena constructor per instance.
 * @note The cap counts chunk memory obtained from the system, not bytes handed out, so it bounds real RAM use.
 */
#ifndef DD_ARENA_DEFAULT_MAX
#define DD_ARENA_DEFAULT_MAX (256u * 1024u)   // default cap; override per block via RenderBlock(maxBytes)
#endif
/**
 * DD_ARENA_CHUNK: growth step in bytes (8 KB). Smaller wastes less on tiny scenes; larger means fewer system allocations.
 * A request bigger than a chunk gets a chunk of its own size.
 */
#ifndef DD_ARENA_CHUNK
#define DD_ARENA_CHUNK (8u * 1024u)           // growth granularity
#endif

/**
 * Rounds a size up to the next multiple of 16 (the arena's block granularity). Compile-time usable.
 */
constexpr size_t ddRound16(size_t n) { return (n + 15) & ~size_t(15); }

/**
 * The arena allocator. See the file comment for the model.
 *
 * @code
 * ddArena arena(64 * 1024);                 // cap at 64 KB
 * ddCircle *c = arena.New<ddCircle>(ddP(0, 0), 5.0f);
 * if (!c) handleFull();
 * arena.Delete(c);                          // destroy + recycle just this one
 * arena.Reset();                            // destroy everything, keep the memory
 * @endcode
 *
 * @note You rarely use ddArena directly: RenderBlock owns one and exposes Emplace / Remove / Clear.
 * @warning Non-copyable (copying would double-free the chunks).
 */
class ddArena {
  struct Chunk { Chunk *Next; size_t Size; size_t Used; };
  struct Ext   { Ext *Next; Ext *Prev; void (*Fn)(void *); };
  struct Base  { uint32_t Size; uint32_t Flags; uint32_t Magic; uint32_t Pad; };
  static constexpr size_t HDR   = ddRound16(sizeof(Chunk));
  static constexpr size_t EXTSZ = ddRound16(sizeof(Ext));
  static constexpr size_t BASESZ = 16;
  static constexpr uint32_t MAGIC = 0xDDA0A5E1u;
  static constexpr size_t NCLASS = 65;          // size classes: total/16 for blocks up to 1024 bytes

  Chunk *Head = nullptr, *Cur = nullptr, *Last = nullptr;
  Ext   *Live = nullptr;                        // objects with destructors, newest first
  void  *Free[NCLASS] = {};
  size_t MaxBytes, ChunkBytes, Total = 0, LiveBytes = 0, PeakBytes = 0;
  bool   Overflow = false, Resetting = false;

  static void *RawAlloc(size_t n) {
#if defined(ESP_PLATFORM)
    void *p = heap_caps_aligned_alloc(16, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_aligned_alloc(16, n, MALLOC_CAP_8BIT);
    return p;
#else
    return malloc(n);
#endif
  }
  uint8_t *Data(Chunk *c) const { return (uint8_t *)c + HDR; }

  // Bump-allocate `n` bytes (multiple of 16), 16-byte aligned.
  void *Bump(size_t n) {
    for (;;) {
      if (Cur) {
        if (Cur->Used + n <= Cur->Size) { void *p = Data(Cur) + Cur->Used; Cur->Used += n; return p; }
        if (Cur->Next) { Cur = Cur->Next; continue; }
      }
      size_t size = n > ChunkBytes ? n : ChunkBytes;
      if (Total + size > MaxBytes) size = n;                    // exact-fit chunk as a last resort
      if (Total + size > MaxBytes) { Overflow = true; return nullptr; }
      Chunk *c = (Chunk *)RawAlloc(HDR + size);
      if (!c) { Overflow = true; return nullptr; }
      c->Next = nullptr; c->Size = size; c->Used = 0;
      Total += size;
      if (Last) Last->Next = c; else Head = c;
      Last = Cur = c;
    }
  }

  static Base *BaseOf(void *obj) { return (Base *)((uint8_t *)obj - BASESZ); }
  static Ext  *ExtOf(void *obj)  { return (Ext *)((uint8_t *)obj - BASESZ - EXTSZ); }
  static void *ObjOf(Ext *e)     { return (uint8_t *)e + EXTSZ + BASESZ; }

  // Returns the object address of a fresh block with room for `payload` bytes.
  void *AllocBlock(size_t payload, bool withExt) {
    size_t total = ddRound16(payload) + BASESZ + (withExt ? EXTSZ : 0);
    size_t cls = total / 16;
    uint8_t *blk = nullptr;
    // Exact size class first, then a slightly larger recycled block (up to 128 bytes of slack).
    for (size_t c = cls; c < NCLASS && c < cls + 8; c++)
      if (Free[c]) { blk = (uint8_t *)Free[c]; Free[c] = *(void **)blk; total = c * 16; break; }
    if (!blk) blk = (uint8_t *)Bump(total);
    if (!blk) return nullptr;
    Base *b = (Base *)(blk + (withExt ? EXTSZ : 0));
    b->Size = (uint32_t)total; b->Flags = withExt ? 1u : 0u; b->Magic = MAGIC; b->Pad = 0;
    LiveBytes += total;
    if (LiveBytes > PeakBytes) PeakBytes = LiveBytes;
    return (uint8_t *)b + BASESZ;
  }

  void ReleaseBlock(void *obj) {
    Base *b = BaseOf(obj);
    if (b->Magic != MAGIC) return;                              // not ours / double free
    b->Magic = 0;
    uint8_t *blk = (uint8_t *)b - ((b->Flags & 1u) ? EXTSZ : 0);
    size_t total = b->Size, cls = total / 16;
    LiveBytes -= total;
    if (cls < NCLASS) { *(void **)blk = Free[cls]; Free[cls] = blk; }
  }

 public:
  /**
   * Creates an empty arena. No memory is allocated until the first New().
   * @param maxBytes    Hard cap on total chunk memory (default DD_ARENA_DEFAULT_MAX).
   * @param chunkBytes  Growth step (default DD_ARENA_CHUNK).
   */
  explicit ddArena(size_t maxBytes = DD_ARENA_DEFAULT_MAX, size_t chunkBytes = DD_ARENA_CHUNK)
    : MaxBytes(maxBytes), ChunkBytes(chunkBytes) {}
  /**
   * Destroys every object and returns all chunk memory to the system.
   */
  ~ddArena() { Release(); }
  /**
   * Copying is disabled.
   */
  ddArena(const ddArena &) = delete;
  ddArena &operator=(const ddArena &) = delete;

  /**
   * Constructs a T inside the arena.
   * @tparam T     Any type with alignof <= 16. If it has a non-trivial destructor the arena will call it later.
   * @param a      Constructor arguments, perfectly forwarded.
   * @return       Pointer to the new object, or nullptr if the byte cap would be exceeded (Overflowed() then returns true).
   * @warning ALWAYS check for nullptr when the cap is small or the scene is large; dereferencing it crashes.
   * @warning Never `delete` the result. Use Delete(), Reset() or let the arena be destroyed.
   * @note Objects deriving from ddVectorPrimitive must derive singly so the object's address equals the arena block's.
   */
  template <class T, class... A>
  T *New(A &&...a) {
    static_assert(alignof(T) <= 16, "over-aligned types are not supported");
    constexpr bool needDtor = !std::is_trivially_destructible<T>::value;
    void *mem = AllocBlock(sizeof(T), needDtor);
    if (!mem) return nullptr;
    T *o = new (mem) T(std::forward<A>(a)...);
    if constexpr (needDtor) {
      Ext *e = ExtOf(mem);
      e->Fn = [](void *p) { ((T *)p)->~T(); };
      e->Prev = nullptr; e->Next = Live;
      if (Live) Live->Prev = e;
      Live = e;
    }
    return o;
  }

  /**
   * Allocates an uninitialised array of n trivially-destructible items (e.g. the points of a polygon).
   * @tparam T  A trivially destructible type (checked at compile time).
   * @param n   Element count.
   * @return    The array, or nullptr if the cap would be exceeded. Release it with Delete().
   * @warning The elements are NOT constructed: assign or placement-new each one before reading it.
   */
  template <class T>
  T *NewArray(size_t n) {
    static_assert(std::is_trivially_destructible<T>::value, "NewArray needs a trivially destructible type");
    return (T *)AllocBlock(sizeof(T) * n, false);
  }

  /**
   * Destroys one object (or frees one array) from New / NewArray and recycles its block.
   * @param obj  The address New / NewArray returned. A base-class pointer to a singly-derived object is fine.
   *               nullptr is ignored.
   * @warning Deleting an address that did not come from this arena, or deleting twice, is detected by a header check and
   *          ignored in most cases, but it is still a bug in the caller.
   * @note Does nothing while the arena is inside Reset() (the reset is already destroying everything).
   */
  void Delete(void *obj) {
    if (!obj || Resetting) return;
    Base *b = BaseOf(obj);
    if (b->Magic != MAGIC) return;
    if (b->Flags & 1u) {
      Ext *e = ExtOf(obj);
      if (e->Prev) e->Prev->Next = e->Next; else Live = e->Next;
      if (e->Next) e->Next->Prev = e->Prev;
      e->Fn(obj);
    }
    ReleaseBlock(obj);
  }

  /**
   * Destroys every live object (newest first) and rewinds all chunks, keeping the memory for reuse.
   * @note This is what makes a per-frame scene free: Reset(), rebuild, draw.
   * @warning Every pointer previously returned by New / NewArray is invalid afterwards.
   */
  void Reset() {
    Resetting = true;
    while (Live) {
      Ext *e = Live;
      Live = e->Next;
      if (Live) Live->Prev = nullptr;
      e->Fn(ObjOf(e));                                         // newest first
    }
    Resetting = false;
    for (Chunk *c = Head; c; c = c->Next) c->Used = 0;
    Cur = Head;
    for (size_t i = 0; i < NCLASS; i++) Free[i] = nullptr;
    LiveBytes = 0;
  }
  /**
   * Reset(), then gives all chunk memory back to the system. The arena can be used again afterwards (it will re-grow).
   */
  void Release() {
    Reset();
    for (Chunk *c = Head; c;) { Chunk *n = c->Next; free(c); c = n; }
    Head = Cur = Last = nullptr; Total = 0;
  }

  /**
   * True while Reset() is running its destructors. Destructors that call back into the arena (e.g. a group destroying its
   * children) use it to avoid freeing things twice.
   */
  bool   IsResetting() const { return Resetting; }
  /**
   * @return Bytes of chunk memory currently held from the system (never exceeds Max()).
   */
  size_t Capacity() const { return Total; }         // bytes currently held from the system
  /**
   * @return The byte cap.
   */
  size_t Max() const { return MaxBytes; }
  /**
   * Changes the byte cap.
   * @warning Lowering it below Capacity() does not free anything; it only prevents further growth.
   */
  void   SetMax(size_t m) { MaxBytes = m; }
  /**
   * @return Bytes in live blocks, headers included. Rises with New(), falls with Delete(), zero after Reset().
   */
  size_t InUse() const { return LiveBytes; }        // bytes in live blocks (headers included)
  /**
   * @return The largest InUse() ever reached. Use it to size the cap: run your heaviest scene, read Peak(), add headroom.
   */
  size_t Peak() const { return PeakBytes; }         // most live bytes at once
  /**
   * @return true if any allocation has ever been refused because of the cap (until ClearOverflow()).
   */
  bool   Overflowed() const { return Overflow; }
  /**
   * Clears the Overflowed() flag.
   */
  void   ClearOverflow() { Overflow = false; }
};
