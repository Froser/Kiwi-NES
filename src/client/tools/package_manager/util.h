// Copyright (C) 2024 Yisi Yu
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#ifndef UTIL_H_
#define UTIL_H_

#include <SDL.h>
#include <gflags/gflags.h>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"

struct Settings {
  char boxarts_package[1024] = "boxarts.zip";
  char zip_output_path[1024] = {0};

  Settings();
};

Settings& GetSettings();
bool IsZipExtension(const std::string& filename);
bool IsJPEGExtension(const std::string& filename);
bool IsNESExtension(const std::string& filename);

class ROMWindow;
struct ROM {
  friend class ROMWindow;

  static constexpr size_t MAX = 128;

  // Title
  std::string key;
  char zh[MAX]{0};
  char zh_hint[MAX]{0};
  char ja[MAX]{0};
  char ja_hint[MAX]{0};

  // Cover
  std::vector<uint8_t> cover_data;

  std::vector<uint8_t> nes_data;
  char nes_file_name[MAX]{0};

 private:
  SDL_Texture* cover_texture_ = nullptr;
};

using ROMS = std::vector<ROM>;

[[nodiscard]] ROMS ReadZipFromFile(const kiwi::base::FilePath& path);
kiwi::base::FilePath WriteZip(const kiwi::base::FilePath& save_dir,
                              const ROMS& roms);
kiwi::base::FilePath PackZip(const kiwi::base::FilePath& rom_zip,
                             const kiwi::base::FilePath& save_dir);
kiwi::base::FilePath WriteROM(const char* filename,
                              const std::vector<uint8_t>& data,
                              const kiwi::base::FilePath& dir);
bool IsMapperSupported(const std::vector<uint8_t>& nes_data,
                       std::string& mapper_name);

#if BUILDFLAG(IS_WIN)
kiwi::base::FilePath GetFontsPath();
#endif

kiwi::base::FilePath GetDefaultSavePath();
void ShellOpen(const kiwi::base::FilePath& file);
void ShellOpenDirectory(const kiwi::base::FilePath& file);
#if BUILDFLAG(IS_MAC)
void RunExecutable(const kiwi::base::FilePath& bundle,
                   const std::vector<std::string>& args);
#else
void RunExecutable(const kiwi::base::FilePath& executable,
                   const std::vector<std::wstring>& args);
#endif

std::vector<uint8_t> TryFetchCoverImage(const std::string& name,
                                        kiwi::base::FilePath* suggested_url);
std::string TryGetPinyin(const std::string& chinese);
std::string TryGetKana(const std::string& kanji);
std::string TryGetJaTitle(const std::string& en_name);
std::string RemoveROMRegion(const std::string& str);
std::vector<uint8_t> RotateJPEG(std::vector<uint8_t> cover_data);
bool FillRomDetailsAutomatically(ROM& rom,
                                 const kiwi::base::FilePath& filename);
std::vector<uint8_t> ReadImageAsJPGFromClipboard();
std::vector<uint8_t> ReadImageAsJPGFromImageData(int width,
                                                 int height,
                                                 size_t bytes_per_row,
                                                 unsigned char* data);
#endif  // UTIL_H_