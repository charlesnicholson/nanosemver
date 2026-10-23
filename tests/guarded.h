// Places a string so its last byte abuts an inaccessible page: reading even one byte
// past the end faults at the offending instruction, with or without a sanitizer.
// Falls back to an exact-size heap copy where there are no page protections.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string_view>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #define NSV_TEST_GUARD_PAGES 1
#elif defined(__unix__) || defined(__APPLE__)
  #include <sys/mman.h>
  #include <unistd.h>
  #define NSV_TEST_GUARD_PAGES 1
#endif

namespace nsv_test {

// Reusable: each place() overwrites the previous string, growing the mapping as needed.
class guarded {
 public:
  guarded() = default;
  guarded(guarded const &) = delete;
  guarded &operator=(guarded const &) = delete;
  ~guarded() { release(); }

  // Returns a pointer to a copy of s whose end is the guard page. Never null, even
  // for an empty s, so a zero-length parse is not mistaken for a NULL argument.
  char const *place(std::string_view s) {
    if (!body_ || (s.size() > cap_)) {
      reserve(s.size());
    }
    char *const p = body_ + (cap_ - s.size());
    if (!s.empty()) {
      std::memcpy(p, s.data(), s.size());
    }
    return p;
  }

 private:
#if defined(NSV_TEST_GUARD_PAGES)
  static size_t page_size() {
  #if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
  #else
    return static_cast<size_t>(::sysconf(_SC_PAGESIZE));
  #endif
  }

  void reserve(size_t n) {
    release();
    size_t const page = page_size();
    size_t const body = ((n + page - 1) / page) * page + page;  // >= one page
    map_len_ = body + page;
  #if defined(_WIN32)
    void *const m = VirtualAlloc(nullptr, map_len_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old;
    if (!m || !VirtualProtect(static_cast<char *>(m) + body, page, PAGE_NOACCESS, &old)) {
      std::abort();
    }
  #else
    void *const m =
        ::mmap(nullptr, map_len_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if ((m == MAP_FAILED) ||
        ::mprotect(static_cast<char *>(m) + body, page, PROT_NONE)) {
      std::abort();
    }
  #endif
    map_ = m;
    body_ = static_cast<char *>(m);
    cap_ = body;
  }

  void release() {
    if (map_) {
  #if defined(_WIN32)
      VirtualFree(map_, 0, MEM_RELEASE);
  #else
      ::munmap(map_, map_len_);
  #endif
    }
    map_ = nullptr;
    body_ = nullptr;
    cap_ = 0;
  }

  void *map_ = nullptr;
  size_t map_len_ = 0;
#else
  void reserve(size_t n) {
    release();
    body_ = static_cast<char *>(std::malloc(n ? n : 1));
    if (!body_) {
      std::abort();
    }
    cap_ = n;
  }

  void release() {
    std::free(body_);
    body_ = nullptr;
    cap_ = 0;
  }
#endif

  char *body_ = nullptr;
  size_t cap_ = 0;
};

}  // namespace nsv_test
