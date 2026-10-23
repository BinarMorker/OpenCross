#pragma once
// FxTextureCache: the texture bindings the effect overlays draw unit and pack models with: the models' own
// textures, white-silhouette variants, VRAM pages cut from the running pack's VRAM and the stage's stock env page.
#include <memory>
#include <utility>
#include <vector>

#include "assets/battle_stage.h"
#include "game/effects/bespoke/bespoke.h"
#include "import/tim.h"
#include "render/sprite_batch.h"
#include "render/texture.h"

namespace oc::game {

struct FxPack;

struct FxTextureCache {
  // Sprite-batch sets of the unit / pack model textures drawn by the overlays, keyed by GPU texture.
  std::vector<std::pair<const SDL_GPUTexture*, render::TexRef>> model_sets;
  // white: FxPrim::white silhouette variants.
  struct FxWhiteTex {
    const void* image = nullptr;
    u8 white = 0;
    std::unique_ptr<render::Texture> tex;
    render::TexRef set;
  };
  std::vector<FxWhiteTex> white_tex;
  // The stock env page of the running stage (StageAsset::env_rgba): the units' env-mapped parts without shading flag 0x80.
  // stock_env_k: FxModels::env_clut, the CLUT row (0, 239) scale the last overlay wrote (-1 = the stage's own row).
  std::unique_ptr<render::Texture> stock_env_tex;
  render::TexRef stock_env_set;
  const assets::StageAsset* stock_env_for = nullptr;
  int stock_env_k = -1, stock_env_built_k = -1;
  struct FxVramPage {
    u16 tpage = 0, clut = 0;
    u32 version = 0;
    std::unique_ptr<render::Texture> tex;
    render::TexRef set;
  };
  std::vector<FxVramPage> vram_pages;
  // The running pack's VRAM (FxPack::build_vram), rebuilt per pack.
  import::Vram vram;
  const FxPack* vram_pack = nullptr;
  u32 vram_version = 0;

  // Frees every set the cache made (the scene is idle) and forgets the textures, the stock env page and the pack the VRAM was built for.
  void reset(render::SpriteBatch& sprites);

  // The set of a model texture, made on first use.
  render::TexRef model_texture_set(render::SpriteBatch& sprites, const render::Sampler& sampler, const render::Texture& texture);
  // The white-CLUT silhouette of `image`: 1 = every non-transparent texel white, 2 = every texel white (STP set: blended).
  render::TexRef white_texture_set(render::GpuContext& ctx, render::SpriteBatch& sprites, const render::Sampler& sampler, const import::Image& image, u8 white);

  // Builds the VRAM for `pack` when it is not the one built for; `rebuilt` tells the caller it was. False without a pack.
  bool vram_ready(const FxPack* pack, bool& rebuilt);
  // FxModels::vram_moves: the overlay's MoveImage / DR_MOVE / StoreImage + LoadImage copies, in order (a move reads the whole source first, as
  // the GPU's VRAM-to-VRAM copy does for the rectangles the overlays use). `ready` = the VRAM exists; the list is consumed either way.
  void apply_vram_moves(effects::FxModels& models, bool ready);
  // TexSrc::Vram: the 256 x 256 texel window a GT3 / GT4 / FT3 packet with this tpage / CLUT word samples, from the running pack's VRAM
  // (tpage bits 0-3 x / 64, bit 4 y / 256, bits 7-8 depth 4 / 8 / 16; CLUT word x / 16 in bits 0-5, y in bits 6-14), STP kept in alpha like the
  // model textures. Re-uploaded when the VRAM changed.
  render::TexRef vram_page_set(render::GpuContext& ctx, render::SpriteBatch& sprites, const render::Sampler& sampler, u16 tpage, u16 clut);
  // The stage's env page as a texture (StageAsset::env_rgba), rebuilt when the stage asset changes (kamei's swap).
  render::TexRef stock_env_page_set(render::GpuContext& ctx, render::SpriteBatch& sprites, const render::Sampler& sampler, const assets::StageAsset* asset);
};

}  // namespace oc::game
