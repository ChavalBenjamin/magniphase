#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "MagniPhaseEngine.h"
#include "GlitchEngine.h"
#include "WindowPreviewControl.h"
#include <vector>
#include <atomic>

// ============================================================================
// Config attendue dans MagniPhase/config.h :
//
//   #define PLUG_TYPE 0              // Effet (pas un instrument)
//   #define PLUG_DOES_MIDI_IN 0
//   #define PLUG_DOES_MIDI_OUT 0
//   #define PLUG_CHANNEL_IO "1.1-1"  // NOTE INCERTAINE : 2 bus d'entree
//                                    // (principal + side-chain, 1 canal
//                                    // chacun), 1 bus de sortie (1 canal).
//                                    // Premiere fois qu'on utilise un
//                                    // side-chain dans ce projet - syntaxe
//                                    // exacte a verifier/ajuster selon le
//                                    // resultat de compilation.
// ============================================================================

enum EParams
{
  kParamFFTSize = 0,
  kParamOverlap,
  kParamWindowCycles,      // nombre de cycles de l'oscillateur generant la fenetre
  kParamWindowPixelLevels, // resolution de quantification (bas = anguleux, haut = lisse)
  kParamMagMirror,   // 0 = normal, 0.5 = tout egal, 1 = miroir complet (magnitude)
  kParamPhaseMirror, // idem, pour la phase
  kParamFreqSwap,    // 0 = normal, 1 = grave/aigu completement echanges (phase inchangee)
  kParamSwapWindowSize,     // 0-1 : largeur de la zone concernee par Freq Swap (1 = tout le spectre)
  kParamSwapWindowPosition, // 0-1 : position de cette zone dans le spectre (deforme, voir moteur)
  kParamInvertUpstream,     // Off/On : inversion complete magnitude+phase, en amont de tout le reste
  kParamGlitchFreezeTime,   // 20-10000ms : duree de repetition du fragment capture
  kParamGlitchRate,         // 0-100% : vitesse du declenchement aleatoire (Poisson)
  kParamGlitchMode,         // Poisson / Rafales / Duree variable
  kNumParams
};

using namespace iplug;
using namespace igraphics;

class MagniPhase final : public iplug::Plugin
{
public:
  MagniPhase(const InstanceInfo& info);

  void OnIdle() override;
  void OnUIClose() override { mWindowView = nullptr; }

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnParamChange(int paramIdx) override;
  void OnReset() override;
#endif

private:
  WindowPreviewControl* mWindowView = nullptr;

#if IPLUG_DSP
  void UpdateEngineParams();

  MagniPhaseEngine mEngine;
  GlitchEngine mGlitchEngine;
  std::vector<float> mInBuf, mOutBuf, mSidechainBuf, mGlitchOutBuf;
#endif
};
