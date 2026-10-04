#include <blendScene/Naming.h>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Value>
Value Take(const blend::Result<Value>& result) {
  if (!result.HasValue()) {
    throw std::runtime_error(result.GetError().code + ": " + result.GetError().message);
  }
  return result.GetValue();
}

blend::Scene Roots(const std::vector<std::string>& names) {
  blend::Scene scene;
  for (const auto& name : names) {
    blend::Object object;
    object.sourceName = name;
    scene.objects.push_back(std::move(object));
  }
  return scene;
}

void CheckExamples() {
  const std::vector<std::pair<std::string, std::string>> examples = {
      {"Cube", "Cube"}, {"Cube.001", "Cube_001"}, {"A/B", "A_B"},
      {"3D Text", "_3D_Text"}, {"", "Object"}, {"///", "Object"},
      {"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", "Object"},
      {"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"
       "2",
          "_2"},
      {"A... /B///", "A_B"}, {"_Cube__", "_Cube__"},
      {"A/__B", "A___B"}, {"A__/__", "A_____"}, {"_///", "_"},
      {"9///", "_9"}, {"A\x7f", "A"}, {"///Cube", "_Cube"}};
  for (const auto& [source, expected] : examples) {
    const auto scene = Roots({source});
    const auto identifiers = blend::ObjectIdentifiers(scene);
    Require(identifiers.HasValue() && identifiers.Diagnostics().empty() &&
                identifiers.GetValue() == std::vector<std::string>{expected},
        "Contract examples preserve literal underscores and collapse only produced runs");
    Require(scene.objects[0].sourceName == source && scene.objects[0].identifier.empty(),
        "Naming does not mutate the source Scene");
  }
  Require(Take(blend::ObjectIdentifiers({})).empty(), "An empty Scene has no identifiers");
}

void CheckCollisions() {
  auto scene = Roots({"Cube_001", "Cube.001", "A_B_1", "A_B", "A/B", "A B",
      "~", "\xc3\xa9", "\xe6\x97\xa5", "Object", "mesh"});
  const auto names = Take(blend::ObjectIdentifiers(scene));
  Require(names == std::vector<std::string>{"Cube_001_1", "Cube_001", "A_B_1_1",
                       "A_B_2", "A_B_1", "A_B", "Object_1", "Object_2", "Object_3", "Object", "mesh"},
      "Source byte ordering, natural suffixes and smallest free collision suffixes");
  scene.meshes.emplace_back();
  scene.objects[0].mesh = 0;
  Require(Take(blend::ObjectIdentifiers(scene)) == names,
      "A root Mesh does not reserve its data child name among root Objects");
  auto hierarchy = Roots({"Parent", "mesh", "mesh.1", "mesh_1", "other", "Empty", "mesh"});
  hierarchy.meshes.emplace_back();
  hierarchy.objects[0].mesh = 0;
  for (const auto index : {1, 2, 3, 4}) {
    hierarchy.objects[index].parent = 0;
  }
  hierarchy.objects[6].parent = 5;
  Require(Take(blend::ObjectIdentifiers(hierarchy)) ==
              std::vector<std::string>{"Parent", "mesh_1", "mesh_1_1", "mesh_1_2", "other", "Empty", "mesh"},
      "Mesh child names are reserved only under Mesh parents; Empty parents have no data child");
  hierarchy.objects[1].mesh = 0;
  auto grandchild = blend::Object{};
  grandchild.sourceName = "mesh";
  grandchild.parent = 1;
  hierarchy.objects.push_back(grandchild);
  Require(Take(blend::ObjectIdentifiers(hierarchy)).back() == "mesh_1",
      "Fixed child reservation also applies to nested Mesh parents");
}

void CheckOrder() {
  auto scene = Roots({"Cube_001", "Cube.001", "mesh", "Parent", "~", "\xc3\xa9", "mesh_1", "mesh.1"});
  scene.meshes.emplace_back();
  scene.objects[3].mesh = 0;
  for (const auto index : {2, 6, 7}) {
    scene.objects[index].parent = 3;
  }
  const auto expected = Take(blend::ObjectIdentifiers(scene));
  std::vector<std::size_t> order(scene.objects.size());
  std::iota(order.begin(), order.end(), 0);
  do {
    auto reordered = scene;
    for (std::size_t index = 0; index < order.size(); ++index) {
      reordered.objects[index] = scene.objects[order[index]];
      if (reordered.objects[index].parent) {
        reordered.objects[index].parent = static_cast<std::size_t>(
            std::find(order.begin(), order.end(), *reordered.objects[index].parent) - order.begin());
      }
      reordered.objects[index].identifier = "stale";
    }
    const auto actual = Take(blend::ObjectIdentifiers(reordered));
    for (std::size_t index = 0; index < order.size(); ++index) {
      Require(actual[index] == expected[order[index]],
          "Every Object permutation produces the same identifiers with remapped parents");
    }
  } while (std::next_permutation(order.begin(), order.end()));
  Require(Take(blend::ObjectIdentifiers(scene)) == expected, "Repeated naming is deterministic");
}

void CheckDisplay() {
  const std::string replacement = "\xef\xbf\xbd";
  const std::string valid = "A\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xef\xbf\xbd\xf0\x90\x80\x80\xf4\x8f\xbf\xbf";
  const auto unchanged = blend::NameForDisplay(valid);
  Require(unchanged.HasValue() && unchanged.GetValue() == valid && unchanged.Diagnostics().empty(),
      "UTF-8 scalar boundaries and an existing replacement character survive unchanged");
  const std::vector<std::pair<std::string, std::string>> invalid = {
      {"\x80", replacement}, {"\xc0\xaf", replacement + replacement},
      {"\xc1\xbf", replacement + replacement}, {"\xf5\x80", replacement + replacement},
      {"\xff", replacement}, {"\xc2", replacement}, {"\xe2\x82", replacement},
      {"\xf0\x90\x80", replacement}, {"\xe2\x82"
                                      "A",
                                         replacement + "A"},
      {"\xc3(", replacement + "("}, {"\xe0\x9f\xbf", replacement + replacement + replacement},
      {"\xed\xa0\x80", replacement + replacement + replacement},
      {"\xf0\x8f\xbf\xbf", replacement + replacement + replacement + replacement},
      {"\xf4\x90\x80\x80", replacement + replacement + replacement + replacement},
      {"A\xff"
       "B\xc2"
       "C",
          "A" + replacement + "B" + replacement + "C"}};
  for (const auto& [source, expected] : invalid) {
    const auto display = blend::NameForDisplay(source);
    Require(display.HasValue() && display.GetValue() == expected && display.Diagnostics().size() == 1,
        "Each maximal ill-formed UTF-8 subpart becomes a replacement without losing valid text");
    const auto& diagnostic = display.Diagnostics()[0];
    Require(diagnostic.code == "BLEND_NAME_INVALID_UTF8" && diagnostic.severity == blend::Severity::Warning &&
                diagnostic.recoverable && diagnostic.datablock == source &&
                !diagnostic.blockIndex && !diagnostic.byteOffset,
        "Display replacement reports one recoverable warning with raw name context");
    const auto scene = Roots({source});
    const auto names = blend::ObjectIdentifiers(scene);
    Require(names.HasValue() && names.Diagnostics().size() == 1 &&
                names.Diagnostics()[0].code == "BLEND_NAME_INVALID_UTF8" &&
                scene.objects[0].sourceName == source,
        "Object naming warns about malformed display text but retains source bytes");
    Require(!names.GetValue()[0].empty() &&
                std::all_of(names.GetValue()[0].begin(), names.GetValue()[0].end(), [](char value) {
                  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
                         (value >= '0' && value <= '9') || value == '_';
                }) &&
                !(names.GetValue()[0][0] >= '0' && names.GetValue()[0][0] <= '9'),
        "Malformed source names still produce valid ASCII identifiers");
  }
}

void CheckFailures() {
  auto duplicate = Roots({"Same", "Same"});
  const auto repeated = blend::ObjectIdentifiers(duplicate);
  Require(!repeated.HasValue() && repeated.GetError().code == "BLEND_NAME_DUPLICATE" &&
              repeated.GetError().severity == blend::Severity::Fatal && !repeated.GetError().recoverable &&
              repeated.GetError().datablock == "Same",
      "Duplicate sibling source names fail rather than using an unstable tie-break");
  for (const bool mesh : {false, true}) {
    auto invalid = Roots({"Invalid"});
    if (mesh) {
      invalid.objects[0].mesh = 0;
    } else {
      invalid.objects[0].parent = 1;
    }
    const auto result = blend::ObjectIdentifiers(invalid);
    Require(!result.HasValue() && result.GetError().code == "BLEND_SCENE_REFERENCE_INVALID" &&
                result.GetError().severity == blend::Severity::Fatal && !result.GetError().recoverable &&
                result.GetError().datablock == "Invalid",
        "Out-of-range IR references fail explicitly before fixed-child lookup");
  }
  duplicate.objects[0].parent = 0;
  Require(!blend::ObjectIdentifiers(duplicate).HasValue(), "A self-parent is not a naming scope");
}

} // namespace

int main() {
  try {
    CheckExamples();
    CheckCollisions();
    CheckOrder();
    CheckDisplay();
    CheckFailures();
    std::cout << "Deterministic Object naming and UTF-8 display checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
