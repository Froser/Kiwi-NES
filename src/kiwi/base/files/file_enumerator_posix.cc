// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is used for debugging assertion support.  The Lock class
// is functionally a wrapper around the LockImpl class, so the only
// real intelligence in the class is in the debugging logic.

#include "base/files/file_enumerator.h"

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdint.h>
#include <string.h>

#include "base/check.h"

namespace kiwi::base {
namespace {

void GetStat(const FilePath& path, bool show_links, stat_wrapper_t* st) {
  DCHECK(st);
  const int res = show_links ? File::Lstat(path.value().c_str(), st)
                             : File::Stat(path.value().c_str(), st);
  if (res < 0) {
    // Print the stat() error message unless it was ENOENT and we're following
    // symlinks.
    if (errno != ENOENT || show_links) {
    }
    LOG(ERROR) << "Cannot stat '" << path << "'";
    memset(st, 0, sizeof(*st));
  }
}

bool ShouldShowSymLinks(int file_type) {
  return file_type & FileEnumerator::SHOW_SYM_LINKS;
}

bool ShouldTrackVisitedDirectories(int file_type) {
  return !(file_type & FileEnumerator::SHOW_SYM_LINKS);
}

}  // namespace

// FileEnumerator::FileInfo ----------------------------------------------------

FileEnumerator::FileInfo::FileInfo() {
  memset(&stat_, 0, sizeof(stat_));
}

bool FileEnumerator::FileInfo::IsDirectory() const {
  return S_ISDIR(stat_.st_mode);
}

FilePath FileEnumerator::FileInfo::GetName() const {
  return filename_;
}

int64_t FileEnumerator::FileInfo::GetSize() const {
  return stat_.st_size;
}

// FileEnumerator --------------------------------------------------------------

FileEnumerator::FileEnumerator(const FilePath& root_path,
                               bool recursive,
                               int file_type)
    : FileEnumerator(root_path,
                     recursive,
                     file_type,
                     FilePath::StringType(),
                     FolderSearchPolicy::MATCH_ONLY) {}

FileEnumerator::FileEnumerator(const FilePath& root_path,
                               bool recursive,
                               int file_type,
                               const FilePath::StringType& pattern)
    : FileEnumerator(root_path,
                     recursive,
                     file_type,
                     pattern,
                     FolderSearchPolicy::MATCH_ONLY) {}

FileEnumerator::FileEnumerator(const FilePath& root_path,
                               bool recursive,
                               int file_type,
                               const FilePath::StringType& pattern,
                               FolderSearchPolicy folder_search_policy)
    : FileEnumerator(root_path,
                     recursive,
                     file_type,
                     pattern,
                     folder_search_policy,
                     ErrorPolicy::IGNORE_ERRORS) {}

FileEnumerator::FileEnumerator(const FilePath& root_path,
                               bool recursive,
                               int file_type,
                               const FilePath::StringType& pattern,
                               FolderSearchPolicy folder_search_policy,
                               ErrorPolicy error_policy)
    : current_directory_entry_(0),
      root_path_(root_path),
      recursive_(recursive),
      file_type_(file_type),
      pattern_(pattern),
      folder_search_policy_(folder_search_policy),
      error_policy_(error_policy) {
  // INCLUDE_DOT_DOT must not be specified if recursive.
  DCHECK(!(recursive && (INCLUDE_DOT_DOT & file_type_)));

  if (file_type_ & FileType::NAMES_ONLY) {
    DCHECK(!recursive_);
    DCHECK_EQ(file_type_ & ~(FileType::NAMES_ONLY | FileType::INCLUDE_DOT_DOT),
              0);
    file_type_ |= (FileType::FILES | FileType::DIRECTORIES);
  }

  if (recursive && ShouldTrackVisitedDirectories(file_type_)) {
    stat_wrapper_t st;
    GetStat(root_path, false, &st);
    visited_directories_.insert(st.st_ino);
  }

  pending_paths_.push(root_path);
}

FileEnumerator::~FileEnumerator() = default;

FilePath FileEnumerator::Next() {
  ++current_directory_entry_;

  // While we've exhausted the entries in the current directory, do the next
  while (current_directory_entry_ >= directory_entries_.size()) {
    if (pending_paths_.empty())
      return FilePath();

    root_path_ = pending_paths_.top();
    root_path_ = root_path_.StripTrailingSeparators();
    pending_paths_.pop();

    DIR* dir = opendir(root_path_.value().c_str());
    if (!dir) {
      if (errno == 0 || error_policy_ == ErrorPolicy::IGNORE_ERRORS)
        continue;
      error_ = File::OSErrorToFileError(errno);
      return FilePath();
    }

    directory_entries_.clear();

    current_directory_entry_ = 0;
    struct dirent* dent;
    // NOTE: Per the readdir() documentation, when the end of the directory is
    // reached with no errors, null is returned and errno is not changed.
    // Therefore we must reset errno to zero before calling readdir() if we
    // wish to know whether a null result indicates an error condition.
    while (errno = 0, dent = readdir(dir)) {
      FileInfo info;
      info.filename_ = FilePath(dent->d_name);

      if (ShouldSkip(info.filename_))
        continue;

      const bool is_pattern_matched = IsPatternMatched(info.filename_);

      // MATCH_ONLY policy enumerates files and directories which matching
      // pattern only. So we can early skip further checks.
      if (folder_search_policy_ == FolderSearchPolicy::MATCH_ONLY &&
          !is_pattern_matched)
        continue;

      // Do not call OS stat/lstat if there is no sense to do it. If pattern is
      // not matched (file will not appear in results) and search is not
      // recursive (possible directory will not be added to pending paths) -
      // there is no sense to obtain item below.
      if (!recursive_ && !is_pattern_matched)
        continue;

      // If the caller only wants the names of files and directories, then
      // continue without populating `info` further.
      if (file_type_ & FileType::NAMES_ONLY) {
        directory_entries_.push_back(std::move(info));
        continue;
      }

      const FilePath full_path = root_path_.Append(info.filename_);
      GetStat(full_path, ShouldShowSymLinks(file_type_), &info.stat_);

      const bool is_dir = info.IsDirectory();

      // Recursive mode: schedule traversal of a directory if either
      // SHOW_SYM_LINKS is on or we haven't visited the directory yet.
      if (recursive_ && is_dir &&
          (!ShouldTrackVisitedDirectories(file_type_) ||
           visited_directories_.insert(info.stat_.st_ino).second)) {
        pending_paths_.push(full_path);
      }

      if (is_pattern_matched && IsTypeMatched(is_dir))
        directory_entries_.push_back(std::move(info));
    }
    int readdir_errno = errno;
    closedir(dir);
    if (readdir_errno != 0 && error_policy_ != ErrorPolicy::IGNORE_ERRORS) {
      error_ = File::OSErrorToFileError(readdir_errno);
      return FilePath();
    }

    // MATCH_ONLY policy enumerates files in matched subfolders by "*" pattern.
    // ALL policy enumerates files in all subfolders by origin pattern.
    if (folder_search_policy_ == FolderSearchPolicy::MATCH_ONLY)
      pattern_.clear();
  }

  return root_path_.Append(
      directory_entries_[current_directory_entry_].filename_);
}

FileEnumerator::FileInfo FileEnumerator::GetInfo() const {
  DCHECK(!(file_type_ & FileType::NAMES_ONLY));
  return directory_entries_[current_directory_entry_];
}

bool FileEnumerator::IsPatternMatched(const FilePath& path) const {
  return pattern_.empty() ||
         !fnmatch(pattern_.c_str(), path.value().c_str(), FNM_NOESCAPE);
}

}  // namespace kiwi::base
