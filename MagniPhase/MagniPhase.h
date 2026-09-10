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
//   #define PLUG_CHANNEL_IO "2-2 2.1-2"  // NOTE INCERTAINE : stereo (2-2)
//                                        // en repli, ou stereo + Aux mono
//                                        // dedie au declenchement du
//                                        // glitch (2.1-2) si l'hote le
//                                        // supporte. Syntaxe a
//                                        // verifier/ajuster selon le
//                                        // resultat de compilation.
// ============================================================================

enum EParams
{
  kParamFFTSize = 0,
  kParamOverlap,
  kParamWindowCycles,      // nombre de cycles de l'oscillateur generant la fenetre
  kParamWindowPixelLevels, // 0-100% : quantite de "pixelisation" (0 = sinus parfait)
  kParamMagMirror,   // 0 = normal, 0.5 = tout egal, 1 = miroir complet (magnitude)
  kParamPhaseMirror, // idem, pour la phase
  kParamFreqSwap,    // 0 = normal, 1 = grave/aigu completement echanges (phase inchangee)
  kParamSwapWindowSize,     // 0-1 : largeur de la zone concernee par Freq Swap (1 = tout le spectre)
  kParamSwapWindowPosition, // 0-1 : position de cette zone dans le spectre (deforme, voir moteur)
  kParamInvertUpstream,     // Off/On : inversion complete magnitude+phase, en amont de tout le reste
  kParamGlitchFreezeTime,   // 20-10000ms : temps de maintien du glitch declenche par l'Aux (mode libre)
  kParamGlitchFreezeSync,   // Off/On : bascule Freeze en division rythmique plutot qu'en ms
  kParamGlitchFreezeNote,   // division rythmique (1/2 -> 1/64, + ternaire 1/8T -> 1/32T)
  kParamGlitchRate,         // 0-100% : frequence du declenchement interne (jamais -> tres souvent)
  kParamGlitchMode,         // Poisson / Rafales / Duree variable
  kParamGlitchEnable,       // Off/On : interrupteur general de toute la section Glitch
  kParamGlitchVolume,       // 0-150% : volume applique uniquement pendant qu'un glitch est actif
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

  // Deux instances independantes du moteur spectral (une par canal),
  // partageant les memes parametres mais chacune avec son propre etat
  // d'analyse/resynthese - vrai traitement stereo, pas un simple
  // dedoublement du mono.
  MagniPhaseEngine mEngineL, mEngineR;

  // Le glitch gere lui-meme les deux canaux ensemble (image stereo
  // coherente : meme instant capture/bouclé sur L et R).
  GlitchEngine mGlitchEngine;

  std::vector<float> mInBufL, mInBufR, mOutBufL, mOutBufR;
  std::vector<float> mSidechainBuf;
  std::vector<float> mGlitchOutBufL, mGlitchOutBufR;
#endif
};
