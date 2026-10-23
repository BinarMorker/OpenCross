// Auto-save (RDE): trigger rule + slot 0 round trip through a save.dat built in memory (no real save is touched).
#include <cstdio>
#include <cstring>
#include <map>

#include "game/autosave.h"

using namespace oc;
using game::Autosave;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static Autosave::Field make_field(u16 loc, bool allowed = true, int entrance = 0) { return {loc, allowed, loc, entrance}; }

static void test_trigger() {
  Autosave autosave;
  // Not allowed: no write. Allowed turning on writes even at the same location; the same location then does not; a location
  // change does.
  CHECK(!autosave.update(make_field(5, false)));
  CHECK(autosave.update(make_field(5)));
  CHECK(!autosave.update(make_field(5)));
  CHECK(autosave.update(make_field(6)));
  CHECK(!autosave.update(make_field(6)));
  // A change while not allowed is recorded without a write; allowed again writes.
  CHECK(!autosave.update(make_field(7, false)));
  CHECK(autosave.update(make_field(7)));
  autosave.note_load();
  // The first tick after a load only records.
  CHECK(!autosave.update(make_field(9)));
  CHECK(!autosave.update(make_field(9)));
  CHECK(autosave.update(make_field(10)));
  // Excluded entrances: nothing written, but the location is recorded.
  CHECK(!autosave.update(make_field(0xFA, true, 0x14)));
  CHECK(!autosave.update(make_field(0xFA, true, 0x14)));
  CHECK(autosave.update(make_field(0xFB, true, 0x18)));
  CHECK(!autosave.update(make_field(0xFC, true, 0x1D)));
  CHECK(autosave.update(make_field(0xFD, true, 0x16)));
  CHECK(Autosave::entrance_excluded(0xFA, 0x13) && !Autosave::entrance_excluded(0xFA, 0x17));
  CHECK(Autosave::entrance_excluded(0xFB, 0x17) && !Autosave::entrance_excluded(0xFB, 0x18));
  CHECK(Autosave::entrance_excluded(0xFC, 0x10) && !Autosave::entrance_excluded(0xFC, 0x16) && !Autosave::entrance_excluded(0xFC, 0x1B));
  CHECK(Autosave::entrance_excluded(0xFD, 0x17) && !Autosave::entrance_excluded(0xFD, 0x16) && !Autosave::entrance_excluded(0xFD, 0x1C));
  CHECK(!Autosave::entrance_excluded(0x10, 0x14));
}

static void test_write() {
  save::MemCard blank = save::MemCard::blank();
  std::vector<save::ZipMember> members;
  members.push_back({"memcard1.mcr", blank.bytes(), true});
  members.push_back({"memcard2.mcr", blank.bytes(), true});
  save::SaveDat dat = save::SaveDat::open(save::zip_write(members, save::kRdeSavePassword, 1));

  save::SaveSlot st;
  st.head[0] = 'S';
  st.head[1] = 'C';
  st.head[save::head::kLocation] = 0x42;
  st.set_gold(1234);
  st.set_playtime_frames(777);
  CHECK(Autosave::write(dat, st));
  // The caller's state is untouched.
  CHECK(st.head[save::head::kLeaderName] == 0);

  const save::MemCard* card = dat.card(1);
  const auto slots = save::list_slots(*card);
  CHECK(slots.size() == 1 && slots[0].slot == 0 && slots[0].name == "BISLPSP02364CHRONO00");
  const save::SaveSlot back = save::read_slot(*card, slots[0].block);
  CHECK(back.gold() == 1234 && back.playtime_frames() == 777 && back.location() == 0x42);
  CHECK(back.head[save::head::kLeaderName] == 0x83 && back.head[save::head::kLeaderName + 11] == 0x75);
  CHECK(save::block_checksum_ok(card->block(slots[0].block)));
  // RDE's loader verdict, emulated: version at 0x1FF8; > 0x102 adds the header frame (0..0x17F) to the payload sum.
  {
    const auto blk = card->block(slots[0].block);
    auto u32at = [&](size_t offset) { return u32(blk[offset]) | u32(blk[offset + 1]) << 8 | u32(blk[offset + 2]) << 16 | u32(blk[offset + 3]) << 24; };
    const u32 ver = u32at(0x1FF8);
    u32 sum = 0;
    for (size_t i = 0x180; i < 0x1FF8; ++i) sum += blk[i];
    if (ver > 0x102)
      for (size_t i = 0; i < 0x180; ++i) sum += blk[i];
    CHECK(ver == 0x106);
    // A mismatch would read as "data corrupted"; 0x24 holds the summary stream bit count.
    CHECK(sum == u32at(0x1FFC));
    CHECK(u32at(0x24) == 210);
    CHECK(blk[0x50] == 1 && blk[0x52] == 'C' && blk[0x53] == 'R' && blk[0x54] == 'D' && blk[0x55] == '0');
    // Playtime: the summary copy (0x37, unaligned) and the state word (0x588) both carry the frames.
    const u32 pv = u32(blk[0x37]) | u32(blk[0x38]) << 8 | u32(blk[0x39]) << 16 | u32(blk[0x3A]) << 24;
    CHECK(pv == 777 && u32at(0x588) == 777);
  }
  // A second write reuses the block; a manual slot 1 gets its own.
  CHECK(Autosave::write(dat, st));
  CHECK(save::write_slot(*dat.card(1), 1, st) != 0);
  CHECK(save::list_slots(*dat.card(1)).size() == 2);
  // ext_save.dat: no host = untouched; with a host slot 0 gets the language / counter / names and the other records stay.
  CHECK(!dat.member("ext_save.dat"));
  std::map<int, std::u32string> names{{0, U"Zed"}};
  save::ExtHost host;
  host.language = 2;
  host.battles = 77;
  host.names = &names;
  dat.set_ext_host(&host);
  CHECK(Autosave::write(dat, st));
  CHECK(dat.member("ext_save.dat") && dat.member("ext_save.dat")->data.size() == save::kExtSize);
  const save::ExtSlot ex = dat.ext().slot(0);
  CHECK(ex.language == 2 && ex.battles == 77 && ex.names == names);
  CHECK(dat.ext().slot(1).names.empty());
  // Serialize / reload keeps it.
  save::SaveDat re = save::SaveDat::open(dat.serialize(1));
  CHECK(re.card(1) && re.card(1)->find("BISLPSP02364CHRONO00") != 0);
  CHECK(re.ext().slot(0).battles == 77 && re.ext().slot(0).names == names);
}

int main() {
  test_trigger();
  test_write();
  std::printf(g_fail ? "test_autosave: %d FAILED\n" : "test_autosave: OK\n", g_fail);
  return g_fail ? 1 : 0;
}
