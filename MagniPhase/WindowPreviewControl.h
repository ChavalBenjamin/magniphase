#pragma once

#include "IControl.h"
#include <algorithm>

// ============================================================================
// WindowPreviewControl
//
// Affiche la forme de la fenetre d'analyse generee en direct (Cycles +
// Pixel), une seule courbe (valeurs toujours 0..1, pas besoin d'un
// affichage bipolaire). Mise a jour depuis le thread interface (OnIdle),
// jamais depuis l'audio.
// ============================================================================

class WindowPreviewControl : public iplug::igraphics::IControl
{
public:
  WindowPreviewControl(const iplug::igraphics::IRECT& bounds)
  : IControl(bounds)
  {
  }

  // Reechantillonne proportionnellement pour tenir dans kMaxPoints -
  // JAMAIS une simple troncature (qui ne montrerait que le debut de la
  // fenetre pour toute taille FFT superieure a kMaxPoints, coupant la fin
  // et donnant une impression de cycles incomplets).
  void SetWaveform(const float* buf, int size)
  {
    mSize = std::min(size, kMaxPoints);
    for (int i = 0; i < mSize; i++)
    {
      int srcIdx = (int)((float)i / (float)mSize * (float)size);
      srcIdx = std::min(srcIdx, size - 1);
      mBuffer[i] = buf[srcIdx];
    }
  }

  void Draw(iplug::igraphics::IGraphics& g) override
  {
    using namespace iplug::igraphics;

    g.FillRect(IColor(255, 15, 15, 20), mRECT);
    if (mSize < 2) return;

    float w = mRECT.W();
    float h = mRECT.H() * 0.85f;
    float baseY = mRECT.B - mRECT.H() * 0.08f;

    for (int i = 0; i < mSize - 1; i++)
    {
      float x0 = mRECT.L + w * (float)i / (float)(mSize - 1);
      float x1 = mRECT.L + w * (float)(i + 1) / (float)(mSize - 1);
      float y0 = baseY - mBuffer[i] * h;
      float y1 = baseY - mBuffer[i + 1] * h;
      g.DrawLine(IColor(255, 120, 200, 255), x0, y0, x1, y1, nullptr, 1.5f);
    }
  }

private:
  static constexpr int kMaxPoints = 512;
  float mBuffer[kMaxPoints] = { 0.f };
  int mSize = 0;
};
