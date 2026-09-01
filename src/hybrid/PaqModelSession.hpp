#pragma once

#include "../Encoder.hpp"
#include "../Predictor.hpp"
#include "../PredictorBlock.hpp"
#include "../PredictorMain.hpp"
#include "../PredictorMainLstmOnly.hpp"
#include "../Shared.hpp"
#include "../Utils.hpp"
#include "PaqConfig.hpp"

#include <cstring>
#include <memory>
#include <utility>

namespace routed {

// Owns the PAQ predictors whose adaptive state spans every PAQ fragment in one
// routed archive.  Arithmetic coders are deliberately not members: each
// fragment creates a fresh Encoder and therefore has an independently bounded
// payload while both predictor families continue their PAQ-subsequence state.
class PaqModelSession {
public:
  class Fragment {
  public:
    Fragment(Fragment&& other) noexcept
        : owner_(other.owner_), mode_(other.mode_),
          encoder_(std::move(other.encoder_)), finished_(other.finished_) {
      other.owner_ = nullptr;
      other.finished_ = true;
    }

    Fragment(const Fragment&) = delete;
    Fragment& operator=(const Fragment&) = delete;
    Fragment& operator=(Fragment&&) = delete;

    ~Fragment() noexcept {
      // Never flush from a destructor: quit() is exception based, so a second
      // failure while unwinding would terminate the process. All production
      // call sites explicitly finish. On an exceptional exit, only release
      // the session's active-fragment guard; the incomplete payload is never
      // committed by the archive writer.
      if (owner_ != nullptr && !finished_)
        owner_->fragmentActive_ = false;
    }

    Encoder& coder() {
      if (finished_ || encoder_ == nullptr)
        quit("PAQ fragment coder is no longer active.");
      return *encoder_;
    }

    void finish() {
      if (owner_ == nullptr || finished_ || encoder_ == nullptr)
        quit("PAQ fragment coder was finished more than once.");
      PaqModelSession* owner = owner_;
      finished_ = true;
      owner_->fragmentActive_ = false;
      if (mode_ == COMPRESS)
        encoder_->flush();
    }

    bool finished() const { return finished_; }

  private:
    friend class PaqModelSession;

    Fragment(PaqModelSession* owner, Mode mode, File* payload)
        : owner_(owner), mode_(mode), encoder_(new Encoder(
            owner->blockPredictor_.get(), owner->mainPredictor_.get(), true,
            mode, payload, owner->mainShared_)) {}

    PaqModelSession* owner_ = nullptr;
    Mode mode_ = COMPRESS;
    std::unique_ptr<Encoder> encoder_;
    bool finished_ = false;
  };

  PaqModelSession(Shared* mainShared, const PaqConfigV1& config)
      : mainShared_(mainShared), config_(config),
        canonicalConfig_(encodePaqConfigCanonical(config)) {
    if (mainShared_ == nullptr ||
        (mainShared_->level == 0 && !mainShared_->GetOptionUseLSTM()))
      quit("Native routed PAQ requires an initialized coding model.");
    if (!matchesShared(config_, *mainShared_))
      quit("Native routed PAQ configuration disagrees with Shared state.");
    if (sessionWasConstructed())
      quit("Only one PAQ model session may exist in a process.");

    blockShared_.init(config_.compressionLevel, 16);
    blockShared_.chosenSimd = mainShared_->chosenSimd;
    blockPredictor_.reset(new PredictorBlock(&blockShared_));
    if (config_.predictorMode == PaqPredictorMode::LSTM_ONLY)
      mainPredictor_.reset(new PredictorMainLstmOnly(mainShared_));
    else
      mainPredictor_.reset(new PredictorMain(mainShared_));
    sessionWasConstructed() = true;
  }

  PaqModelSession(const PaqModelSession&) = delete;
  PaqModelSession& operator=(const PaqModelSession&) = delete;

  // Deliberately do not clear sessionWasConstructed() on destruction. Legacy
  // ContextModel factories contain process-lifetime function-static objects
  // bound to the first Shared/Models graph. Reconstructing a second session
  // in the same process would reuse dangling or stale model bindings. The CLI
  // performs one encode/decode operation per process; a future reusable API
  // must first remove those legacy statics instead of weakening this guard.

  Shared* shared() const { return mainShared_; }
  Shared* blockShared() { return &blockShared_; }
  Predictor* mainPredictor() const { return mainPredictor_.get(); }
  Predictor* blockPredictor() const { return blockPredictor_.get(); }
  const PaqConfigV1& config() const { return config_; }
  const std::vector<uint8_t>& canonicalConfig() const {
    return canonicalConfig_;
  }

  Fragment beginFragment(Mode mode, File* payload) {
    if (payload == nullptr)
      quit("PAQ fragment payload is unavailable.");
    if (fragmentActive_)
      quit("PAQ model session already has an active arithmetic fragment.");
    fragmentActive_ = true;
    return Fragment(this, mode, payload);
  }

private:
  static bool matchesShared(const PaqConfigV1& config,
                            const Shared& shared) {
    uint16_t optionFlags = 0;
    if (shared.GetOptionTrainExe())
      optionFlags |= paqOptionFlag(PaqOptionFlag::TRAIN_EXE);
    if (shared.GetOptionTrainTxt())
      optionFlags |= paqOptionFlag(PaqOptionFlag::TRAIN_TEXT);
    if (shared.GetOptionAdaptiveLearningRate())
      optionFlags |= paqOptionFlag(PaqOptionFlag::ADAPTIVE_LEARNING_RATE);
    if (shared.GetOptionSkipRGB())
      optionFlags |= paqOptionFlag(PaqOptionFlag::SKIP_RGB_TRANSFORM);
    if (shared.GetOptionUseLSTM())
      optionFlags |= paqOptionFlag(PaqOptionFlag::USE_LSTM);
    uint32_t tuningBits = 0;
    static_assert(sizeof(tuningBits) == sizeof(shared.tuning_param),
                  "PAQ tuning parameter must remain binary32.");
    std::memcpy(&tuningBits, &shared.tuning_param, sizeof(tuningBits));
    const bool lstmShapeMatches = !shared.GetOptionUseLSTM() ||
      (config.lstmLayers == shared.LstmSettings.num_layers &&
       config.lstmHiddenSize == shared.LstmSettings.hidden_size &&
       config.lstmHorizon == shared.LstmSettings.horizon);
    return validPaqConfigV1(config) &&
           config.compressionLevel == shared.level &&
           config.optionFlags == optionFlags &&
           lstmShapeMatches &&
           config.tuningParameterBits == tuningBits;
  }

  static bool& sessionWasConstructed() {
    static bool value = false;
    return value;
  }

  Shared* mainShared_ = nullptr;
  PaqConfigV1 config_;
  std::vector<uint8_t> canonicalConfig_;
  Shared blockShared_;
  std::unique_ptr<Predictor> blockPredictor_;
  std::unique_ptr<Predictor> mainPredictor_;
  bool fragmentActive_ = false;
};

} // namespace routed
