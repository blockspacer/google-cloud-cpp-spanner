// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/spanner/database_admin_connection.h"
#include "google/cloud/spanner/testing/matchers.h"
#include "google/cloud/spanner/testing/mock_database_admin_stub.h"
#include "google/cloud/testing_util/assert_ok.h"
#include <google/protobuf/text_format.h>
#include <gmock/gmock.h>

namespace google {
namespace cloud {
namespace spanner {
inline namespace SPANNER_CLIENT_NS {
namespace {

using ::google::cloud::spanner_testing::IsProtoEqual;
using ::google::cloud::spanner_testing::MockDatabaseAdminStub;
using ::google::protobuf::TextFormat;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::ElementsAre;
using ::testing::Return;
namespace gcsa = ::google::spanner::admin::database::v1;

std::shared_ptr<DatabaseAdminConnection> CreateTestingConnection(
    std::shared_ptr<internal::DatabaseAdminStub> mock) {
  LimitedErrorCountRetryPolicy retry(/*maximum_failures=*/2);
  ExponentialBackoffPolicy backoff(
      /*initial_delay=*/std::chrono::microseconds(1),
      /*maximum_delay=*/std::chrono::microseconds(1),
      /*scaling=*/2.0);
  GenericPollingPolicy<LimitedErrorCountRetryPolicy> polling(retry, backoff);
  return internal::MakeDatabaseAdminConnection(
      std::move(mock), retry.clone(), backoff.clone(), polling.clone());
}

/// @test Verify that successful case works.
TEST(DatabaseAdminClientTest, CreateDatabaseSuccess) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, CreateDatabase(_, _))
      .WillOnce([](grpc::ClientContext&, gcsa::CreateDatabaseRequest const&) {
        google::longrunning::Operation op;
        op.set_name("test-operation-name");
        op.set_done(false);
        return make_status_or(op);
      });
  EXPECT_CALL(*mock, GetOperation(_, _))
      .WillOnce([](grpc::ClientContext&,
                   google::longrunning::GetOperationRequest const& r) {
        EXPECT_EQ("test-operation-name", r.name());
        google::longrunning::Operation op;
        op.set_name(r.name());
        op.set_done(true);
        gcsa::Database database;
        database.set_name("test-db");
        op.mutable_response()->PackFrom(database);
        return make_status_or(op);
      });

  auto conn = CreateTestingConnection(std::move(mock));
  Database dbase("test-project", "test-instance", "test-db");
  auto fut = conn->CreateDatabase({dbase, {}});
  EXPECT_EQ(std::future_status::ready, fut.wait_for(std::chrono::seconds(10)));
  auto db = fut.get();
  EXPECT_STATUS_OK(db);

  EXPECT_EQ("test-db", db->name());
}

/// @test Verify that a permanent error in CreateDatabase is immediately
/// reported.
TEST(DatabaseAdminClientTest, HandleCreateDatabaseError) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, CreateDatabase(_, _))
      .WillOnce([](grpc::ClientContext&, gcsa::CreateDatabaseRequest const&) {
        return StatusOr<google::longrunning::Operation>(
            Status(StatusCode::kPermissionDenied, "uh-oh"));
      });

  auto conn = CreateTestingConnection(std::move(mock));
  Database dbase("test-project", "test-instance", "test-db");
  auto fut = conn->CreateDatabase({dbase, {}});
  EXPECT_EQ(std::future_status::ready, fut.wait_for(std::chrono::seconds(0)));
  auto db = fut.get();
  EXPECT_EQ(StatusCode::kPermissionDenied, db.status().code());
}

/// @test Verify that the successful case works.
TEST(DatabaseAdminClientTest, GetDatabaseSuccess) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();
  std::string const expected_name =
      "projects/test-project/instances/test-instance/databases/test-database";

  EXPECT_CALL(*mock, GetDatabase(_, _))
      .WillOnce(Return(Status(StatusCode::kUnavailable, "try-again")))
      .WillOnce([&expected_name](grpc::ClientContext&,
                                 gcsa::GetDatabaseRequest const& request) {
        EXPECT_EQ(expected_name, request.name());
        gcsa::Database response;
        response.set_name(request.name());
        response.set_state(gcsa::Database::READY);
        return response;
      });

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->GetDatabase(
      {Database("test-project", "test-instance", "test-database")});
  EXPECT_STATUS_OK(response);
  EXPECT_EQ(gcsa::Database::READY, response->state());
  EXPECT_EQ(expected_name, response->name());
}

/// @test Verify that permanent errors are reported immediately.
TEST(DatabaseAdminClientTest, GetDatabasePermanentError) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, GetDatabase(_, _))
      .WillOnce(Return(Status(StatusCode::kPermissionDenied, "uh-oh")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->GetDatabase(
      {Database("test-project", "test-instance", "test-database")});
  EXPECT_EQ(StatusCode::kPermissionDenied, response.status().code());
}

/// @test Verify that too many transients errors are reported corrrectly.
TEST(DatabaseAdminClientTest, GetDatabaseTooManyTransients) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, GetDatabase(_, _))
      .Times(AtLeast(2))
      .WillRepeatedly(Return(Status(StatusCode::kUnavailable, "try-again")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->GetDatabase(
      {Database("test-project", "test-instance", "test-database")});
  EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
}

/// @test Verify that the successful case works.
TEST(DatabaseAdminClientTest, GetDatabaseDdlSuccess) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();
  std::string const expected_name =
      "projects/test-project/instances/test-instance/databases/test-database";

  EXPECT_CALL(*mock, GetDatabaseDdl(_, _))
      .WillOnce(Return(Status(StatusCode::kUnavailable, "try-again")))
      .WillOnce([&expected_name](grpc::ClientContext&,
                                 gcsa::GetDatabaseDdlRequest const& request) {
        EXPECT_EQ(expected_name, request.database());
        gcsa::GetDatabaseDdlResponse response;
        response.add_statements("CREATE DATABASE test-database");
        return response;
      });

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->GetDatabaseDdl(
      {Database("test-project", "test-instance", "test-database")});
  EXPECT_STATUS_OK(response);
  ASSERT_EQ(1, response->statements_size());
  ASSERT_EQ("CREATE DATABASE test-database", response->statements(0));
}

/// @test Verify that permanent errors are reported immediately.
TEST(DatabaseAdminClientTest, GetDatabaseDdlPermanentError) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, GetDatabaseDdl(_, _))
      .WillOnce(Return(Status(StatusCode::kPermissionDenied, "uh-oh")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->GetDatabaseDdl(
      {Database("test-project", "test-instance", "test-database")});
  EXPECT_EQ(StatusCode::kPermissionDenied, response.status().code());
}

/// @test Verify that too many transients errors are reported corrrectly.
TEST(DatabaseAdminClientTest, GetDatabaseDdlTooManyTransients) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, GetDatabaseDdl(_, _))
      .Times(AtLeast(2))
      .WillRepeatedly(Return(Status(StatusCode::kUnavailable, "try-again")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->GetDatabaseDdl(
      {Database("test-project", "test-instance", "test-database")});
  EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
}

/// @test Verify that successful case works.
TEST(DatabaseAdminClientTest, UpdateDatabaseSuccess) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, UpdateDatabase(_, _))
      .WillOnce(
          [](grpc::ClientContext&, gcsa::UpdateDatabaseDdlRequest const&) {
            gcsa::UpdateDatabaseDdlMetadata metadata;
            metadata.set_database("test-db");
            google::longrunning::Operation op;
            op.set_name("test-operation-name");
            op.set_done(false);
            return make_status_or(op);
          });
  EXPECT_CALL(*mock, GetOperation(_, _))
      .WillOnce([](grpc::ClientContext&,
                   google::longrunning::GetOperationRequest const& r) {
        EXPECT_EQ("test-operation-name", r.name());
        google::longrunning::Operation op;
        op.set_name(r.name());
        op.set_done(true);
        gcsa::UpdateDatabaseDdlMetadata metadata;
        metadata.set_database("test-db");
        op.mutable_metadata()->PackFrom(metadata);
        return make_status_or(op);
      });

  auto conn = CreateTestingConnection(std::move(mock));
  Database dbase("test-project", "test-instance", "test-db");
  auto fut = conn->UpdateDatabase(
      {dbase, {"ALTER TABLE Albums ADD COLUMN MarketingBudget INT64"}});
  EXPECT_EQ(std::future_status::ready, fut.wait_for(std::chrono::seconds(10)));
  auto metadata = fut.get();
  EXPECT_STATUS_OK(metadata);

  EXPECT_EQ("test-db", metadata->database());
}

/// @test Verify that a permanent error in UpdateDatabase is immediately
/// reported.
TEST(DatabaseAdminClientTest, UpdateDatabaseErrorInPoll) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, UpdateDatabase(_, _))
      .WillOnce(
          [](grpc::ClientContext&, gcsa::UpdateDatabaseDdlRequest const&) {
            return StatusOr<google::longrunning::Operation>(
                Status(StatusCode::kPermissionDenied, "uh-oh"));
          });

  auto conn = CreateTestingConnection(std::move(mock));
  Database dbase("test-project", "test-instance", "test-db");
  auto fut = conn->UpdateDatabase(
      {dbase, {"ALTER TABLE Albums ADD COLUMN MarketingBudget INT64"}});
  EXPECT_EQ(std::future_status::ready, fut.wait_for(std::chrono::seconds(0)));
  auto db = fut.get();
  EXPECT_EQ(StatusCode::kPermissionDenied, db.status().code());
}

/// @test Verify that errors in the polling loop are reported.
TEST(DatabaseAdminClientTest, CreateDatabaseErrorInPoll) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, CreateDatabase(_, _))
      .WillOnce([](grpc::ClientContext&, gcsa::CreateDatabaseRequest const&) {
        google::longrunning::Operation op;
        op.set_name("test-operation-name");
        op.set_done(false);
        return make_status_or(std::move(op));
      });
  EXPECT_CALL(*mock, GetOperation(_, _))
      .WillOnce([](grpc::ClientContext&,
                   google::longrunning::GetOperationRequest const& r) {
        EXPECT_EQ("test-operation-name", r.name());
        google::longrunning::Operation op;
        op.set_done(true);
        op.mutable_error()->set_code(
            static_cast<int>(grpc::StatusCode::PERMISSION_DENIED));
        op.mutable_error()->set_message("uh-oh");
        return op;
      });

  auto conn = CreateTestingConnection(std::move(mock));
  Database dbase("test-project", "test-instance", "test-db");
  auto db = conn->CreateDatabase({dbase, {}}).get();
  EXPECT_EQ(StatusCode::kPermissionDenied, db.status().code());
}

/// @test Verify that errors in the polling loop are reported.
TEST(DatabaseAdminClientTest, UpdateDatabaseGetOperationError) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, UpdateDatabase(_, _))
      .WillOnce(
          [](grpc::ClientContext&, gcsa::UpdateDatabaseDdlRequest const&) {
            google::longrunning::Operation op;
            op.set_name("test-operation-name");
            op.set_done(false);
            return make_status_or(std::move(op));
          });
  EXPECT_CALL(*mock, GetOperation(_, _))
      .WillOnce([](grpc::ClientContext&,
                   google::longrunning::GetOperationRequest const& r) {
        EXPECT_EQ("test-operation-name", r.name());
        google::longrunning::Operation op;
        op.set_done(true);
        op.mutable_error()->set_code(
            static_cast<int>(grpc::StatusCode::PERMISSION_DENIED));
        op.mutable_error()->set_message("uh-oh");
        return op;
      });

  auto conn = CreateTestingConnection(std::move(mock));
  Database dbase("test-project", "test-instance", "test-db");
  auto db =
      conn->UpdateDatabase(
              {dbase, {"ALTER TABLE Albums ADD COLUMN MarketingBudget INT64"}})
          .get();
  EXPECT_EQ(StatusCode::kPermissionDenied, db.status().code());
}

/// @test Verify that we can list databases in multiple pages.
TEST(DatabaseAdminClientTest, ListDatabases) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();
  Instance in("test-project", "test-instance");
  std::string const expected_parent = in.FullName();

  EXPECT_CALL(*mock, ListDatabases(_, _))
      .WillOnce([&expected_parent](grpc::ClientContext&,
                                   gcsa::ListDatabasesRequest const& request) {
        EXPECT_EQ(expected_parent, request.parent());
        EXPECT_TRUE(request.page_token().empty());

        gcsa::ListDatabasesResponse page;
        page.set_next_page_token("page-1");
        gcsa::Database database;
        page.add_databases()->set_name("db-1");
        page.add_databases()->set_name("db-2");
        return make_status_or(page);
      })
      .WillOnce([&expected_parent](grpc::ClientContext&,
                                   gcsa::ListDatabasesRequest const& request) {
        EXPECT_EQ(expected_parent, request.parent());
        EXPECT_EQ("page-1", request.page_token());

        gcsa::ListDatabasesResponse page;
        page.set_next_page_token("page-2");
        gcsa::Database database;
        page.add_databases()->set_name("db-3");
        page.add_databases()->set_name("db-4");
        return make_status_or(page);
      })
      .WillOnce([&expected_parent](grpc::ClientContext&,
                                   gcsa::ListDatabasesRequest const& request) {
        EXPECT_EQ(expected_parent, request.parent());
        EXPECT_EQ("page-2", request.page_token());

        gcsa::ListDatabasesResponse page;
        page.clear_next_page_token();
        gcsa::Database database;
        page.add_databases()->set_name("db-5");
        return make_status_or(page);
      });

  auto conn = CreateTestingConnection(std::move(mock));
  std::vector<std::string> actual_names;
  for (auto const& database : conn->ListDatabases({in})) {
    ASSERT_STATUS_OK(database);
    actual_names.push_back(database->name());
  }
  EXPECT_THAT(actual_names,
              ::testing::ElementsAre("db-1", "db-2", "db-3", "db-4", "db-5"));
}

TEST(DatabaseAdminClientTest, ListDatabasesPermanentFailure) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();
  Instance in("test-project", "test-instance");

  EXPECT_CALL(*mock, ListDatabases(_, _))
      .WillOnce(Return(Status(StatusCode::kPermissionDenied, "uh-oh")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto range = conn->ListDatabases({in});
  auto begin = range.begin();
  ASSERT_NE(begin, range.end());
  EXPECT_EQ(StatusCode::kPermissionDenied, begin->status().code());
}

TEST(DatabaseAdminClientTest, ListDatabasesTooManyFailures) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();
  Instance in("test-project", "test-instance");

  EXPECT_CALL(*mock, ListDatabases(_, _))
      .Times(AtLeast(2))
      .WillRepeatedly(Return(Status(StatusCode::kUnavailable, "try-again")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto range = conn->ListDatabases({in});
  auto begin = range.begin();
  ASSERT_NE(begin, range.end());
  EXPECT_EQ(StatusCode::kUnavailable, begin->status().code());
}

/// @test Verify that the successful case works.
TEST(DatabaseAdminClientTest, GetIamPolicySuccess) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();
  std::string const expected_name =
      "projects/test-project/instances/test-instance/databases/test-database";
  std::string const expected_role = "roles/spanner.databaseReader";
  std::string const expected_member = "user:foobar@example.com";

  EXPECT_CALL(*mock, GetIamPolicy(_, _))
      .WillOnce(Return(Status(StatusCode::kUnavailable, "try-again")))
      .WillOnce([&expected_name, &expected_role, &expected_member](
                    grpc::ClientContext&,
                    google::iam::v1::GetIamPolicyRequest const& request) {
        EXPECT_EQ(expected_name, request.resource());
        google::iam::v1::Policy response;
        auto& binding = *response.add_bindings();
        binding.set_role(expected_role);
        *binding.add_members() = expected_member;
        return response;
      });

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->GetIamPolicy(
      {Database("test-project", "test-instance", "test-database")});
  EXPECT_STATUS_OK(response);
  ASSERT_EQ(1, response->bindings().size());
  ASSERT_EQ(expected_role, response->bindings().Get(0).role());
  ASSERT_EQ(1, response->bindings().Get(0).members().size());
  ASSERT_EQ(expected_member, response->bindings().Get(0).members().Get(0));
}

/// @test Verify that permanent errors are reported immediately.
TEST(DatabaseAdminClientTest, GetIamPolicyPermanentError) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, GetIamPolicy(_, _))
      .WillOnce(Return(Status(StatusCode::kPermissionDenied, "uh-oh")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->GetIamPolicy(
      {Database("test-project", "test-instance", "test-database")});
  EXPECT_EQ(StatusCode::kPermissionDenied, response.status().code());
}

/// @test Verify that too many transients errors are reported corrrectly.
TEST(DatabaseAdminClientTest, GetIamPolicyTooManyTransients) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, GetIamPolicy(_, _))
      .Times(AtLeast(2))
      .WillRepeatedly(Return(Status(StatusCode::kUnavailable, "try-again")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->GetIamPolicy(
      {Database("test-project", "test-instance", "test-database")});
  EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
}

/// @test Verify that the successful case works.
TEST(DatabaseAdminClientTest, SetIamPolicySuccess) {
  std::string const expected_name =
      "projects/test-project/instances/test-instance/databases/test-database";
  google::iam::v1::Policy expected_policy;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        etag: "request-etag"
        bindings {
          role: "roles/spanner.databaseReader"
          members: "user:test-user-1@example.com"
          members: "user:test-user-2@example.com"
        }
      )pb",
      &expected_policy));

  auto mock = std::make_shared<MockDatabaseAdminStub>();
  EXPECT_CALL(*mock, SetIamPolicy(_, _))
      .WillOnce([&expected_name](
                    grpc::ClientContext&,
                    google::iam::v1::SetIamPolicyRequest const& request) {
        EXPECT_EQ(expected_name, request.resource());
        return Status(StatusCode::kUnavailable, "try-again");
      })
      .WillOnce([&expected_name, &expected_policy](
                    grpc::ClientContext&,
                    google::iam::v1::SetIamPolicyRequest const& request) {
        EXPECT_EQ(expected_name, request.resource());
        EXPECT_THAT(request.policy(), IsProtoEqual(expected_policy));
        auto response = expected_policy;
        response.set_etag("response-etag");
        return response;
      });

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->SetIamPolicy(
      {Database("test-project", "test-instance", "test-database"),
       expected_policy});
  EXPECT_STATUS_OK(response);
  expected_policy.set_etag("response-etag");
  EXPECT_THAT(*response, IsProtoEqual(expected_policy));
}

/// @test Verify that permanent errors are reported immediately.
TEST(DatabaseAdminClientTest, SetIamPolicyPermanentError) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, SetIamPolicy(_, _))
      .WillOnce(Return(Status(StatusCode::kPermissionDenied, "uh-oh")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->SetIamPolicy(
      {Database("test-project", "test-instance", "test-database"), {}});
  EXPECT_EQ(StatusCode::kPermissionDenied, response.status().code());
}

/// @test Verify that request without the Etag field should fail with the first
/// transient error.
TEST(DatabaseAdminClientTest, SetIamPolicyNonIdempotent) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, SetIamPolicy(_, _))
      .WillOnce(Return(Status(StatusCode::kUnavailable, "try-again")));

  auto conn = CreateTestingConnection(std::move(mock));
  google::iam::v1::Policy policy;
  auto response = conn->SetIamPolicy(
      {Database("test-project", "test-instance", "test-database"), policy});
  EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
}

/// @test Verify that request with the Etag field is retried for transient
/// errors.
TEST(DatabaseAdminClientTest, SetIamPolicyIdempotent) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, SetIamPolicy(_, _))
      .Times(AtLeast(2))
      .WillRepeatedly(Return(Status(StatusCode::kUnavailable, "try-again")));

  auto conn = CreateTestingConnection(std::move(mock));
  google::iam::v1::Policy policy;
  policy.set_etag("test-etag-value");
  auto response = conn->SetIamPolicy(
      {Database("test-project", "test-instance", "test-database"), policy});
  EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
}

/// @test Verify that the successful case works.
TEST(DatabaseAdminClientTest, TestIamPermissionsSuccess) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();
  std::string const expected_name =
      "projects/test-project/instances/test-instance/databases/test-database";
  std::string const expected_permission = "spanner.databases.read";

  EXPECT_CALL(*mock, TestIamPermissions(_, _))
      .WillOnce(Return(Status(StatusCode::kUnavailable, "try-again")))
      .WillOnce([&expected_name, &expected_permission](
                    grpc::ClientContext&,
                    google::iam::v1::TestIamPermissionsRequest const& request) {
        EXPECT_EQ(expected_name, request.resource());
        EXPECT_EQ(1, request.permissions_size());
        EXPECT_EQ(expected_permission, request.permissions(0));
        google::iam::v1::TestIamPermissionsResponse response;
        response.add_permissions(expected_permission);
        return response;
      });

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->TestIamPermissions(
      {Database("test-project", "test-instance", "test-database"),
       {expected_permission}});
  EXPECT_STATUS_OK(response);
  ASSERT_EQ(1, response->permissions_size());
  ASSERT_EQ(expected_permission, response->permissions(0));
}

/// @test Verify that permanent errors are reported immediately.
TEST(DatabaseAdminClientTest, TestIamPermissionsPermanentError) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, TestIamPermissions(_, _))
      .WillOnce(Return(Status(StatusCode::kPermissionDenied, "uh-oh")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->TestIamPermissions(
      {Database("test-project", "test-instance", "test-database"), {}});
  EXPECT_EQ(StatusCode::kPermissionDenied, response.status().code());
}

/// @test Verify that too many transients errors are reported corrrectly.
TEST(DatabaseAdminClientTest, TestIamPermissionsTooManyTransients) {
  auto mock = std::make_shared<MockDatabaseAdminStub>();

  EXPECT_CALL(*mock, TestIamPermissions(_, _))
      .Times(AtLeast(2))
      .WillRepeatedly(Return(Status(StatusCode::kUnavailable, "try-again")));

  auto conn = CreateTestingConnection(std::move(mock));
  auto response = conn->TestIamPermissions(
      {Database("test-project", "test-instance", "test-database"), {}});
  EXPECT_EQ(StatusCode::kUnavailable, response.status().code());
}

}  // namespace
}  // namespace SPANNER_CLIENT_NS
}  // namespace spanner
}  // namespace cloud
}  // namespace google
