#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "MagniPhaseEngine.h"
#include <vector>

// ============================================================================
// Config attendue dans MagniPhase/config.h :
//
//   #define PLUG_TYPE 0              // Effet (pas un instrument)
//   #define PLUG_DOES_MIDI_IN 0
//   #define PLUG_DOES_MIDI_OUT 0
//   #define PLUG_CHANNEL_IO "1-1"    // mono en entree, mono en sortie (etape 1)
// ============================================================================

enum EParams
{
  kParamFFTSize = 0,
  kParamOverlap,
  kParamWindowMorph, // position dans la banque de fenetres (Tukey -> lobes -> complexe)
  kParamMagMirror,   // 0 = normal, 0.5 = tout egal, 1 = miroir complet (magnitude)
  kParamPhaseMirror, // idem, pour la phase
  kParamFreqSwap,    // 0 = normal, 1 = grave/aigu completement echanges (phase inchangee)
  kParamSwapWindowSize,     // 0-1 : largeur de la zone concernee par Freq Swap (1 = tout le spectre)
  kParamSwapWindowPosition, // 0-1 : position de cette zone dans le spectre (deforme, voir moteur)
  kParamInvertUpstream,     // Off/On : inversion complete magnitude+phase, en amont de tout le reste
  kNumParams
};

using namespace iplug;
using namespace igraphics;

class MagniPhase final : public iplug::Plugin
{
public:
  MagniPhase(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnParamChange(int paramIdx) override;
  void OnReset() override;
#endif

private:
#if IPLUG_DSP
  void UpdateEngineParams();

  MagniPhaseEngine mEngine;
  std::vector<float> mInBuf, mOutBuf;
#endif
};
