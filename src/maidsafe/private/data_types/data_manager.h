/*
* ============================================================================
*
* Copyright [2011] maidsafe.net limited
*
* The following source code is property of maidsafe.net limited and is not
* meant for external use.  The use of this code is governed by the license
* file licence.txt found in the root of this directory and also on
* www.maidsafe.net.
*
* You are not free to copy, amend or otherwise use this source code without
* the explicit written permission of the board of directors of maidsafe.net.
*
* ============================================================================
*/

#ifndef MAIDSAFE_PRIVATE_DATA_MANAGER_H_
#define MAIDSAFE_PRIVATE_DATA_MANAGER_H_

#include "maidsafe/common/types.h"
#include "maidsafe/private/utils/fob.h"
#include "maidsafe/private/data_types/store_policies.h"
#include "maidsafe/private/data_types/get_policies.h"
#include "maidsafe/private/data_types/delete_policies.h"
#include "maidsafe/private/data_types/edit_policies.h"
#include "maidsafe/private/data_types/amend_policies.h"
// #include "maidsafe/private/data_types/store_policies.h"

// Host class

template <typename StoragePolicy, // network or local // simply send to net & get result or store locally
          typename StorePolicy,
          typename GetPolicy,
          typename DeletePolicy,
          typename EditPolicy,
          typename AppendPolicy>
class DataHandler : private StoragePolicy,
                    private StorePolicy,
                    private GetPolicy,
                    private DeletePolicy,
                    private EditPolicy,
                    private AppendPolicy {
 public:
  DataHandler(Routing routing) :
  //             chunk_store_dir_(chunk_store),
               routing_(routing),
  //             message_handler_() {}
  // Default, hash of content == name
  // DataHandler(const ChunkId name, const NonEmptyString content);
  // // Signature, (hash of content + sig == name) !! (content == 0 and signed)
  // DataHandler(const ChunkId name,
  //             const asymm::PublicKey content,
  //             const Signature signature,
  //             const asymm::PublicKey public_key);
  // // Edit by owner
  // DataHandler(const ChunkId name,
  //             const NonEmptyString content,
  //             const Signature signature,
  //             const asymm::PublicKey public_key);
  // // Appendable by all
  // DataHandler(const ChunkId name,
  //             const NonEmptyString content,
  //             const std::vector<asymm::PublicKey> allowed,
  //             const Signature signature,
  //             const asymm::PublicKey public_key);

  static bool Store(NonEmptyString key, NonEmptyString value, Signature Signature, Identity id) {
    return StoragePolicy::Process(StorePolicy::Store(NonEmptyString key, NonEmptyString value));
  }
  static GetPolicy::value Get(NonEmptyString key) {
    return StoragePolicy::Process(GetPolicy::Get(key));
  }
  static bool Delete(NonEmptyString key, Signature Signature, Identity id) {
    return DeletePolicy::Delete(key);
  }
  static bool Edit(NonEmptyString& key,
                   NonEmptyString& version,  // or old_content ??
                   NonEmptyString& new_content) {
    return EditPolicy::Edit(key, version, value);
  }
 private:
  Routing routing_;
  MessageHandler message_handler_;
};

// Data types

typedef DataHandler<Network, StoreAndPay, GetDataElementOnly, DeleteByRemovingReference,
        NoEdit,
        NoAppend> ImmutableData;
typedef DataHandler<Network, StoreAll, GetAllElements, DeleteIfOwner, EditIfOwner, NoAppend>
                                                                            MutableData;

typedef DataHandler<StoreAll, GetAllElements, DeleteIfOwner, NoEdit, AppendIfAllowed>
                                                                            AppendableData;

typedef DataHandler<StoreAll, GetAllElements, ZeroOutIfOwner, NoEdit, NoAppend>
                                                                            SignatureData;
//########################################################################################
typedef DatatHandler <> ClientDataHandler;
typedef DatatHandler <> VaultDataHandler;
typedef DatatHandler <> CIHDataHandler;

template <typename T>
std::string SerialiseDataType(T t) {

}

template <typename T>
T ParseDataType(std::string serialised_data) {

}

// this can all go in a cc file
// Default, hash of content == name
ImmutableData CreateDataType(const ChunkId name,const NonEmptyString content) {
  // .... do stuff to check
  // this probably a try catch block !!
  return ImmutableData(const ChunkId name,const NonEmptyString content);
}

// Signature, (hash of content + sig == name) !! (content == 0 and signed)
SignatureData CreateDataType(const ChunkId name,
                             const asymm::PublicKey content,
                             const Signature signature,
                             const asymm::PublicKey public_key);
// Edit by owner
MutableData signatureData CreateDataType(const ChunkId name,
                                         const NonEmptyString content,
                                         const Signature signature,
                                         const asymm::PublicKey public_key);
// Appendable by all
AppendableData CreateDataType(const ChunkId name,
                              const NonEmptyString content,
                              const std::vector<asymm::PublicKey> allowed,
                              const Signature signature,
                              const asymm::PublicKey public_key);


#endif  // MAIDSAFE_PRIVATE_DATA_MANAGER_H_

