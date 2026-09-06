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
    mCurrentWindow.assign(mFFTSize, 0.f);
    mWindowUIBuf.assign(mFFTSize, 0.f);

    mWindowDirty = true;
    RebuildWindowIfNeeded();

    mWritePos = 0;
    mReadPos = 0;
    mSamplesUntilHop = mHopSize;
  }

  // Fenetre generee en direct a partir de 2 parametres, comme dans le
  // patch Pure Data de reference :
  //  - Cycles : nombre d'oscillations a travers la fenetre
  //  - Pixel  : resolution de quantification (bas = anguleux/triangulaire,
  //    haut = lisse) - applique un effet d'escalier sur la courbe brute
  //    avant de l'envelopper (garantit toujours 0 aux deux bouts).
  //
  // Cycles est mis a l'echelle par rapport a la taille FFT (reference :
  // 1024) : une meme valeur de Cycles couvre alors une duree reelle
  // comparable, quelle que soit la taille FFT choisie - sans ca, une
  // fenetre plus grande "etale" les memes cycles sur plus de temps reel,
  // changeant le caractere du son pour un meme reglage de bouton.
  void SetWindowCycles(float cycles)
  {
    cycles = std::max(1.f, cycles);
    if (cycles != mWindowCyclesBase) { mWindowCyclesBase = cycles; mWindowDirty = true; }
  }

  // Quantite de "pixelisation" (0 = aucune, sinus parfait ; 1 = tres
  // anguleux/triangulaire). A 0 exactement, la quantification est
  // completement court-circuitee : garantit un sinus mathematiquement
  // parfait, pas juste une approximation tres fine.
  void SetWindowPixelAmount(float amount01)
  {
    amount01 = std::clamp(amount01, 0.f, 1.f);
    if (amount01 != mWindowPixelAmount) { mWindowPixelAmount = amount01; mWindowDirty = true; }
  }

  // Pour l'affichage (WindowPreviewControl) : derniere fenetre generee,
  // copiee cote thread audio a chaque reconstruction (voir OnIdle cote
  // plugin pour la lecture cote interface).
  const float* GetWindowForUI() const { return mWindowUIBuf.data(); }
  int GetWindowSizeForUI() const { return mFFTSize; }
  bool WindowUIUpdated() { bool u = mWindowUIUpdated; mWindowUIUpdated = false; return u; }

  // Position du melange normal <-> miroir, 0..1, independant pour
  // magnitude et phase.
  void SetMagMirror(float t) { mMagMirror = std::clamp(t, 0.f, 1.f); }
  void SetPhaseMirror(float t) { mPhaseMirror = std::clamp(t, 0.f, 1.f); }
  void SetFreqSwap(float t) { mFreqSwap = std::clamp(t, 0.f, 1.f); }
  void SetSwapWindowSize(float size) { mSwapWindowSize = std::clamp(size, 0.f, 1.f); }

  // Position de la fenetre de Freq Swap, avec deformation non-lineaire :
  // les 50% premiers du parcours du bouton couvrent les 15% premiers de
  // la valeur reelle (zone la plus sensible/utile, dilatee pour plus de
  // precision), le reste suit une courbe exponentielle. rawT = position
  // brute du bouton (0-1, linaire, ce que le parametre iPlug2 envoie).
  void SetSwapWindowPosition(float rawT)
  {
    rawT = std::clamp(rawT, 0.f, 1.f);
    constexpr float kSplitKnob = 0.5f;   // 50% du bouton...
    constexpr float kSplitValue = 0.15f; // ...= 15% premiers de la valeur
    constexpr float kExpPower = 2.5f;    // durete de la courbe sur le reste

    if (rawT <= kSplitKnob)
      mSwapWindowPosition = (rawT / kSplitKnob) * kSplitValue;
    else
    {
      float s = (rawT - kSplitKnob) / (1.f - kSplitKnob);
      mSwapWindowPosition = kSplitValue + (1.f - kSplitValue) * std::pow(s, kExpPower);
    }
  }

  void SetInvertUpstream(bool invert) { mInvertUpstream = invert; }

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
  // Reconstruit la fenetre si Cycles/Pixel ont change depuis la derniere
  // fois. Principe (identique au patch PD de reference) : un oscillateur
  // multi-lobes (nombre de cycles reglable), enveloppe pour garantir 0 aux
  // deux bouts, puis quantifie (nombre de paliers regle par Pixel) pour
  // obtenir l'effet "pixelise"/anguleux/triangulaire a basse resolution.
  void RebuildWindowIfNeeded()
  {
    if (!mWindowDirty) return;
    mWindowDirty = false;

    // Cycles mis a l'echelle par rapport a la taille FFT (reference 1024).
    float effectiveCycles = mWindowCyclesBase * ((float)mFFTSize / 1024.f);

    // Pixel : 0 = sinus parfait (pas de quantification), 1 = tres
    // anguleux (2 paliers seulement).
    float levels = 256.f - mWindowPixelAmount * (256.f - 2.f);

    int N = mFFTSize;
    for (int i = 0; i < N; i++)
    {
      float x = (float)i / (float)(N - 1);
      float raw = std::abs(std::sin(kPi * effectiveCycles * x));

      float quantized = (mWindowPixelAmount <= 0.0001f)
        ? raw // court-circuite la quantification : sinus mathematiquement parfait
        : std::round(raw * levels) / levels;

      float envelope = std::sin(kPi * x); // garantit 0 aux deux bouts
      mCurrentWindow[i] = envelope * quantized;
    }

    // Copie pour l'affichage (thread interface, lu via GetWindowForUI()).
    mWindowUIBuf = mCurrentWindow;
    mWindowUIUpdated = true;
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
    RebuildWindowIfNeeded();

    ReadRingIntoLinear(mRingIn, mTimeBuf);

    for (int i = 0; i < mFFTSize; i++)
      mCplxBuf[i] = cplx(mTimeBuf[i] * mCurrentWindow[i], 0.f);

    FFT(mCplxBuf, false);

    // Etape 3 : Freq Swap (echange grave/aigu, en tete de chaine - une
    // restructuration de position plus fondamentale que les deux effets
    // "Mirror" qui suivent), puis Mag Mirror, puis Phase Mirror, avec
    // compensation automatique de gain (le volume percu peut fortement
    // chuter quand la magnitude s'aplatit et/ou que les phases s'alignent).
    int numBins = mFFTSize / 2;

    // Passe 1 : extrait magnitude/phase brutes, calcule la moyenne du bloc
    // (point de symetrie du Mag Mirror ET de l'inversion en amont) et
    // l'energie d'origine.
    float sumMag = 0.f, origEnergy = 0.f;
    for (int k = 0; k <= numBins; k++)
    {
      mMagBuf[k] = std::abs(mCplxBuf[k]);
      mPhaseBuf[k] = std::arg(mCplxBuf[k]);
      sumMag += mMagBuf[k];
      origEnergy += mMagBuf[k] * mMagBuf[k];
    }
    float avgMag = sumMag / (float)(numBins + 1);

    // Passe 1bis : inversion complete EN AMONT (magnitude autour de la
    // moyenne, phase autour de zero) - si activee, etablit une nouvelle
    // base sur laquelle Freq Swap et Mag Mirror agiront ensuite.
    if (mInvertUpstream)
    {
      for (int k = 0; k <= numBins; k++)
      {
        mMagBuf[k] = std::max(0.f, 2.f * avgMag - mMagBuf[k]);
        mPhaseBuf[k] = -mPhaseBuf[k];
      }
    }

    // Passe 2 : Freq Swap (magnitude seulement - la phase de chaque bande
    // reste toujours a sa place d'origine), applique uniquement A
    // L'INTERIEUR d'une fenetre reglable (taille + position dans le
    // spectre), miroir autour du CENTRE DE LA FENETRE. En dehors de la
    // fenetre : inchange.
    int windowBins = std::max(2, (int)std::round(mSwapWindowSize * (float)numBins));
    int windowStart = (int)std::round(mSwapWindowPosition * (float)(numBins - windowBins));
    int windowEnd = windowStart + windowBins;

    for (int k = 0; k <= numBins; k++)
    {
      if (k >= windowStart && k <= windowEnd)
      {
        int partner = windowStart + (windowEnd - k);
        float swappedMag = mMagBuf[partner];
        mMagBuf2[k] = mMagBuf[k] * (1.f - mFreqSwap) + swappedMag * mFreqSwap;
      }
      else
      {
        mMagBuf2[k] = mMagBuf[k];
      }
    }

    // Passe 3 : Mag Mirror, applique sur le resultat du Freq Swap.
    float newEnergy = 0.f;
    for (int k = 0; k <= numBins; k++)
    {
      float magMirrored = std::max(0.f, 2.f * avgMag - mMagBuf2[k]);
      float finalMag = mMagBuf2[k] * (1.f - mMagMirror) + magMirrored * mMagMirror;
      mMagBuf[k] = finalMag;
      newEnergy += finalMag * finalMag;
    }

    // Compensation de gain : ramene l'energie du bloc a ce qu'elle etait
    // avant les transformations. PLAFONNEE volontairement (0.25x a 4x)
    // pour eviter tout emballement, plus une rattrape calibree
    // specifiquement sur le Phase Mirror (l'energie seule ne suffit pas a
    // compenser sa perte de crete, voir GetPhaseMirrorMakeupGain()).
    float gain = std::sqrt(origEnergy / std::max(newEnergy, 1e-9f));
    gain = std::clamp(gain, 0.25f, 4.f);
    gain *= GetPhaseMirrorMakeupGain();

    // Passe finale : Phase Mirror (sur la phase issue de la passe 1bis,
    // inversee ou non selon Invert Upstream) puis reconstruction.
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
      mRingOut[idx] += mCplxBuf[i].real() * mCurrentWindow[i] * normOverlap;
    }
  }

  // Rattrape de gain calibree empiriquement sur des mesures reelles :
  // Phase Mirror provoque une perte de crete que la compensation d'energie
  // (Parseval) ne couvre pas (l'energie totale ne depend pas de la phase,
  // mais le niveau de crete si). Points mesures : 0% -> -8dB, 50% -> -24.5dB
  // (perte max), 100% -> -13.5dB (remonte partiellement). Interpolation
  // lineaire en dB entre ces 3 points de reference.
  float GetPhaseMirrorMakeupGain() const
  {
    float t = mPhaseMirror;
    float boostDb;
    if (t <= 0.5f)
      boostDb = 16.5f * (t / 0.5f);
    else
      boostDb = 16.5f + (5.5f - 16.5f) * ((t - 0.5f) / 0.5f);

    return std::pow(10.f, boostDb / 20.f);
  }

  static constexpr float kPi = 3.14159265358979323846f;

  int mFFTSize = 1024;
  int mOverlap = 2;
  int mHopSize = 512;
  int mSamplesUntilHop = 512;
  int mWritePos = 0;
  int mReadPos = 0;

  float mWindowCyclesBase = 1.f;
  float mWindowPixelAmount = 0.f;
  bool mWindowDirty = true;
  std::vector<float> mCurrentWindow;
  std::vector<float> mWindowUIBuf;
  bool mWindowUIUpdated = false;

  std::vector<float> mRingIn, mRingOut;
  std::vector<float> mTimeBuf;
  std::vector<cplx> mCplxBuf;
  std::vector<float> mMagBuf, mMagBuf2, mPhaseBuf;

  float mMagMirror = 0.f;
  float mPhaseMirror = 0.f;
  float mFreqSwap = 0.f;
  float mSwapWindowSize = 1.f;     // 1 = tout le spectre (comportement d'origine)
  float mSwapWindowPosition = 0.f; // 0 = fenetre collee au grave
  bool mInvertUpstream = false;    // inversion complete magnitude+phase, en amont de tout le reste
};
