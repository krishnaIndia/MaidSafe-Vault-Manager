/*  Copyright 2013 MaidSafe.net limited

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

#include "maidsafe/data_store/sure_file_store.h"

#include "maidsafe/common/test.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/data_types/data_type_values.h"
#include "maidsafe/data_types/immutable_data.h"
#include "maidsafe/data_types/owner_directory.h"

namespace maidsafe {
namespace data_store {

namespace test {

const DiskUsage kDefaultMaxDiskUsage(2000);

class SureFileStoreTest {
 protected:
  SureFileStoreTest()
      : sure_file_store_path_(maidsafe::test::CreateTestPath("MaidSafe_Test_SureFileStore")),
        sure_file_store_(*sure_file_store_path_, kDefaultMaxDiskUsage) {}

  maidsafe::test::TestPath sure_file_store_path_;
  SureFileStore sure_file_store_;
};

TEST_CASE_METHOD(SureFileStoreTest, "SureFileStoreSuccessfulStore", "[Private][Behavioural]") {
  const size_t kDataSize(100);
  ImmutableData data(NonEmptyString(RandomString(kDataSize)));
  sure_file_store_.Put(data);
  auto put_timeout(std::chrono::system_clock::now() + std::chrono::milliseconds(100));
  while (std::chrono::system_clock::now() < put_timeout) {
    if (sure_file_store_.GetCurrentDiskUsage() != DiskUsage(kDataSize))
      Sleep(std::chrono::milliseconds(1));
    else
      break;
  }
  REQUIRE(DiskUsage(kDataSize) == sure_file_store_.GetCurrentDiskUsage());

  auto retrieved_data(sure_file_store_.Get<ImmutableData>(data.name()).get());
  REQUIRE(data.name() == retrieved_data.name());
  REQUIRE(data.data() == retrieved_data.data());
  REQUIRE(DiskUsage(kDataSize) == sure_file_store_.GetCurrentDiskUsage());

  sure_file_store_.Delete<ImmutableData>(data.name());
  auto delete_timeout(std::chrono::system_clock::now() + std::chrono::milliseconds(100));
  while (std::chrono::system_clock::now() < delete_timeout) {
    if (sure_file_store_.GetCurrentDiskUsage() != DiskUsage(0))
      Sleep(std::chrono::milliseconds(1));
    else
      break;
  }
  REQUIRE(DiskUsage(0) == sure_file_store_.GetCurrentDiskUsage());

  StructuredDataVersions::VersionName default_version;
  StructuredDataVersions::VersionName version0(0, ImmutableData::Name(Identity(RandomString(64))));
  StructuredDataVersions::VersionName version1(1, ImmutableData::Name(Identity(RandomString(64))));
  StructuredDataVersions::VersionName version2(2, ImmutableData::Name(Identity(RandomString(64))));
  OwnerDirectory::Name dir_name(Identity(RandomString(64)));

  sure_file_store_.PutVersion<OwnerDirectory>(dir_name, default_version, version0);
  sure_file_store_.PutVersion<OwnerDirectory>(dir_name, version0, version1);
  sure_file_store_.PutVersion<OwnerDirectory>(dir_name, version1, version2);

  auto retrieved_versions(sure_file_store_.GetVersions<OwnerDirectory>(dir_name).get());
  REQUIRE(1U == retrieved_versions.size());
  REQUIRE(version2 == retrieved_versions.front());

  retrieved_versions = sure_file_store_.GetBranch<OwnerDirectory>(dir_name, version2).get();
  REQUIRE(3U == retrieved_versions.size());
  auto itr(std::begin(retrieved_versions));
  REQUIRE(version2 == *itr++);
  REQUIRE(version1 == *itr++);
  REQUIRE(version0 == *itr);

  sure_file_store_.DeleteBranchUntilFork<OwnerDirectory>(dir_name, version2);
  retrieved_versions = sure_file_store_.GetVersions<OwnerDirectory>(dir_name).get();
  REQUIRE(retrieved_versions.empty());
}

}  // namespace test

}  // namespace data_store
}  // namespace maidsafe
