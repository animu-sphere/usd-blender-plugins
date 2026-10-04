#include <blendScene/Naming.h>

#include <algorithm>
#include <map>
#include <new>
#include <set>
#include <stdexcept>

namespace blend {
namespace {

Diagnostic AllocationFailure() {
  return {"BLEND_NAME_ALLOCATION", Severity::Fatal,
      "Unable to allocate scene names", {}, {}, {}, false};
}

std::string Identifier(std::string_view name) {
  std::string result;
  bool produced = false;
  for (const auto character : name) {
    if ((character >= 'A' && character <= 'Z') ||
        (character >= 'a' && character <= 'z') ||
        (character >= '0' && character <= '9') || character == '_') {
      result.push_back(character);
      produced = false;
    } else {
      if (!produced) {
        result.push_back('_');
      }
      produced = true;
    }
  }
  if (produced) {
    result.pop_back();
  }
  if (result.empty()) {
    return "Object";
  }
  if (result.front() >= '0' && result.front() <= '9') {
    result.insert(result.begin(), '_');
  }
  return result;
}

bool ByteLess(std::string_view left, std::string_view right) {
  return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
      [](char first, char second) {
        return static_cast<unsigned char>(first) < static_cast<unsigned char>(second);
      });
}

} // namespace

Result<std::string> NameForDisplay(std::string_view sourceName) {
  try {
    std::string display;
    bool invalid = false;
    for (std::size_t offset = 0; offset < sourceName.size();) {
      const auto first = static_cast<unsigned char>(sourceName[offset]);
      if (first < 0x80) {
        display.push_back(sourceName[offset++]);
        continue;
      }
      const std::size_t width = first >= 0xc2 && first <= 0xdf   ? 2
                                : first >= 0xe0 && first <= 0xef ? 3
                                : first >= 0xf0 && first <= 0xf4 ? 4
                                                                 : 0;
      std::size_t prefix = 1;
      while (prefix < width && prefix < sourceName.size() - offset) {
        const auto next = static_cast<unsigned char>(sourceName[offset + prefix]);
        if (next < 0x80 || next > 0xbf ||
            (prefix == 1 && ((first == 0xe0 && next < 0xa0) ||
                                (first == 0xed && next > 0x9f) ||
                                (first == 0xf0 && next < 0x90) ||
                                (first == 0xf4 && next > 0x8f)))) {
          break;
        }
        ++prefix;
      }
      if (width != 0 && prefix == width) {
        display.append(sourceName.substr(offset, width));
      } else {
        display.append("\xef\xbf\xbd");
        invalid = true;
      }
      offset += prefix;
    }
    std::vector<Diagnostic> diagnostics;
    if (invalid) {
      diagnostics.push_back({"BLEND_NAME_INVALID_UTF8", Severity::Warning,
          "Invalid UTF-8 is replaced with U+FFFD for display; source bytes are retained",
          {}, {}, std::string(sourceName), true});
    }
    return Result<std::string>(std::move(display), std::move(diagnostics));
  } catch (const std::bad_alloc&) {
    return Result<std::string>(AllocationFailure());
  } catch (const std::length_error&) {
    return Result<std::string>(AllocationFailure());
  }
}

Result<std::vector<std::string>> ObjectIdentifiers(const Scene& scene) {
  try {
    std::map<std::optional<std::size_t>, std::vector<std::size_t>> siblings;
    for (std::size_t index = 0; index < scene.objects.size(); ++index) {
      const auto& object = scene.objects[index];
      if ((object.parent && (*object.parent >= scene.objects.size() || *object.parent == index)) ||
          (object.mesh && *object.mesh >= scene.meshes.size())) {
        return Result<std::vector<std::string>>(Diagnostic{"BLEND_SCENE_REFERENCE_INVALID", Severity::Fatal,
            "Naming requires valid Object parent and Mesh indices",
            {}, {}, object.sourceName, false});
      }
      siblings[object.parent].push_back(index);
    }
    std::vector<std::string> identifiers(scene.objects.size());
    std::vector<Diagnostic> diagnostics;
    for (auto& [parent, children] : siblings) {
      std::sort(children.begin(), children.end(), [&](std::size_t left, std::size_t right) {
        return ByteLess(scene.objects[left].sourceName, scene.objects[right].sourceName);
      });
      std::set<std::string> used;
      if (parent && scene.objects[*parent].mesh) {
        used.insert("mesh");
      }
      std::map<std::string, std::size_t> nextSuffix;
      std::optional<std::size_t> previous;
      for (const auto index : children) {
        const auto& name = scene.objects[index].sourceName;
        if (previous && scene.objects[*previous].sourceName == name) {
          return Result<std::vector<std::string>>(Diagnostic{"BLEND_NAME_DUPLICATE", Severity::Fatal,
              "Sibling Object source names must be unique; no enumeration-based tie-break is used",
              {}, {}, name, false});
        }
        previous = index;
        const auto display = NameForDisplay(name);
        if (!display.HasValue()) {
          return Result<std::vector<std::string>>(display.GetError());
        }
        diagnostics.insert(diagnostics.end(), display.Diagnostics().begin(), display.Diagnostics().end());
        const auto base = Identifier(name);
        auto candidate = base;
        if (used.contains(candidate)) {
          auto& suffix = nextSuffix[base];
          do {
            candidate = base + "_" + std::to_string(++suffix);
          } while (used.contains(candidate));
        }
        used.insert(candidate);
        identifiers[index] = std::move(candidate);
      }
    }
    return Result<std::vector<std::string>>(std::move(identifiers), std::move(diagnostics));
  } catch (const std::bad_alloc&) {
    return Result<std::vector<std::string>>(AllocationFailure());
  } catch (const std::length_error&) {
    return Result<std::vector<std::string>>(AllocationFailure());
  }
}

} // namespace blend
