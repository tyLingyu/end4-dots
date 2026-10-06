// runtime/js.h against Qt's JS engine: every case of tests/diff/js_dump.qml (expected/js.json)
// is replayed through the C++ helpers. Numbers are compared through String(), so toString()
// is checked along with whatever produced them.

#include "runtime/js.h"

#include "../check.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <map>
#include <string>

using namespace ii;
using Json = nlohmann::json;

namespace {

  Json expected() {
    std::ifstream in(std::string(II_DIFF_EXPECTED_DIR) + "/js.json");
    return Json::parse(in)["cases"];
  }

  double num(const Json& arg) { return js::toNumber(arg.get<std::string>()); }
  std::string str(const Json& arg) { return arg.get<std::string>(); }
  int integer(const Json& arg) { return arg.get<int>(); }

  Json strings(const std::vector<std::string>& values) { return Json(values); }

  // Op name -> the C++ result for the dumped arguments, as JSON comparable with Qt's.
  const std::map<std::string, Json (*)(const Json&)>& ops() {
    static const std::map<std::string, Json (*)(const Json&)> table = {
        {"toString", [](const Json& a) -> Json { return js::toString(num(a[0])); }},
        {"round", [](const Json& a) -> Json { return js::toString(js::round(num(a[0]))); }},
        {"toFixed", [](const Json& a) -> Json { return js::toFixed(num(a[0]), integer(a[1])); }},
        {"toStringRadix", [](const Json& a) -> Json { return js::toString(num(a[0]), integer(a[1])); }},
        {"Number", [](const Json& a) -> Json { return js::toString(js::toNumber(str(a[0]))); }},
        {"parseInt", [](const Json& a) -> Json { return js::toString(js::parseInt(str(a[0]))); }},
        {"parseIntRadix", [](const Json& a) -> Json { return js::toString(js::parseInt(str(a[0]), integer(a[1]))); }},
        {"parseFloat", [](const Json& a) -> Json { return js::toString(js::parseFloat(str(a[0]))); }},
        {"length", [](const Json& a) -> Json { return std::to_string(js::length(str(a[0]))); }},
        {"trim", [](const Json& a) -> Json { return js::trim(str(a[0])); }},
        {"toLowerCase", [](const Json& a) -> Json { return js::toLowerCase(str(a[0])); }},
        {"toUpperCase", [](const Json& a) -> Json { return js::toUpperCase(str(a[0])); }},
        {"slice", [](const Json& a) -> Json { return js::slice(str(a[0]), integer(a[1]), integer(a[2])); }},
        {"slice1", [](const Json& a) -> Json { return js::slice(str(a[0]), integer(a[1])); }},
        {"substring", [](const Json& a) -> Json { return js::substring(str(a[0]), integer(a[1]), integer(a[2])); }},
        {"substr", [](const Json& a) -> Json { return js::substr(str(a[0]), integer(a[1]), integer(a[2])); }},
        {"charAt", [](const Json& a) -> Json { return js::charAt(str(a[0]), integer(a[1])); }},
        {"charCodeAt", [](const Json& a) -> Json { return js::toString(js::charCodeAt(str(a[0]), integer(a[1]))); }},
        {"indexOf", [](const Json& a) -> Json { return std::to_string(js::indexOf(str(a[0]), str(a[1]))); }},
        {"indexOfFrom", [](const Json& a) -> Json { return std::to_string(js::indexOf(str(a[0]), str(a[1]), integer(a[2]))); }},
        {"lastIndexOf", [](const Json& a) -> Json { return std::to_string(js::lastIndexOf(str(a[0]), str(a[1]))); }},
        {"includes", [](const Json& a) -> Json { return js::includes(str(a[0]), str(a[1])); }},
        {"startsWith", [](const Json& a) -> Json { return js::startsWith(str(a[0]), str(a[1])); }},
        {"endsWith", [](const Json& a) -> Json { return js::endsWith(str(a[0]), str(a[1])); }},
        {"split", [](const Json& a) -> Json { return strings(js::split(str(a[0]), str(a[1]))); }},
        {"splitLimit", [](const Json& a) -> Json { return strings(js::split(str(a[0]), str(a[1]), a[2].get<std::uint32_t>())); }},
        {"replace", [](const Json& a) -> Json { return js::replace(str(a[0]), str(a[1]), str(a[2])); }},
        {"padStart", [](const Json& a) -> Json { return js::padStart(str(a[0]), integer(a[1]), str(a[2])); }},
        {"padEnd", [](const Json& a) -> Json { return js::padEnd(str(a[0]), integer(a[1]), str(a[2])); }},
        {"repeat", [](const Json& a) -> Json { return js::repeat(str(a[0]), integer(a[1])); }},
        {"reTest", [](const Json& a) -> Json { return js::test(js::Regex(str(a[1]), str(a[2])), str(a[0])); }},
        {"reSearch", [](const Json& a) -> Json { return std::to_string(js::search(str(a[0]), js::Regex(str(a[1]), str(a[2])))); }},
        {"reMatch",
         [](const Json& a) -> Json {
           const auto m = js::match(str(a[0]), js::Regex(str(a[1]), str(a[2])));
           return m ? strings(*m) : Json(nullptr);
         }},
        {"reReplace", [](const Json& a) -> Json { return js::replace(str(a[0]), js::Regex(str(a[1]), str(a[2])), str(a[3])); }},
        {"reReplaceFn",
         [](const Json& a) -> Json {
           return js::replace(str(a[0]), js::Regex(str(a[1]), str(a[2])),
                              [](const std::vector<std::string>& m) { return "{" + std::to_string(js::length(m[0])) + "}"; });
         }},
        {"reSplit", [](const Json& a) -> Json { return strings(js::split(str(a[0]), js::Regex(str(a[1]), str(a[2])))); }},
        {"reSplitLimit",
         [](const Json& a) -> Json {
           return strings(js::split(str(a[0]), js::Regex(str(a[1]), str(a[2])), a[3].get<std::uint32_t>()));
         }},
        {"reExecAll",
         [](const Json& a) -> Json {
           const js::Regex re(str(a[1]), str(a[2]));
           Json found = Json::array();
           int lastIndex = 0;
           for (int guard = 0; guard < 50; ++guard) {
             const auto m = js::exec(re, str(a[0]), lastIndex);
             if (!m) {
               break;
             }
             found.push_back(Json{std::to_string(m->index), std::to_string(lastIndex), strings(m->groups)});
             if (!re.global()) {
               break;
             }
             if (js::length(m->groups[0]) == 0) {
               ++lastIndex;
             }
           }
           return found;
         }},
        {"stringify", [](const Json& a) -> Json { return js::stringify(js::parse(str(a[0])), integer(a[1])); }},
        {"stableSort",
         [](const Json& a) -> Json {
           std::vector<std::pair<double, int>> pairs;
           for (const auto& k : a[0]) {
             pairs.emplace_back(num(k), static_cast<int>(pairs.size()));
           }
           const auto sorted = js::sorted(pairs, [](const auto& x, const auto& y) { return x.first - y.first; });
           return strings(js::map(sorted, [](const auto& p) { return std::to_string(p.second); }));
         }},
        {"joinNumbers",
         [](const Json& a) -> Json { return js::join(js::map(a[0].get<std::vector<std::string>>(), [](const std::string& s) {
                                                     return js::toNumber(s);
                                                   }),
                                                   "|"); }},
    };
    return table;
  }

} // namespace

TEST("js: every Qt sample agrees") {
  int checked = 0;
  for (const auto& c : expected()) {
    const std::string op = c[0];
    const auto it = ops().find(op);
    if (it == ops().end()) {
      std::fprintf(stderr, "  no op %s\n", op.c_str());
      CHECK(false);
      continue;
    }
    const Json ours = it->second(c[1]);
    if (ours != c[2]) {
      std::fprintf(stderr, "  %s%s: %s, Qt %s\n", op.c_str(), c[1].dump().c_str(), ours.dump().c_str(), c[2].dump().c_str());
      CHECK(false);
    }
    ++checked;
  }
  CHECK(checked > 1000);
}

TEST("js: arrays") {
  const std::vector<int> v{1, 2, 3, 4};
  CHECK(js::slice(v, -2) == (std::vector<int>{3, 4}));
  CHECK(js::indexOf(v, 3) == 2 && js::indexOf(v, 9) == -1);
  CHECK(js::filter(v, [](int x) { return x % 2 == 0; }) == (std::vector<int>{2, 4}));
  CHECK(js::find(v, [](int x) { return x > 2; }) == 3);
  CHECK(!js::find(v, [](int x) { return x > 9; }));
  CHECK(js::map(v, [](int x, int i) { return x * i; }) == (std::vector<int>{0, 2, 6, 12}));
  CHECK(js::join(std::vector<std::string>{"a", "b"}) == "a,b");
}

TEST("js: Math.round") {
  CHECK(js::round(0.49999999999999994) == 0.0);
  CHECK(js::round(-2.5) == -2.0 && js::round(2.5) == 3.0 && js::round(-2.6) == -3.0);
}

TEST_MAIN()
