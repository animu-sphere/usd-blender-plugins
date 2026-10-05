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

std::string Identifier(std::string_view name, std::string_view fallback) {
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
    return std::string(fallback);
  }
  if (result.front() >= '0' && result.front() <= '9') {
    result.insert(result.begin(), '_');
  }
  return result;
}

Result<std::vector<std::string>> AssignIdentifiers(
    const std::vector<std::string_view>& names, std::string_view fallback,
    std::set<std::string> used, std::optional<std::size_t> renderMap = {}) {
  std::vector<std::size_t> order;
  for (std::size_t index = 0; index < names.size(); ++index) {
    order.push_back(index);
  }
  std::sort(order.begin(), order.end(), [&](auto left, auto right) {
    return SourceNameLess(names[left], names[right]);
  });
  std::vector<std::string> identifiers(names.size());
  std::vector<Diagnostic> diagnostics;
  std::map<std::string, std::size_t> nextSuffix;
  std::optional<std::size_t> previous;
  for (const auto index : order) {
    const auto name = names[index];
    if (previous && names[*previous] == name) {
      return Result<std::vector<std::string>>(Diagnostic{"BLEND_NAME_DUPLICATE", Severity::Fatal,
          "Sibling source names must be unique; no enumeration-based tie-break is used",
          {}, {}, std::string(name), false});
    }
    previous = index;
    const auto display = NameForDisplay(name);
    if (!display.HasValue()) {
      return Result<std::vector<std::string>>(display.GetError());
    }
    diagnostics.insert(diagnostics.end(), display.Diagnostics().begin(), display.Diagnostics().end());
    if (renderMap == index) {
      identifiers[index] = "st";
      continue;
    }
    const auto base = Identifier(name, fallback);
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
  return Result<std::vector<std::string>>(std::move(identifiers), std::move(diagnostics));
}

} // namespace

bool SourceNameLess(std::string_view left, std::string_view right) {
  return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
      [](char first, char second) {
        return static_cast<unsigned char>(first) < static_cast<unsigned char>(second);
      });
}

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
      std::vector<std::string_view> names;
      for (const auto index : children) {
        names.push_back(scene.objects[index].sourceName);
      }
      std::set<std::string> used;
      if (parent && scene.objects[*parent].mesh) {
        used.insert("mesh");
      }
      const auto assigned = AssignIdentifiers(names, "Object", std::move(used));
      if (!assigned.HasValue()) {
        return assigned;
      }
      diagnostics.insert(diagnostics.end(), assigned.Diagnostics().begin(), assigned.Diagnostics().end());
      for (std::size_t index = 0; index < children.size(); ++index) {
        identifiers[children[index]] = assigned.GetValue()[index];
      }
    }
    return Result<std::vector<std::string>>(std::move(identifiers), std::move(diagnostics));
  } catch (const std::bad_alloc&) {
    return Result<std::vector<std::string>>(AllocationFailure());
  } catch (const std::length_error&) {
    return Result<std::vector<std::string>>(AllocationFailure());
  }
}

Result<std::vector<std::string>> UvIdentifiers(const Mesh& mesh) {
  try {
    std::vector<std::string_view> names;
    std::optional<std::size_t> renderMap;
    for (std::size_t index = 0; index < mesh.uvMaps.size(); ++index) {
      const auto& map = mesh.uvMaps[index];
      names.push_back(map.sourceName);
      if (map.activeRender) {
        if (renderMap) {
          return Result<std::vector<std::string>>(Diagnostic{"BLEND_NAME_RENDER_UV_INVALID", Severity::Fatal,
              "A Mesh may have at most one active render UV map", {}, {}, mesh.sourceName, false});
        }
        renderMap = index;
      }
    }
    return AssignIdentifiers(names, "UVMap", {"st"}, renderMap);
  } catch (const std::bad_alloc&) {
    return Result<std::vector<std::string>>(AllocationFailure());
  } catch (const std::length_error&) {
    return Result<std::vector<std::string>>(AllocationFailure());
  }
}

Result<std::vector<std::string>> MaterialIdentifiers(const Scene& scene) {
  try {
    std::vector<std::string_view> names;
    for (const auto& material : scene.materials) {
      names.push_back(material.sourceName);
    }
    return AssignIdentifiers(names, "Material", {});
  } catch (const std::bad_alloc&) {
    return Result<std::vector<std::string>>(AllocationFailure());
  } catch (const std::length_error&) {
    return Result<std::vector<std::string>>(AllocationFailure());
  }
}

} // namespace blend
