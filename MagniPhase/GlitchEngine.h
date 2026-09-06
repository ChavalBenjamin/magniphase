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
// duree fixe), puis le boucle rapidement pendant une duree reglable
// (20ms-10s) avant de relacher. Se declenche aleatoirement en interne
// (processus de Poisson - tirage a chaque echantillon, plus organique et
// imprevisible qu'un minuteur classique) et/ou sur une impulsion recue via
// une entree side-chain.
//
// 3 modes (GlitchMode) partageant le meme reglage de vitesse :
//  0 = Poisson    : declenchement aleatoire pur, rien d'autre
//  1 = Rafales    : chaque declenchement a une chance d'enchainer 1 a 3
//                   glitches supplementaires immediatement apres
//  2 = Duree var. : la duree de gel de chaque evenement varie aleatoirement
//                   autour de la valeur reglee (jamais deux fois identique)
// ============================================================================

class GlitchEngine
{
public:
  void Init(double sampleRate)
  {
    mSampleRate = sampleRate;
    mFragmentSamples = std::max(4, (int)(kFragmentMs * 0.001 * sampleRate));
    mFragmentBuf.assign(mFragmentSamples, 0.f);
    mState = State::Idle;
    mPendingBurstCount = 0;
  }

  // Duree pendant laquelle le fragment capture est repete avant de
  // relacher, en millisecondes (20 - 10000). En mode "Duree variable",
  // c'est la MOYENNE autour de laquelle chaque evenement varie.
  void SetFreezeTime(float ms) { mFreezeTimeMs = std::clamp(ms, 20.f, 10000.f); }

  // Vitesse du declenchement aleatoire, 0 (tres rare) a 1 (frequent).
  void SetRandomRate(float rate01)
  {
    rate01 = std::clamp(rate01, 0.f, 1.f);
    mEventsPerSecond = 0.05f + rate01 * 2.95f; // ~1 tous les 20s -> ~3 par seconde
  }

  void SetGlitchMode(int mode) { mGlitchMode = std::clamp(mode, 0, 2); }

  // in = signal a traiter, sidechain = signal de declenchement (peut etre
  // nullptr si aucune side-chain n'est disponible), out = sortie.
  void Process(const float* in, const float* sidechain, float* out, int nFrames)
  {
    for (int i = 0; i < nFrames; i++)
    {
      if (mState == State::Idle)
      {
        if (sidechain && std::abs(sidechain[i]) > kSidechainThreshold)
        {
          TriggerGlitch();
        }
        else
        {
          // Processus de Poisson : probabilite de declenchement testee a
          // CHAQUE echantillon - donne naturellement de longs silences et
          // des rafales rapprochees, plutot qu'un rythme regulier.
          float probPerSample = mEventsPerSecond / (float)mSampleRate;
          float r = (float)std::rand() / (float)RAND_MAX;
          if (r < probPerSample)
          {
            TriggerGlitch();
            if (mGlitchMode == 1) // Rafales
              mPendingBurstCount = 1 + (std::rand() % 3); // 1 a 3 en plus
          }
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
            mSamplesRemaining = ComputeFreezeSamples();
          }
          break;

        case State::Looping:
          out[i] = mFragmentBuf[mLoopReadPos];
          mLoopReadPos = (mLoopReadPos + 1) % mFragmentSamples;
          mSamplesRemaining--;
          if (mSamplesRemaining <= 0)
          {
            if (mPendingBurstCount > 0)
            {
              mPendingBurstCount--;
              TriggerGlitch(); // enchaine immediatement (mode Rafales)
            }
            else
            {
              mState = State::Idle;
            }
          }
          break;
      }
    }
  }

private:
  void TriggerGlitch()
  {
    mState = State::Capturing;
    mCaptureIdx = 0;
  }

  int ComputeFreezeSamples() const
  {
    float ms = mFreezeTimeMs;
    if (mGlitchMode == 2) // Duree variable
    {
      float factor = 0.4f + ((float)std::rand() / (float)RAND_MAX) * 1.2f; // 0.4x a 1.6x
      ms = std::clamp(mFreezeTimeMs * factor, 20.f, 10000.f);
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

  enum class State { Idle, Capturing, Looping };
  State mState = State::Idle;

  static constexpr float kFragmentMs = 15.f; // taille fixe de la "photo"
  static constexpr float kSidechainThreshold = 0.05f;

  double mSampleRate = 44100.0;
  int mFragmentSamples = 661;
  std::vector<float> mFragmentBuf;
  int mCaptureIdx = 0;
  int mLoopReadPos = 0;
  int mSamplesRemaining = 0;

  float mFreezeTimeMs = 200.f;
  float mEventsPerSecond = 0.05f;
  int mGlitchMode = 0;
  int mPendingBurstCount = 0;
};
