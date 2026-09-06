#include "MagniPhase.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"

MagniPhase::MagniPhase(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, 1))
{
  GetParam(kParamFFTSize)->InitEnum("FFT Size", 1, 3, "", IParam::kFlagsNone, "", "512", "1024", "2048");
  GetParam(kParamOverlap)->InitEnum("Overlap", 0, 2, "", IParam::kFlagsNone, "", "2x", "4x");
  GetParam(kParamWindowCycles)->InitDouble("Cycles", 1., 1., 12., 0.01);
  GetParam(kParamWindowPixelLevels)->InitPercentage("Pixel", 0.);
  GetParam(kParamMagMirror)->InitPercentage("Mag Mirror", 0.);
  GetParam(kParamPhaseMirror)->InitPercentage("Phase Mirror", 0.);
  GetParam(kParamFreqSwap)->InitPercentage("Freq Swap", 0.);
  GetParam(kParamSwapWindowSize)->InitPercentage("Swap Size", 100.);
  GetParam(kParamSwapWindowPosition)->InitPercentage("Swap Pos", 0.);
  GetParam(kParamInvertUpstream)->InitEnum("Invert", 0, 2, "", IParam::kFlagsNone, "", "Off", "On");
  GetParam(kParamGlitchFreezeTime)->InitDouble("Freeze", 200., 20., 10000., 1., "ms");
  GetParam(kParamGlitchRate)->InitPercentage("Rate", 0.);
  GetParam(kParamGlitchMode)->InitEnum("Mode", 0, 3, "", IParam::kFlagsNone, "", "Poisson", "Rafales", "Duree Var.");
  GetParam(kParamGlitchEnable)->InitEnum("Glitch", 0, 2, "", IParam::kFlagsNone, "", "Off", "On");

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS,
                         GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachPanelBackground(COLOR_GRAY);
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);

    // Meme equilibre visuel que TroisCorpsWave : boutons agrandis, nom du
    // parametre reduit au-dessus, valeur laissee a sa taille normale.
    const IVStyle knobStyle = DEFAULT_STYLE.WithLabelText(IText(10.f, COLOR_WHITE));

    const IRECT bounds = pGraphics->GetBounds();
    IRECT controlsArea = bounds.GetFromTop(bounds.H() * 0.6f).GetPadded(-20.f);

    pGraphics->AttachControl(new IVMenuButtonControl(controlsArea.GetGridCell(0, 0, 6, 3).GetCentredInside(95.f, 32.f), kParamFFTSize, "FFT Size"));
    pGraphics->AttachControl(new IVMenuButtonControl(controlsArea.GetGridCell(0, 1, 6, 3).GetCentredInside(95.f, 32.f), kParamOverlap, "Overlap"));
    pGraphics->AttachControl(new IVKnobControl(controlsArea.GetGridCell(0, 2, 6, 3).GetCentredInside(64.f), kParamWindowCycles, "Cycles", knobStyle));

    pGraphics->AttachControl(new IVKnobControl(controlsArea.GetGridCell(1, 0, 6, 3).GetCentredInside(64.f), kParamWindowPixelLevels, "Pixel", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(controlsArea.GetGridCell(1, 1, 6, 3).GetCentredInside(64.f), kParamMagMirror, "Mag Mirror", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(controlsArea.GetGridCell(1, 2, 6, 3).GetCentredInside(64.f), kParamPhaseMirror, "Phase Mirror", knobStyle));

    pGraphics->AttachControl(new IVKnobControl(controlsArea.GetGridCell(2, 0, 6, 3).GetCentredInside(64.f), kParamFreqSwap, "Freq Swap", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(controlsArea.GetGridCell(2, 1, 6, 3).GetCentredInside(64.f), kParamSwapWindowSize, "Swap Size", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(controlsArea.GetGridCell(2, 2, 6, 3).GetCentredInside(64.f), kParamSwapWindowPosition, "Swap Pos", knobStyle));

    pGraphics->AttachControl(new IVMenuButtonControl(controlsArea.GetGridCell(3, 0, 6, 3).GetCentredInside(95.f, 32.f), kParamInvertUpstream, "Invert"));

    // --- Glitch (side-chain) ---
    pGraphics->AttachControl(new IVKnobControl(controlsArea.GetGridCell(4, 0, 6, 3).GetCentredInside(64.f), kParamGlitchFreezeTime, "Freeze", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(controlsArea.GetGridCell(4, 1, 6, 3).GetCentredInside(64.f), kParamGlitchRate, "Rate", knobStyle));
    pGraphics->AttachControl(new IVMenuButtonControl(controlsArea.GetGridCell(4, 2, 6, 3).GetCentredInside(95.f, 32.f), kParamGlitchMode, "Mode"));

    pGraphics->AttachControl(new IVMenuButtonControl(controlsArea.GetGridCell(5, 0, 6, 3).GetCentredInside(95.f, 32.f), kParamGlitchEnable, "Glitch"));

    // Petite fenetre de visualisation de la forme de fenetre Hann generee.
    IRECT windowViewArea = IRECT(bounds.L, bounds.T + bounds.H() * 0.6f, bounds.R, bounds.B).GetPadded(-20.f);
    mWindowView = new WindowPreviewControl(windowViewArea);
    pGraphics->AttachControl(mWindowView);
  };
#endif

#if IPLUG_DSP
  OnReset();
#endif
}

void MagniPhase::OnIdle()
{
#if IPLUG_DSP
  if (mWindowView && mEngine.WindowUIUpdated())
  {
    mWindowView->SetWaveform(mEngine.GetWindowForUI(), mEngine.GetWindowSizeForUI());
    mWindowView->SetDirty(false);
  }
#endif
}

#if IPLUG_DSP

void MagniPhase::UpdateEngineParams()
{
  int fftSizeIdx = (int)GetParam(kParamFFTSize)->Value();
  int fftSize = 512 << fftSizeIdx; // 0->512, 1->1024, 2->2048

  int overlapIdx = (int)GetParam(kParamOverlap)->Value();
  int overlap = (overlapIdx == 0) ? 2 : 4;

  mEngine.Init(fftSize, overlap);
  mEngine.SetWindowCycles((float)GetParam(kParamWindowCycles)->Value());
  mEngine.SetWindowPixelAmount((float)(GetParam(kParamWindowPixelLevels)->Value() / 100.0));
  mEngine.SetMagMirror((float)(GetParam(kParamMagMirror)->Value() / 100.0));
  mEngine.SetPhaseMirror((float)(GetParam(kParamPhaseMirror)->Value() / 100.0));
  mEngine.SetFreqSwap((float)(GetParam(kParamFreqSwap)->Value() / 100.0));
  mEngine.SetSwapWindowSize((float)(GetParam(kParamSwapWindowSize)->Value() / 100.0));
  mEngine.SetSwapWindowPosition((float)(GetParam(kParamSwapWindowPosition)->Value() / 100.0));
  mEngine.SetInvertUpstream((int)GetParam(kParamInvertUpstream)->Value() != 0);

  mGlitchEngine.Init(GetSampleRate());
  mGlitchEngine.SetFreezeTime((float)GetParam(kParamGlitchFreezeTime)->Value());
  mGlitchEngine.SetRandomRate((float)(GetParam(kParamGlitchRate)->Value() / 100.0));
  mGlitchEngine.SetGlitchMode((int)GetParam(kParamGlitchMode)->Value());
  mGlitchEngine.SetEnabled((int)GetParam(kParamGlitchEnable)->Value() != 0);
}

void MagniPhase::OnReset()
{
  UpdateEngineParams();
}

void MagniPhase::OnParamChange(int paramIdx)
{
  switch (paramIdx)
  {
    case kParamFFTSize:
    case kParamOverlap:
      UpdateEngineParams(); // reinitialise le moteur (taille FFT/overlap changes)
      break;
    case kParamWindowCycles:
      mEngine.SetWindowCycles((float)GetParam(kParamWindowCycles)->Value());
      break;
    case kParamWindowPixelLevels:
      mEngine.SetWindowPixelAmount((float)(GetParam(kParamWindowPixelLevels)->Value() / 100.0));
      break;
    case kParamMagMirror:
      mEngine.SetMagMirror((float)(GetParam(kParamMagMirror)->Value() / 100.0));
      break;
    case kParamPhaseMirror:
      mEngine.SetPhaseMirror((float)(GetParam(kParamPhaseMirror)->Value() / 100.0));
      break;
    case kParamFreqSwap:
      mEngine.SetFreqSwap((float)(GetParam(kParamFreqSwap)->Value() / 100.0));
      break;
    case kParamSwapWindowSize:
      mEngine.SetSwapWindowSize((float)(GetParam(kParamSwapWindowSize)->Value() / 100.0));
      break;
    case kParamSwapWindowPosition:
      mEngine.SetSwapWindowPosition((float)(GetParam(kParamSwapWindowPosition)->Value() / 100.0));
      break;
    case kParamInvertUpstream:
      mEngine.SetInvertUpstream((int)GetParam(kParamInvertUpstream)->Value() != 0);
      break;
    case kParamGlitchFreezeTime:
      mGlitchEngine.SetFreezeTime((float)GetParam(kParamGlitchFreezeTime)->Value());
      break;
    case kParamGlitchRate:
      mGlitchEngine.SetRandomRate((float)(GetParam(kParamGlitchRate)->Value() / 100.0));
      break;
    case kParamGlitchMode:
      mGlitchEngine.SetGlitchMode((int)GetParam(kParamGlitchMode)->Value());
      break;
    case kParamGlitchEnable:
      mGlitchEngine.SetEnabled((int)GetParam(kParamGlitchEnable)->Value() != 0);
      break;
    default:
      break;
  }
}

void MagniPhase::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  // Canal 0 = entree principale (mono). Canal 1 = entree side-chain (le
  // second bus declare dans PLUG_CHANNEL_IO), utilisee uniquement comme
  // declencheur pour le glitch - jamais mixee dans la sortie audio.
  mInBuf.resize(nFrames);
  mOutBuf.resize(nFrames);
  mSidechainBuf.resize(nFrames);
  mGlitchOutBuf.resize(nFrames);

  bool hasSidechain = (inputs[1] != nullptr);

  for (int i = 0; i < nFrames; i++)
  {
    mInBuf[i] = (float)inputs[0][i];
    mSidechainBuf[i] = hasSidechain ? (float)inputs[1][i] : 0.f;
  }

  // 1) Traitement spectral (MagniPhaseEngine)
  mEngine.Process(mInBuf.data(), mOutBuf.data(), nFrames);

  // 2) Glitch temporel applique sur le resultat, declenche par la side-chain
  mGlitchEngine.Process(mOutBuf.data(), hasSidechain ? mSidechainBuf.data() : nullptr, mGlitchOutBuf.data(), nFrames);

  for (int i = 0; i < nFrames; i++)
    outputs[0][i] = mGlitchOutBuf[i];
}

#endif
