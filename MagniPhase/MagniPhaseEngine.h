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

  static constexpr int kNumWindows = 24;
  static constexpr int kZone1Count = 8;  // Tukey : douce -> quasi-rectangulaire
  static constexpr int kZone2Count = 8;  // lobes multiples, nombre croissant
  static constexpr int kZone3Count = kNumWindows - kZone1Count - kZone2Count; // formes complexes/asymetriques

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
    mMagBuf.assign(mFFTSize, 0.f);
    mMagBuf2.assign(mFFTSize, 0.f);
    mPhaseBuf.assign(mFFTSize, 0.f);

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

  // Position du melange normal <-> miroir, 0..1, independant pour
  // magnitude et phase.
  void SetMagMirror(float t) { mMagMirror = std::clamp(t, 0.f, 1.f); }
  void SetPhaseMirror(float t) { mPhaseMirror = std::clamp(t, 0.f, 1.f); }
  void SetFreqSwap(float t) { mFreqSwap = std::clamp(t, 0.f, 1.f); }

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

    // --- Zone 1 : Tukey, douce -> quasi-rectangulaire. Distribution en
    // racine carree (plutot que lineaire) : le durcissement de la pente
    // arrive plus vite en tournant le bouton, au lieu d'etre etale sur
    // toute la premiere moitie de sa course.
    for (int w = 0; w < kZone1Count; w++)
    {
      float t = (kZone1Count > 1) ? (float)w / (float)(kZone1Count - 1) : 0.f;
      float taper = 1.0f - std::sqrt(t) * (1.0f - 0.02f);
      BuildTukeyWindow(mWindowBank[w], taper);
    }

    // --- Zone 2 : nombre de lobes croissant. Toujours strictement nul aux
    // deux extremites (enveloppe en sin(pi*x)) pour eviter tout clic au
    // raccord des blocs, meme quand le nombre de cycles n'est pas entier
    // (position intermediaire pendant un morph).
    for (int w = 0; w < kZone2Count; w++)
    {
      float t = (kZone2Count > 1) ? (float)w / (float)(kZone2Count - 1) : 0.f;
      float cycles = t * 9.f; // jusqu'a ~9 lobes supplementaires
      BuildLobedWindow(mWindowBank[kZone1Count + w], cycles);
    }

    // --- Zone 3 : formes complexes et asymetriques (plusieurs frequences
    // non-entieres, dephasages fixes) - complexite croissante.
    for (int w = 0; w < kZone3Count; w++)
    {
      float t = (kZone3Count > 1) ? (float)w / (float)(kZone3Count - 1) : 0.f;
      BuildComplexWindow(mWindowBank[kZone1Count + kZone2Count + w], t);
    }
  }

  void BuildLobedWindow(std::vector<float>& dst, float cycles)
  {
    int N = mFFTSize;
    for (int i = 0; i < N; i++)
    {
      float x = (float)i / (float)(N - 1);
      float envelope = std::sin(kPi * x); // garantit 0 aux deux bouts, toujours
      float raw = std::abs(std::sin(kPi * (cycles + 1.f) * x));
      dst[i] = envelope * raw;
    }
  }

  void BuildComplexWindow(std::vector<float>& dst, float complexity)
  {
    int N = mFFTSize;
    int numHarmonics = 2 + (int)(complexity * 4.f); // de 2 a 6 composantes

    for (int i = 0; i < N; i++)
    {
      float x = (float)i / (float)(N - 1);
      float envelope = std::sin(kPi * x); // garantit 0 aux deux bouts

      float sum = 0.f, wsum = 0.f;
      for (int h = 0; h < numHarmonics; h++)
      {
        // Frequences non-entieres et dephasages fixes croissants : casse
        // volontairement la symetrie, forme non-periodique/organique.
        float freq = 1.3f + (float)h * 1.7f;
        float phase = (float)h * 0.9f;
        float amp = 1.f / (float)(h + 1);
        sum += amp * std::sin(2.f * kPi * freq * x + phase);
        wsum += amp;
      }
      sum /= wsum; // ramene approximativement a -1..1

      dst[i] = envelope * (0.5f + 0.5f * sum * complexity);
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

    // Etape 3 : effets "Mirror" (magnitude/phase) et "Freq Swap" (echange
    // grave/aigu, phase inchangee), avec compensation automatique de gain
    // (le volume percu peut fortement chuter quand la magnitude s'aplatit
    // et/ou que les phases s'alignent - on remesure l'energie avant/apres
    // et on rescale pour rester a niveau comparable, quel que soit le
    // melange de reglages utilise).
    int numBins = mFFTSize / 2;

    // Passe 1 : extrait magnitude/phase brutes, calcule la moyenne du bloc
    // (point de symetrie du miroir) et l'energie d'origine.
    float sumMag = 0.f, origEnergy = 0.f;
    for (int k = 0; k <= numBins; k++)
    {
      mMagBuf[k] = std::abs(mCplxBuf[k]);
      mPhaseBuf[k] = std::arg(mCplxBuf[k]);
      sumMag += mMagBuf[k];
      origEnergy += mMagBuf[k] * mMagBuf[k];
    }
    float avgMag = sumMag / (float)(numBins + 1);

    // Passe 2 : melange normal <-> miroir (magnitude), autour de la
    // moyenne. Resultat dans mMagBuf2 (mMagBuf reste intact, encore
    // necessaire tel quel pour l'echange de frequence juste apres).
    for (int k = 0; k <= numBins; k++)
    {
      float magMirrored = std::max(0.f, 2.f * avgMag - mMagBuf[k]);
      mMagBuf2[k] = mMagBuf[k] * (1.f - mMagMirror) + magMirrored * mMagMirror;
    }

    // Passe 3 : melange normal <-> echange grave/aigu (chaque bande
    // echange sa magnitude avec sa bande miroir a l'autre bout du
    // spectre) - la phase, elle, reste toujours a sa place d'origine.
    // Resultat final (avant compensation de gain) dans mMagBuf.
    float newEnergy = 0.f;
    for (int k = 0; k <= numBins; k++)
    {
      float swapped = mMagBuf2[numBins - k];
      float finalMag = mMagBuf2[k] * (1.f - mFreqSwap) + swapped * mFreqSwap;
      mMagBuf[k] = finalMag;
      newEnergy += finalMag * finalMag;
    }

    // Compensation de gain : ramene l'energie du bloc a ce qu'elle etait
    // avant les transformations, quel que soit le melange de reglages.
    float gain = std::sqrt(origEnergy / std::max(newEnergy, 1e-9f));

    // Passe finale : applique le gain de compensation, le miroir de phase,
    // et reconstruit.
    for (int k = 0; k <= numBins; k++)
    {
      float mag = mMagBuf[k] * gain;
      float phase = mPhaseBuf[k];

      float phaseMirrored = -phase;
      phase = phase * (1.f - mPhaseMirror) + phaseMirrored * mPhaseMirror;

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
  std::vector<float> mMagBuf, mMagBuf2, mPhaseBuf;

  float mMagMirror = 0.f;
  float mPhaseMirror = 0.f;
  float mFreqSwap = 0.f;
};
