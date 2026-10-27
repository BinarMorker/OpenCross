#include "vfs/vfs.h"

#include <miniz.h>

#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <unordered_map>

#include "core/log.h"

namespace oc::vfs {

std::string normalise_path(std::string_view path) {
  std::string out(path);
  for (char& ch : out) {
    if (ch == '\\') ch = '/';
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  while (!out.empty() && out.front() == '/') out.erase(out.begin());
  return out;
}

namespace {

std::optional<Bytes> read_loose(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    log::warn("vfs: cannot read mod file {}", file.string());
    return std::nullopt;
  }
  return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

std::pair<std::string, std::string> split_archive_path(std::string_view path) {
  std::string normalised = normalise_path(path);
  size_t slash = normalised.find('/');
  if (slash == std::string::npos) return {normalised, ""};
  return {normalised.substr(0, slash), normalised.substr(slash + 1)};
}

// One ZIP archive. miniz keeps the central directory in memory; member reads go through the file on demand.
class Archive {
 public:
  explicit Archive(const std::filesystem::path& file) {
    std::memset(&zip_, 0, sizeof(zip_));
    ok_ = mz_zip_reader_init_file(&zip_, file.string().c_str(), 0) != 0;
    if (!ok_) {
      log::warn("vfs: cannot open archive {}", file.string());
      return;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip_);
    names_.reserve(count);
    for (mz_uint i = 0; i < count; ++i) {
      char name[1024];
      mz_zip_reader_get_filename(&zip_, i, name, sizeof(name));
      names_.emplace_back(name);
      index_.emplace(normalise_path(name), i);
    }
  }
  ~Archive() {
    if (ok_) mz_zip_reader_end(&zip_);
  }
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;

  bool ok() const { return ok_; }
  const std::vector<std::string>& names() const { return names_; }

  bool contains(const std::string& member) const { return index_.contains(member); }

  std::optional<Bytes> read(const std::string& member) const {
    auto it = index_.find(member);
    if (it == index_.end()) return std::nullopt;
    std::lock_guard lock(mutex_);
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(const_cast<mz_zip_archive*>(&zip_), it->second, &stat)) return std::nullopt;
    Bytes out(static_cast<size_t>(stat.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(const_cast<mz_zip_archive*>(&zip_), it->second, out.data(), out.size(), 0)) {
      log::warn("vfs: extract failed for {}", member);
      return std::nullopt;
    }
    return out;
  }

 private:
  mz_zip_archive zip_{};
  bool ok_ = false;
  std::vector<std::string> names_;
  std::unordered_map<std::string, mz_uint> index_;
  mutable std::mutex mutex_;
};

// `archives`: archive name (or "mod|<file>" for a mod's replacement) -> the opened archive, nullptr = known missing.
struct Vfs::Impl {
  mutable std::mutex mutex;
  mutable std::map<std::string, std::unique_ptr<Archive>> archives;
  std::shared_ptr<const Overlay> overlay = std::make_shared<Overlay>();
};

Vfs::Vfs() : impl_(std::make_unique<Impl>()) {}
Vfs::~Vfs() = default;

bool Vfs::open(const std::filesystem::path& game_dir) {
  game_dir_ = game_dir;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(game_dir / "data" / "cdrom.dat", ec)) {
    log::error("vfs: {} does not contain data\\cdrom.dat", game_dir.string());
    return false;
  }
  impl_->archives.clear();
  return true;
}

void Vfs::set_overlay(Overlay overlay) {
  auto next = std::make_shared<const Overlay>(std::move(overlay));
  std::lock_guard lock(impl_->mutex);
  if (next->files != impl_->overlay->files || next->archives != impl_->overlay->archives || next->textures != impl_->overlay->textures ||
      next->enhance != impl_->overlay->enhance)
    ++generation_;
  impl_->overlay = std::move(next);
}

std::string Vfs::cache_key(std::string_view path) const {
  const std::uint32_t gen = generation_.load();
  return gen ? std::string(path) + "|mods" + std::to_string(gen) : std::string(path);
}

std::shared_ptr<const Overlay> Vfs::overlay() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->overlay;
}

// Unless `base`, a mod's whole-archive replacement is tried first; it falls back to the shipped archive if it cannot be opened.
Archive* Vfs::archive(std::string_view name, bool base) const {
  std::lock_guard lock(impl_->mutex);
  std::string key(name);
  if (!base) {
    auto replacement = impl_->overlay->archives.find(key);
    if (replacement != impl_->overlay->archives.end()) {
      const std::string mod_key = "mod|" + replacement->second.string();
      auto mod_it = impl_->archives.find(mod_key);
      if (mod_it == impl_->archives.end()) {
        auto opened = std::make_unique<Archive>(replacement->second);
        if (!opened->ok()) opened.reset();
        else log::info("vfs: {} replaced by {}", key, replacement->second.string());
        mod_it = impl_->archives.emplace(mod_key, std::move(opened)).first;
      }
      if (mod_it->second) return mod_it->second.get();
    }
  }
  auto it = impl_->archives.find(key);
  if (it != impl_->archives.end()) return it->second.get();
  auto file = game_dir_ / "data" / (key + ".dat");
  std::unique_ptr<Archive> opened;
  std::error_code ec;
  if (std::filesystem::is_regular_file(file, ec)) {
    opened = std::make_unique<Archive>(file);
    if (!opened->ok()) opened.reset();
  }
  Archive* raw = opened.get();
  impl_->archives.emplace(key, std::move(opened));
  return raw;
}

// RDE tries data\cdrom2\X before data\cdrom\X for every file in HD mode, and for a fixed list of files in original
// mode (openfont.tim, cmptitle.tim, map/window/*, ending.ctd).
bool Vfs::cdrom2_first(const std::string& member) const {
  if (hd_mode_) return true;
  if (member == "map/etc/openfont.tim" || member == "menu/title/cmptitle.tim" || member == "map/mapbin/ending.ctd") return true;
  return member.rfind("map/window/", 0) == 0;
}

std::optional<Bytes> Vfs::read(std::string_view path) const {
  auto [arc, member] = split_archive_path(path);
  if (auto ov = overlay(); !ov->files.empty()) {
    auto it = ov->files.find(arc + "/" + member);
    if (it != ov->files.end()) {
      if (auto bytes = read_loose(it->second)) return bytes;
    }
  }
  if (arc == "cdrom" && cdrom2_first(member)) {
    if (Archive* cdrom2 = archive("cdrom2")) {
      if (auto bytes = cdrom2->read(member)) return bytes;
    }
  }
  Archive* shipped = archive(arc);
  if (!shipped) return std::nullopt;
  return shipped->read(member);
}

std::optional<Bytes> Vfs::read_disc(std::string_view path) const {
  auto [arc, member] = split_archive_path(path);
  if (arc != "cdrom") return read(path);
  if (auto ov = overlay(); !ov->files.empty()) {
    auto it = ov->files.find(arc + "/" + member);
    if (it != ov->files.end()) {
      if (auto bytes = read_loose(it->second)) return bytes;
    }
  }
  Archive* shipped = archive(arc);
  if (!shipped) return std::nullopt;
  return shipped->read(member);
}

bool Vfs::exists(std::string_view path) const {
  auto [arc, member] = split_archive_path(path);
  if (auto ov = overlay(); ov->files.contains(arc + "/" + member)) return true;
  if (arc == "cdrom" && cdrom2_first(member)) {
    if (Archive* cdrom2 = archive("cdrom2"); cdrom2 && cdrom2->contains(member)) return true;
  }
  Archive* shipped = archive(arc);
  return shipped && shipped->contains(member);
}

std::optional<std::filesystem::path> Vfs::loose_file(std::string_view path) const {
  auto [arc, member] = split_archive_path(path);
  auto ov = overlay();
  auto it = ov->files.find(arc + "/" + member);
  if (it == ov->files.end()) return std::nullopt;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(it->second, ec)) return std::nullopt;
  return it->second;
}

bool Vfs::has_textures() const { return !overlay()->textures.empty(); }

bool Vfs::enhanced() const { return overlay()->enhance; }

std::optional<std::filesystem::path> Vfs::texture_file(std::string_view canonical_path) const {
  auto ov = overlay();
  if (ov->textures.empty()) return std::nullopt;
  auto it = ov->textures.find(std::string(canonical_path));
  if (it == ov->textures.end()) return std::nullopt;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(it->second, ec)) return std::nullopt;
  return it->second;
}

std::optional<std::filesystem::path> Vfs::texture_file_matching(std::string_view prefix, std::string_view suffix) const {
  auto ov = overlay();
  const std::string* best = nullptr;
  const std::filesystem::path* file = nullptr;
  for (const auto& [target, path] : ov->textures)
    if (target.size() >= prefix.size() + suffix.size() && target.starts_with(prefix) && target.ends_with(suffix) && (!best || target < *best)) best = &target, file = &path;
  std::error_code ec;
  if (!file || !std::filesystem::is_regular_file(*file, ec)) return std::nullopt;
  return *file;
}

std::vector<std::string> Vfs::list(std::string_view name) const {
  Archive* arc = archive(name);
  return arc ? arc->names() : std::vector<std::string>{};
}

std::vector<std::string> Vfs::list_base(std::string_view name) const {
  Archive* arc = archive(name, true);
  return arc ? arc->names() : std::vector<std::string>{};
}

}  // namespace oc::vfs
