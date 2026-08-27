#include "MagniPhase.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"

MagniPhase::MagniPhase(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, 1))
{
  GetParam(kParamFFTSize)->InitEnum("FFT Size", 1, 3, "", IParam::kFlagsNone, "", "512", "1024", "2048");
  GetParam(kParamOverlap)->InitEnum("Overlap", 0, 2, "", IParam::kFlagsNone, "", "2x", "4x");
  GetParam(kParamWindowMorph)->InitDouble("Window Morph", 0., 0., (double)(MagniPhaseEngine::kNumWindows - 1), 0.01);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS,
                         GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachPanelBackground(COLOR_GRAY);
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);

    const IRECT bounds = pGraphics->GetBounds();
    IRECT area = bounds.GetPadded(-20.f);

    pGraphics->AttachControl(new IVMenuButtonControl(area.GetGridCell(0, 0, 1, 3).GetCentredInside(90.f, 30.f), kParamFFTSize, "FFT Size"));
    pGraphics->AttachControl(new IVMenuButtonControl(area.GetGridCell(0, 1, 1, 3).GetCentredInside(90.f, 30.f), kParamOverlap, "Overlap"));
    pGraphics->AttachControl(new IVKnobControl(area.GetGridCell(0, 2, 1, 3).GetCentredInside(60.f), kParamWindowMorph, "Window Morph"));
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
