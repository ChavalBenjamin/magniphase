#include "MagniPhase.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"

MagniPhase::MagniPhase(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, 1))
{
  GetParam(kParamFFTSize)->InitEnum("FFT Size", 1, 5, "", IParam::kFlagsNone, "", "512", "1024", "2048", "4096", "8192");
  GetParam(kParamOverlap)->InitEnum("Overlap", 0, 2, "", IParam::kFlagsNone, "", "2x", "4x");
  GetParam(kParamWindowCycles)->InitDouble("Cycles", 1., 1., 12., 0.01);
  GetParam(kParamWindowPixelLevels)->InitPercentage("Pixel", 0.);
  GetParam(kParamMagMirror)->InitPercentage("Mag Mirror", 0.);
  GetParam(kParamPhaseMirror)->InitPercentage("Phase Mirror", 0.);
  GetParam(kParamFreqSwap)->InitPercentage("Freq Swap", 0.);
  GetParam(kParamSwapWindowSize)->InitPercentage("Swap Size", 100.);
  GetParam(kParamSwapWindowPosition)->InitDouble("Swap Pos", 0., 0., 100., 0.001, "%");
  GetParam(kParamInvertUpstream)->InitEnum("Invert", 0, 2, "", IParam::kFlagsNone, "", "Off", "On");
  GetParam(kParamGlitchFreezeTime)->InitDouble("Freeze", 200., 20., 10000., 1., "ms");
  GetParam(kParamGlitchFreezeSync)->InitEnum("Sync", 0, 2, "", IParam::kFlagsNone, "", "Off", "On");
  GetParam(kParamGlitchFreezeNote)->InitEnum("Note", 2, 9, "", IParam::kFlagsNone, "",
    "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/8T", "1/16T", "1/32T");
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

    const IVStyle knobStyle = DEFAULT_STYLE.WithLabelText(IText(10.f, COLOR_WHITE));
    const IVStyle mirrorKnobStyle = knobStyle; // meme style, taille geree separement (+15%)
    const IVStyle blueKnobStyle = knobStyle.WithColor(kFG, IColor(255, 120, 200, 255)); // meme bleu que la courbe du graph
    const IColor kRedGrey(255, 130, 95, 95); // "rouge grisonnant" pour la zone Freq Swap
    const IVStyle swapKnobStyle = knobStyle.WithColor(kFG, kRedGrey);

    const IRECT bounds = pGraphics->GetBounds();

    // --- Rangee du haut : reglages generaux, selecteurs x2 plus epais ---
    IRECT topRow = bounds.GetFromTop(60.f).GetPadded(-10.f);
    pGraphics->AttachControl(new IVMenuButtonControl(topRow.GetGridCell(0, 0, 1, 3).GetCentredInside(180.f, 46.f), kParamFFTSize, "FFT Size"));
    pGraphics->AttachControl(new IVMenuButtonControl(topRow.GetGridCell(0, 1, 1, 3).GetCentredInside(180.f, 46.f), kParamOverlap, "Overlap"));
    pGraphics->AttachControl(new IVMenuButtonControl(topRow.GetGridCell(0, 2, 1, 3).GetCentredInside(180.f, 46.f), kParamInvertUpstream, "Invert"));

    IRECT belowTop(bounds.L, bounds.T + 60.f, bounds.R, bounds.B);
    IRECT leftCol(belowTop.L, belowTop.T, belowTop.L + belowTop.W() * 0.6f, belowTop.B);
    IRECT rightCol(leftCol.R, belowTop.T, belowTop.R, belowTop.B);

    // --- Colonne gauche : panneau Mirror puis panneau Freq Swap, separes visuellement ---
    IRECT mirrorPanel(leftCol.L, leftCol.T, leftCol.R, leftCol.T + 150.f);
    pGraphics->AttachControl(new IPanelControl(mirrorPanel, IColor(255, 60, 60, 70)));
    IRECT mirrorRow = mirrorPanel.GetPadded(-10.f);
    pGraphics->AttachControl(new IVKnobControl(mirrorRow.GetGridCell(0, 0, 1, 2).GetCentredInside(74.f), kParamMagMirror, "Mag Mirror", mirrorKnobStyle));
    pGraphics->AttachControl(new IVKnobControl(mirrorRow.GetGridCell(0, 1, 1, 2).GetCentredInside(74.f), kParamPhaseMirror, "Phase Mirror", mirrorKnobStyle));

    IRECT swapPanel(leftCol.L, mirrorPanel.B, leftCol.R, mirrorPanel.B + 170.f);
    pGraphics->AttachControl(new IPanelControl(swapPanel, IColor(255, 75, 55, 55)));
    IRECT swapRow = swapPanel.GetPadded(-10.f);
    pGraphics->AttachControl(new IVKnobControl(swapRow.GetGridCell(0, 0, 1, 3).GetCentredInside(64.f), kParamFreqSwap, "Freq Swap", swapKnobStyle));
    pGraphics->AttachControl(new IVKnobControl(swapRow.GetGridCell(0, 1, 1, 3).GetCentredInside(64.f), kParamSwapWindowSize, "Swap Size", swapKnobStyle));
    pGraphics->AttachControl(new IVKnobControl(swapRow.GetGridCell(0, 2, 1, 3).GetCentredInside(88.f), kParamSwapWindowPosition, "Swap Pos", swapKnobStyle));

    // --- Colonne droite : Cycles/Pixel (bleu) au-dessus de l'apercu
    // reduit, puis le panneau Glitch separe visuellement en dessous ---
    IRECT windowCtrlRow = IRECT(rightCol.L, rightCol.T, rightCol.R, rightCol.T + 90.f).GetPadded(-10.f);
    pGraphics->AttachControl(new IVKnobControl(windowCtrlRow.GetGridCell(0, 0, 1, 2).GetCentredInside(64.f), kParamWindowCycles, "Cycles", blueKnobStyle));
    pGraphics->AttachControl(new IVKnobControl(windowCtrlRow.GetGridCell(0, 1, 1, 2).GetCentredInside(64.f), kParamWindowPixelLevels, "Pixel", blueKnobStyle));

    // Fenetre d'apercu reduite a 35% de sa taille precedente.
    IRECT windowViewArea = IRECT(rightCol.L, windowCtrlRow.B, rightCol.R, windowCtrlRow.B + 95.f).GetPadded(-10.f);
    mWindowView = new WindowPreviewControl(windowViewArea);
    pGraphics->AttachControl(mWindowView);

    IRECT glitchPanel(rightCol.L, windowViewArea.B, rightCol.R, belowTop.B);
    pGraphics->AttachControl(new IPanelControl(glitchPanel, IColor(255, 55, 65, 60)));
    IRECT glitchArea = glitchPanel.GetPadded(-10.f);

    pGraphics->AttachControl(new IVKnobControl(glitchArea.GetGridCell(0, 0, 2, 3).GetCentredInside(60.f), kParamGlitchFreezeTime, "Freeze", knobStyle));
    pGraphics->AttachControl(new IVKnobControl(glitchArea.GetGridCell(0, 1, 2, 3).GetCentredInside(60.f), kParamGlitchRate, "Rate", knobStyle));
    pGraphics->AttachControl(new IVMenuButtonControl(glitchArea.GetGridCell(0, 2, 2, 3).GetCentredInside(80.f, 30.f), kParamGlitchMode, "Mode"));

    pGraphics->AttachControl(new IVMenuButtonControl(glitchArea.GetGridCell(1, 0, 2, 3).GetCentredInside(80.f, 30.f), kParamGlitchEnable, "Glitch"));
    pGraphics->AttachControl(new IVMenuButtonControl(glitchArea.GetGridCell(1, 1, 2, 3).GetCentredInside(80.f, 30.f), kParamGlitchFreezeSync, "Sync"));
    pGraphics->AttachControl(new IVMenuButtonControl(glitchArea.GetGridCell(1, 2, 2, 3).GetCentredInside(80.f, 30.f), kParamGlitchFreezeNote, "Note"));
  };
#endif

#if IPLUG_DSP
  OnReset();
#endif
}

void MagniPhase::OnIdle()
{
#if IPLUG_DSP
  // Les deux canaux partagent les memes parametres de fenetre -> un seul
  // apercu suffit, on lit celui du canal gauche.
  if (mWindowView && mEngineL.WindowUIUpdated())
  {
    mWindowView->SetWaveform(mEngineL.GetWindowForUI(), mEngineL.GetWindowSizeForUI());
    mWindowView->SetDirty(false);
  }
#endif
}

#if IPLUG_DSP

void MagniPhase::UpdateEngineParams()
{
  int fftSizeIdx = (int)GetParam(kParamFFTSize)->Value();
  int fftSize = 512 << fftSizeIdx; // 0->512, 1->1024, 2->2048, 3->4096, 4->8192

  int overlapIdx = (int)GetParam(kParamOverlap)->Value();
  int overlap = (overlapIdx == 0) ? 2 : 4;

  float windowCycles = (float)GetParam(kParamWindowCycles)->Value();
  float windowPixel = (float)(GetParam(kParamWindowPixelLevels)->Value() / 100.0);
  float magMirror = (float)(GetParam(kParamMagMirror)->Value() / 100.0);
  float phaseMirror = (float)(GetParam(kParamPhaseMirror)->Value() / 100.0);
  float freqSwap = (float)(GetParam(kParamFreqSwap)->Value() / 100.0);
  float swapSize = (float)(GetParam(kParamSwapWindowSize)->Value() / 100.0);
  float swapPos = (float)(GetParam(kParamSwapWindowPosition)->Value() / 100.0);
  bool invertUp = (int)GetParam(kParamInvertUpstream)->Value() != 0;

  for (MagniPhaseEngine* eng : { &mEngineL, &mEngineR })
  {
    eng->Init(fftSize, overlap);
    eng->SetWindowCycles(windowCycles);
    eng->SetWindowPixelAmount(windowPixel);
    eng->SetMagMirror(magMirror);
    eng->SetPhaseMirror(phaseMirror);
    eng->SetFreqSwap(freqSwap);
    eng->SetSwapWindowSize(swapSize);
    eng->SetSwapWindowPosition(swapPos);
    eng->SetInvertUpstream(invertUp);
  }

  mGlitchEngine.Init(GetSampleRate());
  mGlitchEngine.SetFreezeTime((float)GetParam(kParamGlitchFreezeTime)->Value());
  mGlitchEngine.SetFreezeSync((int)GetParam(kParamGlitchFreezeSync)->Value() != 0);
  mGlitchEngine.SetFreezeNoteValue((int)GetParam(kParamGlitchFreezeNote)->Value());
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
      // A partir de 2048 (inclus), force automatiquement l'overlap a 75%
      // (4x) - necessaire pour rester "propre" a ces tailles (voir
      // discussion sur la pulsation residuelle liee au hop).
      if ((int)GetParam(kParamFFTSize)->Value() >= 2 && (int)GetParam(kParamOverlap)->Value() != 1)
        GetParam(kParamOverlap)->Set(1.);
      UpdateEngineParams();
      break;
    case kParamOverlap:
      UpdateEngineParams(); // reinitialise les deux moteurs (taille FFT/overlap changes)
      break;
    case kParamWindowCycles:
    {
      float v = (float)GetParam(kParamWindowCycles)->Value();
      mEngineL.SetWindowCycles(v);
      mEngineR.SetWindowCycles(v);
      break;
    }
    case kParamWindowPixelLevels:
    {
      float v = (float)(GetParam(kParamWindowPixelLevels)->Value() / 100.0);
      mEngineL.SetWindowPixelAmount(v);
      mEngineR.SetWindowPixelAmount(v);
      break;
    }
    case kParamMagMirror:
    {
      float v = (float)(GetParam(kParamMagMirror)->Value() / 100.0);
      mEngineL.SetMagMirror(v);
      mEngineR.SetMagMirror(v);
      break;
    }
    case kParamPhaseMirror:
    {
      float v = (float)(GetParam(kParamPhaseMirror)->Value() / 100.0);
      mEngineL.SetPhaseMirror(v);
      mEngineR.SetPhaseMirror(v);
      break;
    }
    case kParamFreqSwap:
    {
      float v = (float)(GetParam(kParamFreqSwap)->Value() / 100.0);
      mEngineL.SetFreqSwap(v);
      mEngineR.SetFreqSwap(v);
      break;
    }
    case kParamSwapWindowSize:
    {
      float v = (float)(GetParam(kParamSwapWindowSize)->Value() / 100.0);
      mEngineL.SetSwapWindowSize(v);
      mEngineR.SetSwapWindowSize(v);
      break;
    }
    case kParamSwapWindowPosition:
    {
      float v = (float)(GetParam(kParamSwapWindowPosition)->Value() / 100.0);
      mEngineL.SetSwapWindowPosition(v);
      mEngineR.SetSwapWindowPosition(v);
      break;
    }
    case kParamInvertUpstream:
    {
      bool v = (int)GetParam(kParamInvertUpstream)->Value() != 0;
      mEngineL.SetInvertUpstream(v);
      mEngineR.SetInvertUpstream(v);
      break;
    }
    case kParamGlitchFreezeTime:
      mGlitchEngine.SetFreezeTime((float)GetParam(kParamGlitchFreezeTime)->Value());
      break;
    case kParamGlitchFreezeSync:
      mGlitchEngine.SetFreezeSync((int)GetParam(kParamGlitchFreezeSync)->Value() != 0);
      break;
    case kParamGlitchFreezeNote:
      mGlitchEngine.SetFreezeNoteValue((int)GetParam(kParamGlitchFreezeNote)->Value());
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
  // Canaux 0/1 = entree principale stereo. Canal 2 (si present) = Aux
  // mono, le second bus declare dans PLUG_CHANNEL_IO - utilise UNIQUEMENT
  // comme declencheur du glitch, jamais mixe dans la sortie audio.
  mInBufL.resize(nFrames);
  mInBufR.resize(nFrames);
  mOutBufL.resize(nFrames);
  mOutBufR.resize(nFrames);
  mSidechainBuf.resize(nFrames);
  mGlitchOutBufL.resize(nFrames);
  mGlitchOutBufR.resize(nFrames);

  bool hasSidechain = (inputs[2] != nullptr);

  for (int i = 0; i < nFrames; i++)
  {
    mInBufL[i] = (float)inputs[0][i];
    mInBufR[i] = (float)inputs[1][i];
    mSidechainBuf[i] = hasSidechain ? (float)inputs[2][i] : 0.f;
  }

  // 1) Traitement spectral (un moteur independant par canal)
  mEngineL.Process(mInBufL.data(), mOutBufL.data(), nFrames);
  mEngineR.Process(mInBufR.data(), mOutBufR.data(), nFrames);

  // 2) Glitch temporel stereo, applique sur le resultat, declenche par
  // l'Aux (les deux canaux sont geres ENSEMBLE pour garder l'image
  // stereo coherente). Tempo hote fourni a chaque bloc (utilise
  // uniquement si le mode Sync est actif).
  mGlitchEngine.SetHostTempo(GetTempo());
  mGlitchEngine.Process(mOutBufL.data(), mOutBufR.data(),
                         hasSidechain ? mSidechainBuf.data() : nullptr,
                         mGlitchOutBufL.data(), mGlitchOutBufR.data(), nFrames);

  for (int i = 0; i < nFrames; i++)
  {
    outputs[0][i] = mGlitchOutBufL[i];
    outputs[1][i] = mGlitchOutBufR[i];
  }
}

#endif
