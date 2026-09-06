#pragma once

#include <vector>
#include <cmath>
#include <cstdlib>
#include <algorithm>

// ============================================================================
// GlitchEngine
//
// Reproduit l'artefact classique de dissimulation de perte de paquets
// (VoIP/WhatsApp) : capture un tres court fragment du signal ("la photo",
// duree fixe), puis le boucle rapidement avant de relacher.
//
// DEUX sources de declenchement INDEPENDANTES, qui ne se melangent pas :
//
//  - SIDE-CHAIN (Freeze Time) : une vraie enveloppe qui ne fait RIEN sans
//    signal recu sur l'entree Aux. Le glitch reste actif tant que le
//    side-chain est present, puis continue encore "Freeze Time" ms apres
//    sa disparition (temps de maintien) avant de relacher.
//
//  - DECLENCHEMENT INTERNE (Poisson/Rafales/Duree variable) : totalement
//    independant du side-chain, avec sa PROPRE duree courte (decorellee
//    de Freeze Time) et un taux volontairement RARE par defaut - l'effet
//    "probleme de connexion" doit laisser le son intact la tres grande
//    majorite du temps, ponctue de temps en temps seulement.
//
// Un interrupteur general (SetEnabled) desactive tout le module d'un coup
// (passthrough pur).
// ============================================================================

class GlitchEngine
{
public:
  enum class Mode { Poisson = 0, Rafales = 1, DureeVariable = 2 };

  void Init(double sampleRate)
  {
    mSampleRate = sampleRate;
    mFragmentSamples = std::max(4, (int)(kFragmentMs * 0.001 * sampleRate));
    mFragmentBuf.assign(mFragmentSamples, 0.f);
    mState = State::Idle;
    mTriggerSource = Source::None;
    mPendingBurstCount = 0;
    mSidechainHangoverSamples = 0;
  }

  void SetEnabled(bool enabled) { mEnabled = enabled; }

  // Temps de maintien (ms) APRES la disparition du signal side-chain,
  // avant de relacher. Ne concerne QUE le side-chain.
  void SetFreezeTime(float ms) { mFreezeTimeMs = std::clamp(ms, 20.f, 10000.f); }

  // Vitesse du declenchement aleatoire INTERNE, 0 (tres rare) a 1 (rare a
  // occasionnel - volontairement jamais "frequent", pour que le son reste
  // intact la plupart du temps).
  void SetRandomRate(float rate01)
  {
    rate01 = std::clamp(rate01, 0.f, 1.f);
    mEventsPerSecond = 0.01f + rate01 * 0.59f; // ~1 tous les 100s -> ~1 toutes les 1.7s au max
  }

  void SetGlitchMode(int mode) { mGlitchMode = std::clamp(mode, 0, 2); }

  // in = signal a traiter, sidechain = signal de declenchement (peut etre
  // nullptr si aucune side-chain n'est disponible), out = sortie.
  void Process(const float* in, const float* sidechain, float* out, int nFrames)
  {
    for (int i = 0; i < nFrames; i++)
    {
      if (!mEnabled)
      {
        out[i] = in[i];
        continue;
      }

      // --- Side-chain : vraie enveloppe, ne fait rien sans signal ---
      bool sidechainActive = sidechain && std::abs(sidechain[i]) > kSidechainThreshold;

      if (sidechainActive)
      {
        if (mState == State::Idle)
          TriggerGlitch(Source::Sidechain);
        mSidechainHangoverSamples = (int)(mFreezeTimeMs * 0.001 * mSampleRate);
      }
      else if (mTriggerSource == Source::Sidechain && mState != State::Idle)
      {
        if (mSidechainHangoverSamples > 0)
          mSidechainHangoverSamples--;
        else
          mState = State::Idle; // signal disparu + temps de maintien ecoule
      }

      // --- Declenchement interne, independant, seulement si rien d'actif ---
      if (mState == State::Idle)
      {
        float probPerSample = mEventsPerSecond / (float)mSampleRate;
        float r = (float)std::rand() / (float)RAND_MAX;
        if (r < probPerSample)
        {
          TriggerGlitch(Source::Internal);
          mInternalSamplesRemaining = ComputeInternalDurationSamples();
          if (mGlitchMode == (int)Mode::Rafales)
            mPendingBurstCount = 1 + (std::rand() % 3);
        }
      }

      float sample = in[i];

      switch (mState)
      {
        case State::Idle:
          out[i] = sample;
          break;

        case State::Capturing:
          mFragmentBuf[mCaptureIdx] = sample;
          out[i] = sample; // passthrough pendant la capture (tres brieve)
          mCaptureIdx++;
          if (mCaptureIdx >= mFragmentSamples)
          {
            SmoothLoopSeam();
            mCaptureIdx = 0;
            mState = State::Looping;
            mLoopReadPos = 0;
          }
          break;

        case State::Looping:
          out[i] = mFragmentBuf[mLoopReadPos];
          mLoopReadPos = (mLoopReadPos + 1) % mFragmentSamples;

          // La sortie de boucle du side-chain est geree plus haut (enveloppe).
          // Ici, seule la duree INTERNE est decomptee.
          if (mTriggerSource == Source::Internal)
          {
            mInternalSamplesRemaining--;
            if (mInternalSamplesRemaining <= 0)
            {
              if (mPendingBurstCount > 0)
              {
                mPendingBurstCount--;
                TriggerGlitch(Source::Internal);
                mInternalSamplesRemaining = ComputeInternalDurationSamples();
              }
              else
              {
                mState = State::Idle;
              }
            }
          }
          break;
      }
    }
  }

private:
  enum class State { Idle, Capturing, Looping };
  enum class Source { None, Sidechain, Internal };

  void TriggerGlitch(Source source)
  {
    mState = State::Capturing;
    mCaptureIdx = 0;
    mTriggerSource = source;
  }

  int ComputeInternalDurationSamples() const
  {
    float ms = kInternalBaseMs;
    if (mGlitchMode == (int)Mode::DureeVariable)
    {
      float factor = 0.3f + ((float)std::rand() / (float)RAND_MAX) * 1.7f; // 0.3x a 2x
      ms = std::clamp(kInternalBaseMs * factor, 30.f, 2000.f);
    }
    return (int)(ms * 0.001f * mSampleRate);
  }

  // Lisse le point de bouclage du fragment capture (evite un clic a
  // chaque repetition) - meme principe que le raccord de boucle des
  // tables d'onde de TroisCorpsWave.
  void SmoothLoopSeam()
  {
    int fadeLen = std::max(2, mFragmentSamples / 10);
    for (int i = 0; i < fadeLen; i++)
    {
      float t = (float)i / (float)fadeLen;
      int idx = mFragmentSamples - fadeLen + i;
      mFragmentBuf[idx] = mFragmentBuf[idx] * (1.f - t) + mFragmentBuf[0] * t;
    }
  }

  State mState = State::Idle;
  Source mTriggerSource = Source::None;

  static constexpr float kFragmentMs = 15.f;      // taille fixe de la "photo"
  static constexpr float kInternalBaseMs = 150.f; // duree de base des glitches internes (independante de Freeze Time)
  static constexpr float kSidechainThreshold = 0.05f;

  bool mEnabled = false;

  double mSampleRate = 44100.0;
  int mFragmentSamples = 661;
  std::vector<float> mFragmentBuf;
  int mCaptureIdx = 0;
  int mLoopReadPos = 0;

  float mFreezeTimeMs = 200.f;   // side-chain uniquement (temps de maintien)
  int mSidechainHangoverSamples = 0;

  float mEventsPerSecond = 0.01f;
  int mGlitchMode = 0;
  int mInternalSamplesRemaining = 0;
  int mPendingBurstCount = 0;
};
