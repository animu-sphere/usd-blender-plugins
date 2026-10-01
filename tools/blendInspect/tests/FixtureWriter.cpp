#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 3) {
    return 1;
  }
  std::ifstream input(std::filesystem::u8path(argv[1]), std::ios::binary);
  const std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad() || bytes.size() != 188558) {
    return 1;
  }
  const auto directory = std::filesystem::u8path(argv[2]);
  const auto write = [&](const char* name, const std::vector<char>& contents) {
    std::ofstream output(directory / name, std::ios::binary);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    return output.good();
  };
  auto changed = bytes;
  changed[53925] = 'X';
  if (!write("missing-dna.blend", changed)) {
    return 1;
  }
  changed = bytes;
  changed.insert(changed.begin() + 188526, bytes.begin() + 53922, bytes.begin() + 188526);
  if (!write("duplicate-dna.blend", changed)) {
    return 1;
  }
  changed = bytes;
  std::copy_n("FAIL", 4, changed.begin() + 53954);
  return write("invalid-dna.blend", changed) ? 0 : 1;
}