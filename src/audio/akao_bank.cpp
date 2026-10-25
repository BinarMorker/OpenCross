#include "audio/akao_bank.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "audio/byte_io.h"

namespace oc::audio {

std::shared_ptr<const Bank> parse_bank(ByteSpan data) {
  if (data.size() < 0x40 || std::memcmp(data.data(), "AKAO", 4) != 0) return nullptr;
  auto bank = std::make_shared<Bank>();
  bank->id = read_u16(data, 4);
  const u32 sample_bytes = read_u32(data, 0x14);
  bank->first_index = read_u32(data, 0x18);
  const u32 count = read_u32(data, 0x1C);
  if (count == 0 || count > 256) return nullptr;
  const size_t table = 0x40, data_off = table + size_t(count) * 16;
  if (data_off > data.size()) return nullptr;
  const ByteSpan samples = data.subspan(data_off, std::min<size_t>(sample_bytes, data.size() - data_off));
  bank->spu = read_u32(data, 0x10);
  bank->samples.assign(samples.begin(), samples.end());
  std::map<std::pair<u32, u32>, std::shared_ptr<const AdpcmSample>> cache;
  bank->inst.resize(count);
  for (u32 i = 0; i < count; ++i) {
    const u8* entry = &data[table + size_t(i) * 16];
    Instrument& inst = bank->inst[i];
    inst.start = read_u32(entry);
    inst.loop = read_u32(entry + 4);
    inst.spu = bank->spu;
    inst.fine_tune = s16(read_u16(entry + 8));
    inst.base_note = s16(read_u16(entry + 10));
    inst.adsr1 = read_u16(entry + 12);
    inst.adsr2 = read_u16(entry + 14);
    inst.bank_id = bank->id;
    if (inst.start + 16 > samples.size()) continue;
    auto key = std::make_pair(inst.start, inst.loop);
    auto it = cache.find(key);
    if (it == cache.end())
      it = cache.emplace(key, std::make_shared<AdpcmSample>(adpcm_decode_sample(samples, inst.start, inst.loop))).first;
    inst.sample = it->second;
  }
  return bank;
}

u32 sample_end(const std::vector<u8>& ram, u32 from) {
  u32 at = from;
  while (at + 16 <= ram.size()) {
    at += 16;
    if (ram[at - 15] & 1) break;
  }
  return at;
}

// Banks that no slot points into any more are dropped (a battle loads one bank per action and per spell; copies of the table keep
// their own).
void InstrumentTable::overlay(const std::shared_ptr<const Bank>& bank) {
  if (!bank) return;
  auto ram = ram_ ? std::make_shared<std::vector<u8>>(*ram_) : std::make_shared<std::vector<u8>>(kRamBytes, u8{0});
  const u32 begin = std::min(bank->spu, kRamBytes);
  const u32 end = u32(std::min<u64>(u64(bank->spu) + bank->samples.size(), kRamBytes));
  std::vector<size_t> reached;
  for (size_t idx = 0; idx < 256 && begin < end; ++idx) {
    const Instrument* inst = slot_[idx];
    if (!inst || (idx >= bank->first_index && idx < bank->first_index + bank->inst.size())) continue;
    const u32 from = inst->spu + inst->start;
    if (from < end && sample_end(*ram, from) > begin) reached.push_back(idx);
  }
  std::copy_n(bank->samples.begin(), end - begin, ram->begin() + begin);
  ram_ = ram;
  banks_.push_back(bank);
  for (size_t i = 0; i < bank->inst.size(); ++i) {
    const size_t idx = bank->first_index + i;
    if (idx < 256) slot_[idx] = &bank->inst[i];
  }
  if (!reached.empty()) {
    auto rebuilt = std::make_shared<Bank>();
    rebuilt->inst.reserve(reached.size());
    for (size_t idx : reached) {
      Instrument inst = *slot_[idx];
      inst.overwritten = true;
      inst.sample = std::make_shared<AdpcmSample>(adpcm_decode_sample(*ram_, inst.spu + inst.start, inst.spu + inst.loop));
      rebuilt->inst.push_back(std::move(inst));
    }
    for (size_t k = 0; k < reached.size(); ++k) slot_[reached[k]] = &rebuilt->inst[k];
    banks_.push_back(std::move(rebuilt));
  }
  banks_.erase(std::remove_if(banks_.begin(), banks_.end(),
                              [&](const std::shared_ptr<const Bank>& candidate) {
                                for (const Instrument* slot : slot_)
                                  if (slot && slot >= candidate->inst.data() && slot < candidate->inst.data() + candidate->inst.size()) return false;
                                return true;
                              }),
               banks_.end());
}

}  // namespace oc::audio
