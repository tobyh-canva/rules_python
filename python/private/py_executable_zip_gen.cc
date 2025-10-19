// Copyright 2025 The Bazel Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Wrapper for zipper that processes Python zip manifest generation.
//
// This native C++ tool replaces the shell script wrapper, providing the same
// simplified Starlark API while maintaining native performance. It eliminates
// the need for to_list() calls in Starlark by processing file lists at
// execution time.
//
// Performance characteristics:
// - Zero per-target overhead vs direct zipper usage (within measurement noise)
// - One-time compilation cost (~1-2s per workspace)
// - Uses std::filesystem for robust path manipulation (C++17)

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "tools/cpp/runfiles/runfiles.h"

using bazel::tools::cpp::runfiles::Runfiles;
namespace fs = std::filesystem;

// Path manipulation utilities using std::filesystem (C++17)
namespace path {

// Normalize a path (remove "../" and "." components)
// Uses std::filesystem::path::lexically_normal() for correctness
std::string normalize(const std::string& p) {
  return fs::path(p).lexically_normal().string();
}

// Remove prefix from path
std::string relativize(const std::string& path, const std::string& prefix) {
  // Check if path starts with prefix
  if (path.size() >= prefix.size() && 
      path.compare(0, prefix.size(), prefix) == 0) {
    size_t start = prefix.size();
    // Skip leading slash if present
    if (start < path.size() && path[start] == '/') {
      start++;
    }
    return path.substr(start);
  }
  return path;
}

bool starts_with(const std::string& str, const std::string& prefix) {
  return str.size() >= prefix.size() && 
         str.compare(0, prefix.size(), prefix) == 0;
}

} // namespace path

// Get zip runfiles path for a file
std::string get_zip_runfiles_path(const std::string& path,
                                   const std::string& workspace_name,
                                   bool legacy_external_runfiles) {
  std::string zip_runfiles_path;
  
  if (legacy_external_runfiles && path::starts_with(path, "external/")) {
    // Remove "external/" prefix
    zip_runfiles_path = path::relativize(path, "external");
  } else {
    // Normalize workspace_name/path
    std::string combined = workspace_name + "/" + path;
    zip_runfiles_path = path::normalize(combined);
  }
  
  return "runfiles/" + zip_runfiles_path;
}

// Parse a file entry in "short_path=disk_path" or "short_path=" format
struct FileEntry {
  std::string short_path;
  std::string disk_path;
  bool is_empty;
  
  static FileEntry parse(const std::string& line) {
    FileEntry entry;
    size_t eq = line.find('=');
    if (eq == std::string::npos) {
      std::cerr << "ERROR: Invalid file entry (no '='): " << line << std::endl;
      std::exit(1);
    }
    
    entry.short_path = line.substr(0, eq);
    entry.disk_path = line.substr(eq + 1);
    entry.is_empty = entry.disk_path.empty();
    
    return entry;
  }
};

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <params_file>" << std::endl;
    return 1;
  }
  
  std::string params_file = argv[1];
  
  // Parse arguments from params file
  std::string output;
  std::string workspace_name;
  std::string main_file;
  std::string repo_mapping_manifest;
  bool legacy_external_runfiles = false;
  std::vector<FileEntry> files;
  
  std::ifstream in(params_file);
  if (!in) {
    std::cerr << "ERROR: Cannot open params file: " << params_file << std::endl;
    return 1;
  }
  
  std::string line;
  bool parsing_positional = false;
  
  while (std::getline(in, line)) {
    // Skip empty lines
    if (line.empty()) continue;
    
    // Check for explicit -- separator
    if (line == "--") {
      parsing_positional = true;
      continue;
    }
    
    // If we've seen --, everything is a positional argument
    if (parsing_positional) {
      files.push_back(FileEntry::parse(line));
      continue;
    }
    
    // Parse flags
    if (line == "--output") {
      if (!std::getline(in, output)) {
        std::cerr << "ERROR: --output requires a value" << std::endl;
        return 1;
      }
    } else if (line == "--workspace-name") {
      if (!std::getline(in, workspace_name)) {
        std::cerr << "ERROR: --workspace-name requires a value" << std::endl;
        return 1;
      }
    } else if (line == "--main-file") {
      if (!std::getline(in, main_file)) {
        std::cerr << "ERROR: --main-file requires a value" << std::endl;
        return 1;
      }
    } else if (line == "--repo-mapping-manifest") {
      if (!std::getline(in, repo_mapping_manifest)) {
        std::cerr << "ERROR: --repo-mapping-manifest requires a value" << std::endl;
        return 1;
      }
    } else if (line == "--legacy-external-runfiles") {
      legacy_external_runfiles = true;
    } else {
      // Positional argument (file entry)
      files.push_back(FileEntry::parse(line));
    }
  }
  
  in.close();
  
  // Validate required arguments
  if (output.empty()) {
    std::cerr << "ERROR: --output is required" << std::endl;
    return 1;
  }
  if (workspace_name.empty()) {
    std::cerr << "ERROR: --workspace-name is required" << std::endl;
    return 1;
  }
  if (main_file.empty()) {
    std::cerr << "ERROR: --main-file is required" << std::endl;
    return 1;
  }
  
  // Generate zip manifest
  // Order must match main branch for reproducible builds
  std::string manifest_file = "zip_manifest.txt";
  std::ofstream manifest(manifest_file);
  if (!manifest) {
    std::cerr << "ERROR: Cannot create manifest file: " << manifest_file << std::endl;
    return 1;
  }
  
  // 1. Main file
  manifest << "__main__.py=" << main_file << "\n";
  
  // 2. Default empty files
  manifest << "__init__.py=\n";
  manifest << get_zip_runfiles_path("__init__.py", workspace_name, legacy_external_runfiles) << "=\n";
  
  // 3. Process file entries
  for (const auto& file : files) {
    std::string zip_path = get_zip_runfiles_path(file.short_path, workspace_name, legacy_external_runfiles);
    manifest << zip_path << "=" << file.disk_path << "\n";
  }
  
  // 4. Repo mapping manifest (last, to match main branch order)
  if (!repo_mapping_manifest.empty()) {
    manifest << "runfiles/_repo_mapping=" << repo_mapping_manifest << "\n";
  }
  
  manifest.close();
  
  // Find zipper tool via runfiles library
  std::string error;
  std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &error));
  
  if (runfiles == nullptr) {
    std::cerr << "ERROR: Failed to initialize runfiles: " << error << std::endl;
    return 1;
  }
  
  std::string zipper_path = runfiles->Rlocation("bazel_tools/tools/zip/zipper/zipper");
  if (zipper_path.empty()) {
    std::cerr << "ERROR: Could not locate zipper in runfiles" << std::endl;
    return 1;
  }
  
  // Execute zipper
  std::string cmd = zipper_path + " cC " + output + " @" + manifest_file;
  int result = std::system(cmd.c_str());
  
  if (result != 0) {
    std::cerr << "ERROR: zipper failed with exit code " << result << std::endl;
    return 1;
  }
  
  return 0;
}

