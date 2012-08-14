/***************************************************************************************************
 *  Copyright 2012 maidsafe.net limited                                                            *
 *                                                                                                 *
 *  The following source code is property of MaidSafe.net limited and is not meant for external    *
 *  use. The use of this code is governed by the licence file licence.txt found in the root of     *
 *  this directory and also on www.maidsafe.net.                                                   *
 *                                                                                                 *
 *  You are not free to copy, amend or otherwise use this source code without the explicit written *
 *  permission of the board of directors of MaidSafe.net.                                          *
 **************************************************************************************************/

#ifndef MAIDSAFE_PRIVATE_RETURN_CODES_H_
#define MAIDSAFE_PRIVATE_RETURN_CODES_H_

namespace maidsafe {

namespace priv {

enum ReturnCode {
  kSuccess = 0,
  kGeneralError = -100001,
  kUnknownFailure = -150002,
  kNullParameter = -150003,
  kKeyNotUnique = -150004,
  kKeyUnique = -150005,
  kParseFailure = -150006,
  kPreOperationCheckFailure = -150007,
  kDuplicateNameFailure = -150008,
  kVerifyDataFailure = -150009,
  kGetFailure = -150010,
  kStoreFailure = -150011,
  kDeleteFailure = -150012,
  kModifyFailure = -150013,
  kInvalidSignedData = -150014,
  kInvalidModify = -150015,
  kSignatureVerificationFailure = -150016,
  kNotHashable = -150017,
  kNotOwner = -150018,
  kInvalidChunkType = -150019,
  kFailedToFindChunk = -150020,
  kInvalidPublicKey = -150021,
  kAppendDisallowed = -150022,
  kHashFailure = -150023,
  kDifferentVersion = -150024,
  kChunkNotModified = -150025,
  kDataNotPublicKey = -150026,

  // Transport
  kAlreadyStarted = -350001,
  kInvalidPort = -350002,
  kInvalidAddress = -350003,
  kSetOptionFailure = -350004,
  kBindError = -350005,
  kListenError = -350006,
  kMessageSizeTooLarge = -350007,
  kReceiveFailure = -350008,
  kReceiveTimeout = -350009,
  kSendFailure = -350010,
  kSendTimeout = -350011
};

}  // namespace priv

}  // namespace maidsafe

#endif  // MAIDSAFE_PRIVATE_RETURN_CODES_H_
