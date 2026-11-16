#include "../benchmarks/workloads.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

int main()
try {
  const auto path = std::filesystem::temp_directory_path()
      / ("sst-data-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directory(path)) throw std::runtime_error("Cannot create fixture directory");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
  } cleanup{path};
  const auto write = [&](const std::string& name, const std::vector<unsigned char>& bytes) {
    std::ofstream output(path / name, std::ios::binary);
    for (auto byte : bytes) output.put(static_cast<char>(byte));
    if (!output) throw std::runtime_error("Cannot write fixture");
  };
  write("valid", {0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 128, 4, 3, 2, 1});
  if (workloads::read_keys((path / "valid").string())
      != workloads::Keys{0, 0xffffffffU, 0x80000000U, 0x01020304U}) {
    throw std::runtime_error("Little-endian decoder mismatch");
  }
  write("empty", {});
  write("short", {1, 2, 3});
  write("truncated", {1, 2, 3, 4, 5});
  for (const auto& name : {"empty", "short", "truncated", "missing"}) {
    bool rejected = false;
    try { (void)workloads::read_keys((path / name).string()); }
    catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Invalid dataset accepted");
  }
  const auto keys = workloads::make_keys(1000, "dense", 1);
  const auto forward = workloads::make_queries(keys, 4096, "sorted", 7);
  const auto backward = workloads::make_queries(keys, 4096, "reverse", 7);
  auto reversed = forward;
  std::reverse(reversed.begin(), reversed.end());
  if (!std::is_sorted(forward.begin(), forward.end()) || backward != reversed) {
    throw std::runtime_error("Ordered query generator mismatch");
  }
  bool rejected = false;
  try { (void)workloads::make_queries({}, 100, "mixed", 1); }
  catch (const std::runtime_error&) { rejected = true; }
  if (!rejected) throw std::runtime_error("Empty query source accepted");
  std::cout << "PASS binary dataset decoding, malformed inputs, ordered query generation\n";
} catch (const std::exception& error) {
  std::cerr << "FAIL: " << error.what() << '\n';
  return 1;
}
