#pragma once

// Minimal test harness: TEST(name) { CHECK(...); } and a main() that runs every test.

#include <cstdio>
#include <functional>
#include <vector>

namespace ii_test {

  struct Case {
    const char* name;
    void (*fn)();
  };

  inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
  }

  inline int& failures() {
    static int count = 0;
    return count;
  }

  struct Register {
    Register(const char* name, void (*fn)()) { cases().push_back({name, fn}); }
  };

  inline void fail(const char* file, int line, const char* expr) {
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, expr);
    ++failures();
  }

  inline int runAll() {
    for (const auto& c : cases()) {
      const int before = failures();
      c.fn();
      std::fprintf(stderr, "%s %s\n", failures() == before ? "ok  " : "FAIL", c.name);
    }
    std::fprintf(stderr, "%zu tests, %d failed checks\n", cases().size(), failures());
    return failures() == 0 ? 0 : 1;
  }

} // namespace ii_test

#define II_TEST_CAT2(a, b) a##b
#define II_TEST_CAT(a, b) II_TEST_CAT2(a, b)
#define TEST(name)                                                                                                 \
  static void II_TEST_CAT(test_fn_, __LINE__)();                                                                   \
  static const ii_test::Register II_TEST_CAT(test_reg_, __LINE__)(name, &II_TEST_CAT(test_fn_, __LINE__));         \
  static void II_TEST_CAT(test_fn_, __LINE__)()
#define CHECK(expr)                                                                                                \
  do {                                                                                                             \
    if (!(expr)) {                                                                                                 \
      ii_test::fail(__FILE__, __LINE__, #expr);                                                                    \
    }                                                                                                              \
  } while (false)
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define TEST_MAIN()                                                                                                \
  int main() { return ii_test::runAll(); }
