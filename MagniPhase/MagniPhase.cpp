#include "MagniPhase.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"

MagniPhase::MagniPhase(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, 1))
{
  GetParam(kParamFFTSize)->InitEnum("FFT Size", 1, 3, "", IParam::kFlagsNone, "", "512", "1024", "2048");
  GetParam(kParamOverlap)->InitEnum("Overlap", 0, 2, "", IParam::kFlagsNone, "", "2x", "4x");
  GetParam(kParamWindowMorph)->InitDouble("Window Morph", 0., 0., (double)(MagniPhaseEngine::kNumWindows - 1), 0.01);
  GetParam(kParamMagMirror)->InitPercentage("Mag Mirror", 0.);
  GetParam(kParamPhaseMirror)->InitPercentage("Phase Mirror", 0.);
  GetParam(kParamFreqSwap)->InitPercentage("Freq Swap", 0.);
  GetParam(kParamSwapWindowSize)->InitPercentage("Swap Size", 100.);
  GetParam(kParamSwapWindowPosition)->InitPercentage("Swap Pos", 0.);
  GetParam(kParamInvertUpstream)->InitEnum("Invert", 0, 2, "", IParam::kFlagsNone, "", "Off", "On");

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
    IRECT area = bounds.GetPadded(-20.f);

    pGraphics->AttachControl(new IVMenuButtonControl(area.GetGridCell(0, 0, 4, 3).GetCentredInside(95.f, 32.f), kParamFFTSize, "FFT Size"));
    pGraphics->AttachControl(new IVMenuButtonControl(area.GetGridCell(0, 1, 4, 3).GetCentredInside(95.f, 32.f), kParamOverlap, "Overlap"));
    pGraphics->AttachControl(new IVKnobControl(area.GetGridCell(0, 2, 4, 3).GetCentredInside(64.f), kParamWindowMorph, "Window Morph", knobStyle));

    pGraphics->AttachControl(new IVKnobControl(area.GetGridCell(1, 0, 4, 3).GetCentredInside(64.f), kParamMagMirror, "Mag Mirror", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(area.GetGridCell(1, 1, 4, 3).GetCentredInside(64.f), kParamPhaseMirror, "Phase Mirror", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(area.GetGridCell(1, 2, 4, 3).GetCentredInside(64.f), kParamFreqSwap, "Freq Swap", knobStyle));

    pGraphics->AttachControl(new IVKnobControl(area.GetGridCell(2, 0, 4, 3).GetCentredInside(64.f), kParamSwapWindowSize, "Swap Size", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(area.GetGridCell(2, 1, 4, 3).GetCentredInside(64.f), kParamSwapWindowPosition, "Swap Pos", knobStyle));
    pGraphics->AttachControl(new IVMenuButtonControl(area.GetGridCell(2, 2, 4, 3).GetCentredInside(95.f, 32.f), kParamInvertUpstream, "Invert"));
  };
#endif

#if IPLUG_DSP
  OnReset();
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
  mEngine.SetWindowMorph((float)GetParam(kParamWindowMorph)->Value());
  mEngine.SetMagMirror((float)(GetParam(kParamMagMirror)->Value() / 100.0));
  mEngine.SetPhaseMirror((float)(GetParam(kParamPhaseMirror)->Value() / 100.0));
  mEngine.SetFreqSwap((float)(GetParam(kParamFreqSwap)->Value() / 100.0));
  mEngine.SetSwapWindowSize((float)(GetParam(kParamSwapWindowSize)->Value() / 100.0));
  mEngine.SetSwapWindowPosition((float)(GetParam(kParamSwapWindowPosition)->Value() / 100.0));
  mEngine.SetInvertUpstream((int)GetParam(kParamInvertUpstream)->Value() != 0);
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
    case kParamWindowMorph:
      mEngine.SetWindowMorph((float)GetParam(kParamWindowMorph)->Value());
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
    default:
      break;
  }
}

void MagniPhase::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  // Mono pour l'etape 1 : on ne traite que le canal 0.
  mInBuf.resize(nFrames);
  mOutBuf.resize(nFrames);

  for (int i = 0; i < nFrames; i++)
    mInBuf[i] = (float)inputs[0][i];

  mEngine.Process(mInBuf.data(), mOutBuf.data(), nFrames);

  for (int i = 0; i < nFrames; i++)
    outputs[0][i] = mOutBuf[i];
}

#endif
