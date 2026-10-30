// The viewer must evaluate raw clips at one shared time, not advance two
// independent actors or assume matching clip vector order.
#include <cmath>
#include <cstdio>

#include "game/battle/battle_pose.h"
#include "game/model_viewer.h"

using namespace oc;
int main() {
  namespace v=game::viewer;
  assets::ModelAsset reference, modified;
  reference.skeleton.joints.emplace_back();
  assets::AnimationClip internal, extra;
  internal.name="s4_00";internal.source="s4";internal.index=0;internal.frame_count=2;
  internal.tracks.resize(1);
  internal.tracks[0].trans={{{0},{0,0,0}},{{1},{10,0,0}}};
  extra=internal;extra.name="ex_00";extra.source="ex";
  extra.tracks[0].trans={{{0},{100,0,0}},{{1},{200,0,0}}};
  reference.clips={internal,extra};
  modified=reference;modified.clips={extra,internal};
  const int selected=v::matching_clip(modified.clips,reference.clips[0]);
  if(selected!=1)return 1;
  game::UnitPose a,b;a.set_model(&reference);b.set_model(&modified);
  std::vector<render::Mat4> ap,bp;
  for(double seconds:{0.,1./30.,1./15.,.1,1.,100.}) {
    const auto frame=static_cast<float>(v::playback_frame(seconds,internal.frame_count,true));
    a.palette_at(0,frame,ap);b.palette_at(selected,frame,bp);
    if(ap.size()!=1 || bp.size()!=1)return 2;
    for(size_t k=0;k<16;++k)if(std::fabs(ap[0].m[k]-bp[0].m[k])>1e-5f)return 3;
  }
  a.palette_at(0,.5f,ap);
  if(std::fabs(ap[0].at(0,3)-5.f)>1e-5f)return 4;
  a.palette_at(-1,0,ap);
  if(std::fabs(ap[0].at(0,3))>1e-5f)return 5;
  std::puts("model viewer shared pose: ok");
  return 0;
}
