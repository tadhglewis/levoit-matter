#include "commissionable_data.h"

#include <sdkconfig.h>

#include <crypto/CHIPCryptoPAL.h>
#include <esp_log.h>
#include <esp_matter_providers.h>
#include <lib/support/Base64.h>
#include <lib/support/Span.h>
#include <nvs_flash.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/ESP32/ESP32Config.h>

#include <cstring>

static const char *const TAG = "pairing";

using chip::DeviceLayer::Internal::ESP32Config;

namespace {

constexpr uint32_t TEST_PASSCODE = 20202021;
constexpr uint16_t TEST_DISCRIMINATOR = 3840;
constexpr uint32_t TEST_ITERATIONS = chip::Crypto::kSpake2p_Min_PBKDF_Iterations;
constexpr uint8_t TEST_SALT[] = {'L', 'e', 'v', 'o', 'i', 't', ' ', 'C',
                                 'o', 'r', 'e', ' ', 'S', 'a', 'l', 't'};

class CommissionableData final
    : public chip::DeviceLayer::CommissionableDataProvider {
public:
  bool load_factory() {
    uint32_t discriminator = 0;
    uint32_t iterations = 0;
    char salt[BASE64_ENCODED_LEN(chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length) + 1] = {};
    char verifier[BASE64_ENCODED_LEN(chip::Crypto::kSpake2p_VerifierSerialized_Length) + 1] = {};
    size_t salt_chars = 0;
    size_t verifier_chars = 0;

    if (ESP32Config::ReadConfigValue(ESP32Config::kConfigKey_SetupDiscriminator,
                                     discriminator) != CHIP_NO_ERROR ||
        ESP32Config::ReadConfigValue(ESP32Config::kConfigKey_Spake2pIterationCount,
                                     iterations) != CHIP_NO_ERROR ||
        ESP32Config::ReadConfigValueStr(ESP32Config::kConfigKey_Spake2pSalt, salt,
                                        sizeof(salt), salt_chars) != CHIP_NO_ERROR ||
        ESP32Config::ReadConfigValueStr(ESP32Config::kConfigKey_Spake2pVerifier,
                                        verifier, sizeof(verifier),
                                        verifier_chars) != CHIP_NO_ERROR)
      return false;

    size_t salt_len = chip::Base64Decode32(
        salt, static_cast<uint32_t>(strnlen(salt, sizeof(salt))), this->salt_);
    size_t verifier_len = chip::Base64Decode32(
        verifier, static_cast<uint32_t>(strnlen(verifier, sizeof(verifier))),
        this->verifier_);
    if (discriminator > 4095 ||
        iterations < chip::Crypto::kSpake2p_Min_PBKDF_Iterations ||
        iterations > chip::Crypto::kSpake2p_Max_PBKDF_Iterations ||
        salt_len < chip::Crypto::kSpake2p_Min_PBKDF_Salt_Length ||
        salt_len > chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length ||
        verifier_len != chip::Crypto::kSpake2p_VerifierSerialized_Length)
      return false;

    this->discriminator_ = static_cast<uint16_t>(discriminator);
    this->iterations_ = iterations;
    this->salt_len_ = salt_len;
    this->verifier_len_ = verifier_len;
    this->factory_ = true;
    return true;
  }

  void use_test_code() {
    this->discriminator_ = TEST_DISCRIMINATOR;
    this->iterations_ = TEST_ITERATIONS;
    memcpy(this->salt_, TEST_SALT, sizeof(TEST_SALT));
    this->salt_len_ = sizeof(TEST_SALT);
    this->verifier_len_ = 0;
    this->factory_ = false;
  }

  uint16_t discriminator() const { return this->discriminator_; }

  CHIP_ERROR GetSetupDiscriminator(uint16_t &discriminator) override {
    discriminator = this->discriminator_;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR SetSetupDiscriminator(uint16_t) override {
    return CHIP_ERROR_NOT_IMPLEMENTED;
  }
  CHIP_ERROR GetSpake2pIterationCount(uint32_t &count) override {
    count = this->iterations_;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &salt) override {
    if (salt.size() < this->salt_len_)
      return CHIP_ERROR_BUFFER_TOO_SMALL;
    memcpy(salt.data(), this->salt_, this->salt_len_);
    salt.reduce_size(this->salt_len_);
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &out,
                                size_t &out_len) override {
    if (this->verifier_len_ == 0) {
      chip::Crypto::Spake2pVerifier verifier;
      ReturnErrorOnFailure(verifier.Generate(
          this->iterations_, chip::ByteSpan(this->salt_, this->salt_len_),
          TEST_PASSCODE));
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
    if (this->factory_)
      return CHIP_ERROR_NOT_IMPLEMENTED;
    passcode = TEST_PASSCODE;
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR SetSetupPasscode(uint32_t) override {
    return CHIP_ERROR_NOT_IMPLEMENTED;
  }

private:
  bool factory_ = false;
  uint16_t discriminator_ = TEST_DISCRIMINATOR;
  uint32_t iterations_ = TEST_ITERATIONS;
  uint8_t salt_[chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length + 4] = {};
  size_t salt_len_ = 0;
  uint8_t verifier_[chip::Crypto::kSpake2p_VerifierSerialized_Length + 4] = {};
  size_t verifier_len_ = 0;
};

CommissionableData provider;

} // namespace

bool commissionable_data_install() {
  esp_err_t err = nvs_flash_init_partition(CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL);
  bool factory = err == ESP_OK && provider.load_factory();
  if (factory) {
    ESP_LOGI(TAG, "setup code from the factory partition, discriminator %u",
             provider.discriminator());
  } else {
    provider.use_test_code();
    ESP_LOGW(TAG, "no factory data; using the public test setup code");
  }
  esp_matter::set_custom_commissionable_data_provider(&provider);
  return factory;
}
