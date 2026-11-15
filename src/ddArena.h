#pragma once
// Chunked arena allocator with per-object free and a hard byte cap.
//  - Memory comes from PSRAM first on ESP32, then the regular heap.
//  - Grows lazily in chunks (never moves existing objects) up to MaxBytes.
//  - Reset() destroys everything and keeps the chunks, so rebuilding every frame costs no malloc/free.
//  - Delete() destroys one object and recycles its block (size-class free lists, blocks <= 1 KB),
//    so long-lived scenes that add/remove objects don't grow without bound.
//  - Objects with non-trivial destructors are tracked and destroyed on Delete()/Reset().
//  - New/NewArray/Delete/Reset return nullptr / do nothing rather than throw.
// Block layout: [Ext (only if a destructor is needed)][Base header 16 B][object ...], 16-byte aligned.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

#ifndef DD_ARENA_DEFAULT_MAX
#define DD_ARENA_DEFAULT_MAX (256u * 1024u)   // default cap; override per block via RenderBlock(maxBytes)
#endif
#ifndef DD_ARENA_CHUNK
#define DD_ARENA_CHUNK (8u * 1024u)           // growth granularity
#endif

constexpr size_t ddRound16(size_t n) { return (n + 15) & ~size_t(15); }

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
  explicit ddArena(size_t maxBytes = DD_ARENA_DEFAULT_MAX, size_t chunkBytes = DD_ARENA_CHUNK)
    : MaxBytes(maxBytes), ChunkBytes(chunkBytes) {}
  ~ddArena() { Release(); }
  ddArena(const ddArena &) = delete;
  ddArena &operator=(const ddArena &) = delete;

  // Construct a T in the arena. Returns nullptr if the cap would be exceeded.
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

  // Uninitialised array of trivially-destructible items; release with Delete().
  template <class T>
  T *NewArray(size_t n) {
    static_assert(std::is_trivially_destructible<T>::value, "NewArray needs a trivially destructible type");
    return (T *)AllocBlock(sizeof(T) * n, false);
  }

  // Destroy one object (or free an array) from New/NewArray and recycle its block.
  // Pass the address New returned (a base-class pointer to a singly-derived object is fine).
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

  // Destroy everything, keep the chunks for reuse.
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
  // Reset and give the memory back to the system.
  void Release() {
    Reset();
    for (Chunk *c = Head; c;) { Chunk *n = c->Next; free(c); c = n; }
    Head = Cur = Last = nullptr; Total = 0;
  }

  bool   IsResetting() const { return Resetting; }
  size_t Capacity() const { return Total; }         // bytes currently held from the system
  size_t Max() const { return MaxBytes; }
  void   SetMax(size_t m) { MaxBytes = m; }
  size_t InUse() const { return LiveBytes; }        // bytes in live blocks (headers included)
  size_t Peak() const { return PeakBytes; }         // most live bytes at once
  bool   Overflowed() const { return Overflow; }
  void   ClearOverflow() { Overflow = false; }
};
