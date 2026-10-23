// octext - dialogue/text inspection tool (text importer).
//   octext msg <room> <idx> [lang]   print one message in every form: PS1 stream (as RDE markup), lang.dat row,
//                                    rich-text tokens, flattened tokens. <room> = map file stem or numeric lang.dat id.
//   octext find <text> [lang]        every room message whose lang.dat row contains <text>
//   octext coverage                  per language: message records of every room vs lang.dat rows ({ERROR} placeholders)
// GameDir comes from -g <dir>, the OPENCROSS_GAMEDIR environment variable, or the usual discovery (platform::find_game_dir).
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "import/text.h"
#include "platform/paths.h"
#include "tool_util.h"
#include "vfs/vfs.h"

using namespace oc;
using import::Lang;

namespace {

constexpr int kLangCount = 6;

std::string escape_newlines(const std::string& text) {
  std::string out;
  for (char ch : text) {
    if (ch == '\n') out += "\\n";
    else out.push_back(ch);
  }
  return out;
}

int usage() {
  std::fprintf(stderr, "usage: octext [-g <gamedir>] msg <room> <idx> [lang]\n       octext [-g <gamedir>] coverage\n");
  return 2;
}

// Room ids that share a stem are separate lang.dat rooms; only the requested id is shown.
int cmd_msg(vfs::Vfs& game_fs, const std::string& room_arg, int idx, Lang lang) {
  int room_id = -1;
  char* end = nullptr;
  const long numeric = std::strtol(room_arg.c_str(), &end, 10);
  if (end && *end == '\0' && !room_arg.empty()) room_id = static_cast<int>(numeric);
  else room_id = import::room_id_from_name(room_arg);
  const char* name = import::room_name_for_id(room_id);
  if (!name) {
    std::fprintf(stderr, "unknown room '%s'\n", room_arg.c_str());
    return 1;
  }
  import::JpGlyphs jp_glyphs;
  if (auto exe = game_fs.read("cdrom/prog/slps_023.64")) jp_glyphs.load_bitmaps(*exe);
  import::LangTables tables;
  tables.init(&game_fs);

  std::printf("; room %s (id %d) message %d\n", name, room_id, idx);
  auto block_bytes = import::read_room_dialogue_block(game_fs, name);
  if (block_bytes) {
    auto block = import::parse_dialogue_block(*block_bytes);
    if (block && static_cast<size_t>(idx) < block->records.size()) {
      const auto& record = block->records[static_cast<size_t>(idx)];
      std::printf("[%3d] hdr w1=%d lines=%d px=%d\n", idx, record.width_chars, record.lines, record.width_px);
      std::printf("  JP*: %s\n", import::ps1_to_markup(record.bytes, jp_glyphs).c_str());
      import::Message ps1_message = import::parse_ps1(record.bytes, jp_glyphs);
      std::printf("  PS1 tokens: %s\n", import::describe(ps1_message).c_str());
    } else {
      std::printf("  (no PS1 record %d; block has %zu)\n", idx, block ? block->records.size() : size_t(0));
    }
  } else {
    std::printf("  (room message block unavailable)\n");
  }

  import::LookupInfo info;
  auto row = tables.lookup_field(room_id, idx, lang, 0, &info);
  std::string lang_upper = import::lang_code(lang);
  for (char& ch : lang_upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  if (!row) {
    std::printf("  %s : <no row -> MESSAGE NOT FOUND>\n", lang_upper.c_str());
    return 0;
  }
  if (info.variant) std::printf("  %s (variant speaker %d) : %s\n", lang_upper.c_str(), info.speaker, escape_newlines(*row).c_str());
  else std::printf("  %s : %s\n", lang_upper.c_str(), escape_newlines(*row).c_str());
  import::Message message = import::parse_markup(*row, lang);
  std::printf("  tokens : %s\n", import::describe(message).c_str());
  import::Message flat = import::flatten(message, tables.make_context(lang));
  std::printf("  flat   : %s\n", import::describe(flat).c_str());
  return 0;
}

int cmd_find(vfs::Vfs& game_fs, const std::string& needle, Lang lang) {
  import::LangTables tables;
  tables.init(&game_fs);
  int hits = 0;
  for (int id = 0; id < 2000; ++id) {
    const char* name = import::room_name_for_id(id);
    if (!name) continue;
    for (int msg = 0; msg < 400; ++msg) {
      const std::string* row = tables.raw_field(id, msg, lang);
      if (row && row->find(needle) != std::string::npos) {
        std::printf("room %s (id %d) msg %d : %s\n", name, id, msg, escape_newlines(*row).c_str());
        ++hits;
      }
    }
  }
  return hits ? 0 : 1;
}

// Per language: the room message records (excluding each block's sentinel record) against the lang.dat rows they map to.
int cmd_coverage(vfs::Vfs& game_fs) {
  import::LangTables tables;
  tables.init(&game_fs);
  std::map<std::string, size_t> block_records;
  size_t rooms = 0, missing_blocks = 0, total = 0;
  std::vector<std::pair<int, std::string>> room_list;
  for (int id = 0; id < 2000; ++id)
    if (const char* name = import::room_name_for_id(id)) room_list.emplace_back(id, name);
  size_t real[kLangCount] = {}, err[kLangCount] = {}, miss[kLangCount] = {};
  for (auto& [id, name] : room_list) {
    auto it = block_records.find(name);
    if (it == block_records.end()) {
      size_t records = 0;
      if (auto bytes = import::read_room_dialogue_block(game_fs, name)) {
        if (auto block = import::parse_dialogue_block(*bytes)) records = block->records.size() > 0 ? block->records.size() - 1 : 0;
      } else {
        ++missing_blocks;
      }
      it = block_records.emplace(name, records).first;
    }
    ++rooms;
    total += it->second;
    for (int lang_index = 0; lang_index < kLangCount; ++lang_index) {
      const Lang lang = static_cast<Lang>(lang_index);
      for (size_t i = 0; i < it->second; ++i) {
        const std::string* row = tables.raw_field(id, static_cast<int>(i), lang);
        if (!row) ++miss[lang_index];
        else if (*row == "{ERROR}") ++err[lang_index];
        else ++real[lang_index];
      }
    }
  }
  std::printf("rooms %zu (blocks missing: %zu), message records excluding sentinels: %zu\n", rooms, missing_blocks, total);
  std::printf("%-5s %10s %10s %10s %10s\n", "lang", "rows", "text", "{ERROR}", "missing");
  for (int lang_index = 0; lang_index < kLangCount; ++lang_index)
    std::printf("%-5s %10zu %10zu %10zu %10zu\n", import::lang_code(static_cast<Lang>(lang_index)),
                tables.field_row_count(static_cast<Lang>(lang_index)), real[lang_index], err[lang_index], miss[lang_index]);
  import::JpGlyphs jp_glyphs;
  std::printf("embedded JP glyph codes: %zu\n", jp_glyphs.known_codes());
  return 0;
}

}  // namespace

int octext_main(int argc, char** argv) {
  std::vector<std::string> args;
  std::string gamedir;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-g" && i + 1 < argc) gamedir = argv[++i];
    else args.push_back(arg);
  }
  if (gamedir.empty()) gamedir = tool::env_game_dir();
  if (args.empty()) return usage();
  vfs::Vfs game_fs;
  const std::filesystem::path dir = platform::find_game_dir(gamedir);
  if (dir.empty() || !game_fs.open(dir)) {
    std::fprintf(stderr, "game directory not found (use -g <dir> or OPENCROSS_GAMEDIR)\n");
    return 1;
  }
  if (args[0] == "msg" && args.size() >= 3) {
    Lang lang = Lang::En;
    if (args.size() >= 4 && !import::parse_lang(args[3], lang)) return usage();
    return cmd_msg(game_fs, args[1], std::atoi(args[2].c_str()), lang);
  }
  if (args[0] == "find" && args.size() >= 2) {
    Lang lang = Lang::En;
    if (args.size() >= 3 && !import::parse_lang(args[2], lang)) return usage();
    return cmd_find(game_fs, args[1], lang);
  }
  if (args[0] == "coverage") return cmd_coverage(game_fs);
  return usage();
}
