#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

// ============================================================================
// MagniPhaseEngine
//
// Moteur STFT auto-contenu (meme principe que StftCrossSynth de Magnitude1) :
// FFT maison, ring buffer, chevauchement-addition (overlap-add). Etape 1 :
// extrait magnitude/phase de chaque bande, reconstruit A L'IDENTIQUE (aucune
// modification) - valide le pipeline avant d'ajouter des operations.
//
// Particularite : au lieu d'une fenetre de Hann fixe, une BANQUE de 10
// fenetres de Tukey (du plus doux, ~Hann, au quasi-rectangulaire a pente
// tres raide) est utilisee, avec un morphing continu entre elles. Note :
// les tapers extremes (proches du rectangulaire) ne respectent plus
// parfaitement la propriete de recouvrement constant (COLA) qu'a Hann -
// c'est volontaire ici (recherche d'un caractere "derangeant"/distordu),
// pas une erreur a corriger.
// ============================================================================

class MagniPhaseEngine
{
public:
  using cplx = std::complex<float>;

  static constexpr int kNumWindows = 10;

  MagniPhaseEngine() { Init(1024, 2); }

  void Init(int fftSize, int overlapFactor)
  {
    mFFTSize = fftSize;
    mOverlap = overlapFactor;
    mHopSize = mFFTSize / mOverlap;

    mRingIn.assign(mFFTSize, 0.f);
    mRingOut.assign(mFFTSize, 0.f);
    mTimeBuf.resize(mFFTSize);
    mCplxBuf.assign(mFFTSize, cplx(0.f, 0.f));

    BuildWindowBank();

    mWritePos = 0;
    mReadPos = 0;
    mSamplesUntilHop = mHopSize;
  }

  // Position de morph dans la banque de fenetres : 0 = la plus douce
  // (quasi-Hann), kNumWindows-1 = la plus extreme (quasi-rectangulaire).
  // Continue : entre deux entiers, melange lineaire des deux voisines.
  void SetWindowMorph(float morphPos)
  {
    mWindowMorph = std::clamp(morphPos, 0.f, (float)(kNumWindows - 1));
  }

  void Process(const float* in, float* out, int nFrames)
  {
    for (int i = 0; i < nFrames; i++)
    {
      mRingIn[mWritePos] = in[i];

      out[i] = mRingOut[mReadPos];
      mRingOut[mReadPos] = 0.f;

      mWritePos = (mWritePos + 1) % mFFTSize;
      mReadPos = (mReadPos + 1) % mFFTSize;

      if (--mSamplesUntilHop == 0)
      {
        mSamplesUntilHop = mHopSize;
        ProcessHop();
      }
    }
  }

private:
  void BuildWindowBank()
  {
    mWindowBank.assign(kNumWindows, std::vector<float>(mFFTSize));

    for (int w = 0; w < kNumWindows; w++)
    {
      // Taper de 1.0 (le plus doux) a 0.05 (quasi rectangulaire, pente
      // tres raide) - couvre toute la famille demandee, extremes inclus.
      float taper = 1.0f - (float)w * (1.0f - 0.05f) / (float)(kNumWindows - 1);
      BuildTukeyWindow(mWindowBank[w], taper);
    }
  }

  void BuildTukeyWindow(std::vector<float>& dst, float taper)
  {
    int N = mFFTSize;
    for (int i = 0; i < N; i++)
    {
      float x = (float)i / (float)(N - 1);
      float w;

      if (x < taper * 0.5f)
        w = 0.5f * (1.f + std::cos(kPi * (2.f * x / taper - 1.f)));
      else if (x <= 1.f - taper * 0.5f)
        w = 1.f;
      else
        w = 0.5f * (1.f + std::cos(kPi * (2.f * x / taper - 2.f / taper + 1.f)));

      dst[i] = w;
    }
  }

  // Echantillon de la fenetre courante (position i), obtenu par melange
  // lineaire entre les deux entrees voisines de la banque selon mWindowMorph.
  float GetWindowSample(int i) const
  {
    int i0 = (int)mWindowMorph;
    int i1 = std::min(i0 + 1, kNumWindows - 1);
    float frac = mWindowMorph - (float)i0;
    return mWindowBank[i0][i] * (1.f - frac) + mWindowBank[i1][i] * frac;
  }

  void ReadRingIntoLinear(const std::vector<float>& ring, std::vector<float>& dst)
  {
    int start = mWritePos;
    for (int i = 0; i < mFFTSize; i++)
      dst[i] = ring[(start + i) % mFFTSize];
  }

  static void FFT(std::vector<cplx>& a, bool invert)
  {
    int n = (int)a.size();
    for (int i = 1, j = 0; i < n; i++)
    {
      int bit = n >> 1;
      for (; j & bit; bit >>= 1)
        j ^= bit;
      j ^= bit;
      if (i < j) std::swap(a[i], a[j]);
    }

    for (int len = 2; len <= n; len <<= 1)
    {
      float ang = 2.f * kPi / (float)len * (invert ? 1.f : -1.f);
      cplx wlen(std::cos(ang), std::sin(ang));
      for (int i = 0; i < n; i += len)
      {
        cplx w(1.f, 0.f);
        for (int k = 0; k < len / 2; k++)
        {
          cplx u = a[i + k];
          cplx v = a[i + k + len / 2] * w;
          a[i + k] = u + v;
          a[i + k + len / 2] = u - v;
          w *= wlen;
        }
      }
    }

    if (invert)
    {
      for (auto& x : a)
        x /= (float)n;
    }
  }

  void ProcessHop()
  {
    ReadRingIntoLinear(mRingIn, mTimeBuf);

    for (int i = 0; i < mFFTSize; i++)
      mCplxBuf[i] = cplx(mTimeBuf[i] * GetWindowSample(i), 0.f);

    FFT(mCplxBuf, false);

    // Etape 1 : extraction magnitude/phase puis reconstruction A
    // L'IDENTIQUE (aucune modification) - valide le pipeline complet
    // avant d'ajouter les operations des etapes suivantes.
    int numBins = mFFTSize / 2;
    for (int k = 0; k <= numBins; k++)
    {
      float mag = std::abs(mCplxBuf[k]);
      float phase = std::arg(mCplxBuf[k]);

      cplx val(mag * std::cos(phase), mag * std::sin(phase));
      mCplxBuf[k] = val;
      if (k > 0 && k < numBins)
        mCplxBuf[mFFTSize - k] = std::conj(val);
    }

    FFT(mCplxBuf, true);

    float normOverlap = 1.f / (float)mOverlap * 2.f;
    int start = mWritePos;
    for (int i = 0; i < mFFTSize; i++)
    {
      int idx = (start + i) % mFFTSize;
      mRingOut[idx] += mCplxBuf[i].real() * GetWindowSample(i) * normOverlap;
    }
  }

  static constexpr float kPi = 3.14159265358979323846f;

  int mFFTSize = 1024;
  int mOverlap = 2;
  int mHopSize = 512;
  int mSamplesUntilHop = 512;
  int mWritePos = 0;
  int mReadPos = 0;

  float mWindowMorph = 0.f;
  std::vector<std::vector<float>> mWindowBank;

  std::vector<float> mRingIn, mRingOut;
  std::vector<float> mTimeBuf;
  std::vector<cplx> mCplxBuf;
};
