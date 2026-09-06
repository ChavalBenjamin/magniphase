#pragma once

#include <vector>
#include <cmath>
#include <cstdlib>
#include <algorithm>

// ============================================================================
// GlitchEngine (stereo)
//
// Reproduit l'artefact classique de dissimulation de perte de paquets
// (VoIP/WhatsApp) : capture un tres court fragment stereo du signal ("la
// photo", duree fixe, LES DEUX CANAUX ENSEMBLE pour garder une image
// stereo coherente), puis le boucle avant de relacher.
//
// DEUX sources de declenchement INDEPENDANTES :
//
//  - SIDE-CHAIN (Aux, mono) : une vraie enveloppe qui ne fait RIEN sans
//    signal recu. Le glitch reste actif tant que l'Aux est present, puis
//    continue "Freeze Time" ms de plus (temps de maintien) avant de
//    relacher et reprendre le cours normal - exactement comme une "photo"
//    qui capture puis relache.
//
//  - DECLENCHEMENT INTERNE : Rate regle la frequence moyenne (de "jamais"
//    a "tres souvent", echelle logarithmique pour une sensation naturelle
//    sur tout le parcours du bouton), le Mode regle le CARACTERE de
//    chaque evenement - pense pour rester chaotique et imprevisible meme
//    une fois le mecanisme connu (seul l'Aux reste un declenchement
//    volontaire/previsible) :
//
//    0 = Poisson       : declenchements isoles, aucune structure, aucune
//                        memoire du passe.
//    1 = Rafales       : chaque declenchement a une chance ALEATOIRE de
//                        se transformer en rafale (nombre ET espacement
//                        entre les coups tous aleatoires) - jamais deux
//                        rafales identiques.
//    2 = Duree Variable: chaque evenement dure une duree tiree sur une
//                        PLAGE LARGE (d'un accroc tres bref a un
//                        decrochage nettement plus long), sans previsibilite.
//
// Un interrupteur general (SetEnabled) desactive tout le module d'un coup.
// Un filet de securite force un retour a la normale si un glitch dure
// anormalement longtemps, quelle qu'en soit la cause.
// ============================================================================

class GlitchEngine
{
public:
  enum class Mode { Poisson = 0, Rafales = 1, DureeVariable = 2 };

  void Init(double sampleRate)
  {
    mSampleRate = sampleRate;
    mFragmentSamples = std::max(4, (int)(kFragmentMs * 0.001 * sampleRate));
    mFragmentBufL.assign(mFragmentSamples, 0.f);
    mFragmentBufR.assign(mFragmentSamples, 0.f);
    mState = State::Idle;
    mTriggerSource = Source::None;
    mPendingBurstCount = 0;
    mBurstGapRemaining = 0;
    mSidechainHangoverSamples = 0;
    mSafetySamplesElapsed = 0;
    ScheduleNextInternalTrigger();
  }

  void SetEnabled(bool enabled) { mEnabled = enabled; }

  // Temps de maintien (ms) APRES la disparition du signal Aux, avant de
  // relacher. Ne concerne QUE le side-chain.
  void SetFreezeTime(float ms) { mFreezeTimeMs = std::clamp(ms, 20.f, 10000.f); }

  // Vitesse du declenchement interne, 0 (jamais) a 1 (tres souvent).
  // Echelle logarithmique : ~0.01 evenement/s au minimum utile jusqu'a
  // ~10 evenements/s au maximum, pour une sensation naturelle sur tout
  // le parcours du bouton plutot qu'une plage trop etroite.
  void SetRandomRate(float rate01)
  {
    rate01 = std::clamp(rate01, 0.f, 1.f);
    mEventsPerSecond = (rate01 <= 0.001f) ? 0.f : std::pow(10.f, -2.f + rate01 * 3.f);
  }

  void SetGlitchMode(int mode) { mGlitchMode = std::clamp(mode, 0, 2); }

  // inL/inR = signal a traiter (stereo), sidechain = signal Aux mono de
  // declenchement (peut etre nullptr si non disponible), outL/outR = sortie.
  void Process(const float* inL, const float* inR, const float* sidechain,
               float* outL, float* outR, int nFrames)
  {
    for (int i = 0; i < nFrames; i++)
    {
      if (!mEnabled)
      {
        outL[i] = inL[i];
        outR[i] = inR[i];
        continue;
      }

      // --- Side-chain (Aux) : vraie enveloppe, ne fait rien sans signal ---
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
        if (mBurstGapRemaining > 0)
        {
          mBurstGapRemaining--;
          if (mBurstGapRemaining <= 0 && mPendingBurstCount > 0)
          {
            mPendingBurstCount--;
            TriggerGlitch(Source::Internal);
            mInternalSamplesRemaining = ComputeInternalDurationSamples();
          }
        }
        else if (mEventsPerSecond > 0.f)
        {
          mSamplesUntilNextTrigger--;
          if (mSamplesUntilNextTrigger <= 0)
          {
            TriggerGlitch(Source::Internal);
            mInternalSamplesRemaining = ComputeInternalDurationSamples();
            MaybeScheduleBurst();
            ScheduleNextInternalTrigger();
          }
        }
      }

      // --- Filet de securite : force un retour a la normale si un glitch
      // (quelle qu'en soit la source) dure anormalement longtemps.
      if (mState != State::Idle)
      {
        mSafetySamplesElapsed++;
        if (mSafetySamplesElapsed > kMaxGlitchSamples)
        {
          mState = State::Idle;
          mSafetySamplesElapsed = 0;
        }
      }
      else
      {
        mSafetySamplesElapsed = 0;
      }

      float sL = inL[i], sR = inR[i];

      switch (mState)
      {
        case State::Idle:
          outL[i] = sL;
          outR[i] = sR;
          break;

        case State::Capturing:
          mFragmentBufL[mCaptureIdx] = sL;
          mFragmentBufR[mCaptureIdx] = sR;
          outL[i] = sL; // passthrough pendant la capture (tres brieve)
          outR[i] = sR;
          mCaptureIdx++;
          if (mCaptureIdx >= mFragmentSamples)
          {
            SmoothLoopSeam(mFragmentBufL);
            SmoothLoopSeam(mFragmentBufR);
            mCaptureIdx = 0;
            mState = State::Looping;
            mLoopReadPos = 0;
          }
          break;

        case State::Looping:
          outL[i] = mFragmentBufL[mLoopReadPos];
          outR[i] = mFragmentBufR[mLoopReadPos];
          mLoopReadPos = (mLoopReadPos + 1) % mFragmentSamples;

          // La sortie de boucle du side-chain est geree plus haut
          // (enveloppe). Ici, seule la duree INTERNE est decomptee.
          if (mTriggerSource == Source::Internal)
          {
            mInternalSamplesRemaining--;
            if (mInternalSamplesRemaining <= 0)
            {
              mState = State::Idle;
              if (mPendingBurstCount > 0)
                mBurstGapRemaining = RandomBurstGapSamples();
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
    mSafetySamplesElapsed = 0;
  }

  // Calcule le delai jusqu'au prochain declenchement interne via une loi
  // exponentielle (methode standard pour simuler un vrai processus de
  // Poisson) - robuste numeriquement.
  void ScheduleNextInternalTrigger()
  {
    if (mEventsPerSecond <= 0.f) { mSamplesUntilNextTrigger = 0x7fffffff; return; }
    float u = std::max(1e-6f, (float)std::rand() / (float)RAND_MAX);
    float intervalSec = -std::log(u) / mEventsPerSecond;
    mSamplesUntilNextTrigger = (int)(intervalSec * mSampleRate);
  }

  // Mode Rafales : chance ALEATOIRE (pas systematique) que ce
  // declenchement devienne une rafale, avec un nombre de coups
  // supplementaires lui aussi aleatoire. L'espacement entre les coups
  // (RandomBurstGapSamples) est egalement variable a chaque fois.
  void MaybeScheduleBurst()
  {
    mPendingBurstCount = 0;
    if (mGlitchMode != (int)Mode::Rafales) return;

    float r = (float)std::rand() / (float)RAND_MAX;
    if (r < 0.35f) // ~35% de chance qu'un declenchement devienne une rafale
      mPendingBurstCount = 1 + (std::rand() % 4); // 1 a 4 coups supplementaires
  }

  int RandomBurstGapSamples() const
  {
    float ms = 10.f + ((float)std::rand() / (float)RAND_MAX) * 180.f; // 10-190ms, variable a chaque coup
    return (int)(ms * 0.001f * mSampleRate);
  }

  int ComputeInternalDurationSamples() const
  {
    float ms = kInternalBaseMs;
    if (mGlitchMode == (int)Mode::DureeVariable)
    {
      // Plage large et deliberement imprevisible : d'un accroc tres bref
      // a un decrochage nettement plus long.
      float factor = 0.1f + ((float)std::rand() / (float)RAND_MAX) * 4.9f; // 0.1x a 5x
      ms = std::clamp(kInternalBaseMs * factor, 15.f, 3000.f);
    }
    return (int)(ms * 0.001f * mSampleRate);
  }

  // Lisse le point de bouclage du fragment capture (evite un clic a
  // chaque repetition).
  void SmoothLoopSeam(std::vector<float>& buf)
  {
    int fadeLen = std::max(2, mFragmentSamples / 10);
    for (int i = 0; i < fadeLen; i++)
    {
      float t = (float)i / (float)fadeLen;
      int idx = mFragmentSamples - fadeLen + i;
      buf[idx] = buf[idx] * (1.f - t) + buf[0] * t;
    }
  }

  State mState = State::Idle;
  Source mTriggerSource = Source::None;

  static constexpr float kFragmentMs = 15.f;      // taille fixe de la "photo"
  static constexpr float kInternalBaseMs = 150.f; // duree de base des glitches internes
  static constexpr float kSidechainThreshold = 0.05f;
  static constexpr int kMaxGlitchSamples = 5 * 48000; // filet de securite absolu

  bool mEnabled = false;

  double mSampleRate = 44100.0;
  int mFragmentSamples = 661;
  std::vector<float> mFragmentBufL, mFragmentBufR;
  int mCaptureIdx = 0;
  int mLoopReadPos = 0;
  int mSafetySamplesElapsed = 0;

  float mFreezeTimeMs = 200.f;
  int mSidechainHangoverSamples = 0;

  float mEventsPerSecond = 0.f;
  int mGlitchMode = 0;
  int mSamplesUntilNextTrigger = 0;
  int mInternalSamplesRemaining = 0;
  int mPendingBurstCount = 0;
  int mBurstGapRemaining = 0;
};
