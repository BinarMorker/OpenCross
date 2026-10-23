// FxTextureCache: texture bindings of the effect overlays' model draws.
#include "game/battle/battle_fx_textures.h"

#include <cstring>

#include "game/battle/battle_fx.h"

namespace oc::game {

void FxTextureCache::reset(render::SpriteBatch&) {
  model_sets.clear();
  vram_pages.clear();
  stock_env_set = {};
  stock_env_tex.reset();
  stock_env_for = nullptr;
  stock_env_k = stock_env_built_k = -1;
  white_tex.clear();
  vram_pack = nullptr;
}

render::TexRef FxTextureCache::model_texture_set(render::SpriteBatch&, const render::Sampler& sampler, const render::Texture& texture) {
  for (const auto& [texture_handle, cached_set] : model_sets)
    if (texture_handle == texture.handle()) return cached_set;
  const render::TexRef cached_set(texture, sampler);
  model_sets.emplace_back(texture.handle(), cached_set);
  return cached_set;
}

render::TexRef FxTextureCache::white_texture_set(render::GpuContext& ctx, render::SpriteBatch&, const render::Sampler& sampler, const import::Image& img, u8 white) {
  for (const FxWhiteTex& cached : white_tex)
    if (cached.image == &img && cached.white == white) return cached.set;
  if (img.indexed || img.pixels.size() < static_cast<size_t>(img.width) * img.height * 4u) return {};
  std::vector<u8> px(img.pixels.size());
  for (size_t i = 0; i + 3 < px.size(); i += 4) {
    const bool on = white == 2 || img.pixels[i + 3] != 0;
    px[i] = px[i + 1] = px[i + 2] = on ? 255 : 0;
    px[i + 3] = on ? import::kStpAlpha : 0;
  }
  FxWhiteTex entry;
  entry.image = &img;
  entry.white = white;
  entry.tex = std::make_unique<render::Texture>();
  if (!entry.tex->create(ctx, img.width, img.height, render::TextureFormat::Rgba8, px.data())) return {};
  entry.set = render::TexRef(*entry.tex, sampler);
  white_tex.push_back(std::move(entry));
  return white_tex.back().set;
}

bool FxTextureCache::vram_ready(const FxPack* pack, bool& rebuilt) {
  if (!pack) return false;
  if (vram_pack != pack) {
    rebuilt = true;
    vram = pack->build_vram();
    vram_pack = pack;
    ++vram_version;
  }
  return true;
}

void FxTextureCache::apply_vram_moves(effects::FxModels& models, bool ready) {
  if (ready) {
    for (const effects::FxModels::VramMove& mv : models.vram_moves) {
      if (mv.w <= 0 || mv.h <= 0) continue;
      std::vector<u16> buf(static_cast<size_t>(mv.w) * static_cast<size_t>(mv.h));
      for (int y = 0; y < mv.h; ++y)
        for (int x = 0; x < mv.w; ++x) {
          const int sy = mv.scroll ? mv.y + ((y + mv.dy) % mv.h + mv.h) % mv.h : mv.y + y;
          buf[static_cast<size_t>(y) * static_cast<size_t>(mv.w) + static_cast<size_t>(x)] = vram.at(mv.x + x, sy);
        }
      if (mv.scroll) vram.blit(mv.x, mv.y, mv.w, mv.h, buf);
      else vram.blit(mv.dx, mv.dy, mv.w, mv.h, buf);
    }
    ++vram_version;
  }
  models.vram_moves.clear();
}

render::TexRef FxTextureCache::vram_page_set(render::GpuContext& ctx, render::SpriteBatch&, const render::Sampler& sampler, u16 tpage, u16 clut) {
  FxVramPage* pg = nullptr;
  for (FxVramPage& page : vram_pages)
    if (page.tpage == tpage && page.clut == clut) pg = &page;
  if (pg && pg->version == vram_version && pg->set) return pg->set;
  const int mode = (tpage >> 7) & 3, bpp = mode == 0 ? 4 : mode == 1 ? 8 : 16;
  const int x0 = (tpage & 0xF) * 64 * (16 / bpp), y0 = ((tpage >> 4) & 1) * 256;
  std::vector<u16> pal;
  if (bpp < 16) pal = vram.read_clut((clut & 0x3F) * 16, (clut >> 6) & 0x1FF, bpp == 4 ? 16 : 256);
  std::vector<u8> px(256u * 256u * 4u);
  for (int y = 0; y < 256; ++y)
    for (int x = 0; x < 256; ++x) {
      const u32 texel = import::bgr555_to_rgba_stp(vram.texel(x0 + x, y0 + y, bpp, pal));
      std::memcpy(&px[(static_cast<size_t>(y) * 256u + static_cast<size_t>(x)) * 4u], &texel, 4);
    }
  if (!pg) {
    vram_pages.emplace_back();
    pg = &vram_pages.back();
    pg->tpage = tpage;
    pg->clut = clut;
  }
  if (!pg->tex) {
    pg->tex = std::make_unique<render::Texture>();
    if (!pg->tex->create(ctx, 256, 256, render::TextureFormat::Rgba8, px.data())) {
      pg->tex.reset();
      return {};
    }
    pg->set = render::TexRef(*pg->tex, sampler);
  } else {
    pg->tex->update(ctx, px.data());
  }
  pg->version = vram_version;
  return pg->set;
}

render::TexRef FxTextureCache::stock_env_page_set(render::GpuContext& ctx, render::SpriteBatch&, const render::Sampler& sampler, const assets::StageAsset* asset) {
  if (!asset || asset->env_rgba.empty()) return {};
  if (asset == stock_env_for && stock_env_set && stock_env_built_k == stock_env_k) return stock_env_set;
  // A stage load writes its own CLUT rows (kamei / lasbos swaps).
  if (asset != stock_env_for && stock_env_for) stock_env_k = -1;
  // FxModels::env_clut (darkfeit / lasdes3): every CLUT entry's 5-bit channels x k >> 8, | 0x8000 - a texel of
  // entry 0x0000 turns opaque black, every texel takes the STP bit.
  std::vector<u8> px = asset->env_rgba;
  if (stock_env_k >= 0)
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
      for (int ch = 0; ch < 3; ++ch) {
        const int channel5 = px[i + static_cast<size_t>(ch)] >> 3, scaled = px[i + 3] ? (channel5 * stock_env_k) >> 8 : 0;
        px[i + static_cast<size_t>(ch)] = static_cast<u8>((scaled << 3) | (scaled >> 2));
      }
      px[i + 3] = import::kStpAlpha;
    }
  stock_env_built_k = stock_env_k;
  if (asset == stock_env_for && stock_env_tex && stock_env_set) {
    stock_env_tex->update(ctx, px.data());
    return stock_env_set;
  }
  stock_env_set = {};
  stock_env_tex = std::make_unique<render::Texture>();
  stock_env_for = asset;
  if (!stock_env_tex->create(ctx, 256, 256, render::TextureFormat::Rgba8, px.data())) {
    stock_env_tex.reset();
    return {};
  }
  stock_env_set = render::TexRef(*stock_env_tex, sampler);
  return stock_env_set;
}

}  // namespace oc::game
