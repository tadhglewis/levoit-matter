#include "commissionable_data.h"

#include <sdkconfig.h>

#include <crypto/CHIPCryptoPAL.h>
#include <esp_log.h>
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#include <platform/CommissionableDataProvider.h>

#include <cstring>

static const char *const TAG = "pairing";

namespace {

constexpr uint32_t PASSCODE = CONFIG_LEVOIT_SETUP_PASSCODE;
constexpr uint16_t DISCRIMINATOR = CONFIG_LEVOIT_SETUP_DISCRIMINATOR;
constexpr uint32_t ITERATIONS = chip::Crypto::kSpake2p_Min_PBKDF_Iterations;
constexpr uint8_t SALT[] = {'L', 'e', 'v', 'o', 'i', 't', ' ', 'C',
                            'o', 'r', 'e', ' ', 'S', 'a', 'l', 't'};

constexpr bool passcode_valid(uint32_t passcode) {
  return passcode >= 1 && passcode <= 99999998 && passcode != 11111111 &&
         passcode != 22222222 && passcode != 33333333 &&
         passcode != 44444444 && passcode != 55555555 &&
         passcode != 66666666 && passcode != 77777777 &&
         passcode != 88888888 && passcode != 12345678 && passcode != 87654321;
}

static_assert(passcode_valid(PASSCODE), "LEVOIT_SETUP_PASSCODE is not allowed by Matter");
static_assert(DISCRIMINATOR <= 4095, "LEVOIT_SETUP_DISCRIMINATOR must be 0-4095");

class FixedCommissionableData final
    : public chip::DeviceLayer::CommissionableDataProvider {
public:
  CHIP_ERROR GetSetupDiscriminator(uint16_t &discriminator) override {
    discriminator = DISCRIMINATOR;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR SetSetupDiscriminator(uint16_t) override {
    return CHIP_ERROR_NOT_IMPLEMENTED;
  }
  CHIP_ERROR GetSpake2pIterationCount(uint32_t &count) override {
    count = ITERATIONS;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &salt) override {
    if (salt.size() < sizeof(SALT))
      return CHIP_ERROR_BUFFER_TOO_SMALL;
    memcpy(salt.data(), SALT, sizeof(SALT));
    salt.reduce_size(sizeof(SALT));
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &out,
                                size_t &out_len) override {
    if (this->verifier_len_ == 0) {
      chip::Crypto::Spake2pVerifier verifier;
      ReturnErrorOnFailure(
          verifier.Generate(ITERATIONS, chip::ByteSpan(SALT), PASSCODE));
      chip::MutableByteSpan serialized(this->verifier_);
      ReturnErrorOnFailure(verifier.Serialize(serialized));
      this->verifier_len_ = serialized.size();
    }
    out_len = this->verifier_len_;
    if (out.size() < this->verifier_len_)
      return CHIP_ERROR_BUFFER_TOO_SMALL;
    memcpy(out.data(), this->verifier_, this->verifier_len_);
    out.reduce_size(this->verifier_len_);
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR GetSetupPasscode(uint32_t &passcode) override {
    passcode = PASSCODE;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR SetSetupPasscode(uint32_t) override {
    return CHIP_ERROR_NOT_IMPLEMENTED;
  }

private:
  chip::Crypto::Spake2pVerifierSerialized verifier_ = {};
  size_t verifier_len_ = 0;
};

FixedCommissionableData provider;

} // namespace

void commissionable_data_install() {
  esp_matter::set_custom_commissionable_data_provider(&provider);
  if (PASSCODE == 20202021)
    ESP_LOGW(TAG, "using the public test setup code; set LEVOIT_SETUP_PASSCODE");
}
