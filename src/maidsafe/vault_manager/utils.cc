/*  Copyright 2014 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#include "maidsafe/vault_manager/utils.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <mutex>

#include "boost/filesystem/operations.hpp"

#include "maidsafe/common/error.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/make_unique.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/passport/passport.h"

#include "maidsafe/vault_manager/interprocess_messages.pb.h"
#include "maidsafe/vault_manager/vault_info.pb.h"
#include "maidsafe/vault_manager/vault_info.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace vault_manager {

namespace {

#ifdef TESTING
std::once_flag test_env_flag;
Port g_test_vault_manager_port(0);
fs::path g_test_env_root_dir, g_path_to_vault;
bool g_using_default_environment(true);
routing::BootstrapContacts g_bootstrap_contacts;
int g_identity_index(0);
#endif

}  // unnamed namespace


namespace detail {

template <>
routing::BootstrapContacts Parse<routing::BootstrapContacts>(const std::string& message) {
  return routing::ParseBootstrapContacts(message);
}

template <>
std::unique_ptr<VaultConfig> Parse<std::unique_ptr<VaultConfig>>(const std::string& message) {
  protobuf::VaultStartedResponse
      vault_started_response{ ParseProto<protobuf::VaultStartedResponse>(message) };
  passport::Pmid pmid = { passport::DecryptPmid(
      crypto::CipherText{ NonEmptyString{ vault_started_response.encrypted_pmid() } },
      crypto::AES256Key{ vault_started_response.aes256key() },
      crypto::AES256InitialisationVector{ vault_started_response.aes256iv() }) };
  boost::filesystem::path vault_dir(vault_started_response.vault_dir());
  DiskUsage max_disk_usage(vault_started_response.max_disk_usage());
  routing::BootstrapContacts bootstrap_contacts(
          routing::ParseBootstrapContacts(vault_started_response.serialised_bootstrap_contacts()));
  auto vault_config = maidsafe::make_unique<VaultConfig>(pmid, vault_dir, max_disk_usage,
                                                         bootstrap_contacts);
#ifdef TESTING
  // TODO(Prakash) parse if has_serialised_public_pmids()
  vault_config->test_config.public_pmid_list = std::vector<passport::PublicPmid>();
#endif
  return vault_config;
}


template <>
std::unique_ptr<asymm::PlainText> Parse<std::unique_ptr<asymm::PlainText>>(
    const std::string& message) {
  return maidsafe::make_unique<asymm::PlainText>(message);
}

template <>
std::unique_ptr<passport::PmidAndSigner> Parse<std::unique_ptr<passport::PmidAndSigner>>(
    const std::string& /*message*/) {
// FIXME need to set exception in case of error. this requires access to promise to set exception
  return std::unique_ptr<passport::PmidAndSigner>{};
}

}  // namspace detail


void ToProtobuf(crypto::AES256Key symm_key, crypto::AES256InitialisationVector symm_iv,
                const VaultInfo& vault_info, protobuf::VaultInfo* protobuf_vault_info) {
  protobuf_vault_info->set_pmid(
      passport::EncryptPmid(vault_info.pmid_and_signer->first, symm_key, symm_iv)->string());
  protobuf_vault_info->set_anpmid(
      passport::EncryptAnpmid(vault_info.pmid_and_signer->second, symm_key, symm_iv)->string());
  protobuf_vault_info->set_vault_dir(vault_info.vault_dir.string());
  protobuf_vault_info->set_label(vault_info.label.string());
  if (vault_info.max_disk_usage != 0U)
    protobuf_vault_info->set_max_disk_usage(vault_info.max_disk_usage.data);
  if (vault_info.owner_name->IsInitialised())
    protobuf_vault_info->set_owner_name(vault_info.owner_name->string());
}

void FromProtobuf(crypto::AES256Key symm_key, crypto::AES256InitialisationVector symm_iv,
                  const protobuf::VaultInfo& protobuf_vault_info, VaultInfo& vault_info) {
  vault_info.pmid_and_signer = std::make_shared<passport::PmidAndSigner>(std::make_pair(
    passport::DecryptPmid(
        crypto::CipherText{ NonEmptyString{ protobuf_vault_info.pmid() } }, symm_key, symm_iv),
    passport::DecryptAnpmid(
        crypto::CipherText{ NonEmptyString{ protobuf_vault_info.anpmid() } }, symm_key, symm_iv)));
  vault_info.vault_dir = protobuf_vault_info.vault_dir();
  vault_info.label = NonEmptyString{ protobuf_vault_info.label() };
  if (protobuf_vault_info.has_max_disk_usage())
    vault_info.max_disk_usage = DiskUsage{ protobuf_vault_info.max_disk_usage() };
  if (protobuf_vault_info.has_owner_name()) {
    vault_info.owner_name =
        passport::PublicMaid::Name{ Identity{ protobuf_vault_info.owner_name() } };
  }
}

std::string WrapMessage(MessageAndType message_and_type) {
  protobuf::WrapperMessage wrapper_message;
  wrapper_message.set_payload(message_and_type.first);
  wrapper_message.set_type(static_cast<int32_t>(message_and_type.second));
  return wrapper_message.SerializeAsString();
}

MessageAndType UnwrapMessage(std::string wrapped_message) {
  protobuf::WrapperMessage wrapper;
  if (!wrapper.ParseFromString(wrapped_message)) {
    LOG(kError) << "Failed to unwrap message";
    BOOST_THROW_EXCEPTION(MakeError(CommonErrors::parsing_error));
  }

  return std::make_pair(wrapper.payload(), static_cast<MessageType>(wrapper.type()));
}

NonEmptyString GenerateLabel() {
  std::string label{ RandomAlphaNumericString(4) };
  for (int i(0); i < 4; ++i)
    label += ("-" + RandomAlphaNumericString(4));
  std::transform(std::begin(label), std::end(label), std::begin(label),
                 std::ptr_fun<int, int>(std::toupper));
  return NonEmptyString{ label };
}

#ifdef TESTING
void SetTestEnvironmentVariables(Port test_vault_manager_port, const fs::path& test_env_root_dir,
                                 const fs::path& path_to_vault,
                                 routing::BootstrapContacts bootstrap_contacts) {
  std::call_once(test_env_flag, [=] {
    g_test_vault_manager_port = test_vault_manager_port;
    g_test_env_root_dir = test_env_root_dir;
    g_path_to_vault = path_to_vault;
    g_bootstrap_contacts = bootstrap_contacts;
    g_using_default_environment = false;
  });
}

Port GetTestVaultManagerPort() { return g_test_vault_manager_port; }
fs::path GetTestEnvironmentRootDir() { return g_test_env_root_dir; }
fs::path GetPathToVault() { return g_path_to_vault; }
routing::BootstrapContacts GetBootstrapContacts() { return g_bootstrap_contacts; }
void SetIdentityIndex(int identity_index) { g_identity_index = identity_index; }
int IdentityIndex() { return g_identity_index; }
bool UsingDefaultEnvironment() { return g_using_default_environment; }
#endif  // TESTING

}  //  namespace vault_manager

}  //  namespace maidsafe
