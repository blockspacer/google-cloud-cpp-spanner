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

#ifndef GOOGLE_CLOUD_CPP_SPANNER_GOOGLE_CLOUD_SPANNER_CLIENT_H_
#define GOOGLE_CLOUD_CPP_SPANNER_GOOGLE_CLOUD_SPANNER_CLIENT_H_

#include "google/cloud/spanner/client_options.h"
#include "google/cloud/spanner/commit_result.h"
#include "google/cloud/spanner/internal/spanner_stub.h"
#include "google/cloud/spanner/mutations.h"
#include "google/cloud/spanner/result_set.h"
#include "google/cloud/spanner/sql_partition.h"
#include "google/cloud/spanner/sql_statement.h"
#include "google/cloud/spanner/transaction.h"
#include "google/cloud/spanner/value.h"
#include "google/cloud/optional.h"
#include "google/cloud/status.h"
#include "google/cloud/status_or.h"
#include <google/spanner/v1/spanner.pb.h>
#include <memory>
#include <string>
#include <vector>

namespace google {
namespace cloud {
namespace spanner {
inline namespace SPANNER_CLIENT_NS {

/// Options passed to `Read` or `PartitionRead`.
struct ReadOptions {
  /**
   * If non-empty, the name of an index on a database table. This index is used
   * instead of the table primary key when interpreting the `KeySet`and sorting
   * result rows.
   */
  std::string index_name;

  /**
   * Limit on the number of rows to yield, or 0 for no limit.
   * A limit cannot be specified when calling`PartitionRead`.
   */
  std::int64_t limit = 0;
};

/// Options passed to `PartitionRead` or `PartitionQuery`
using PartitionOptions = google::spanner::v1::PartitionOptions;

// TODO(#202) placeholder until KeySet is implemented.
struct KeySet {};

/**
 * Performs database client operations on Spanner.
 *
 * Applications use this class to perform operations on
 * [Spanner Databases][spanner-doc-link].
 *
 * @par Performance
 *
 * Creating a new `Client` is a relatively expensive operation, new objects
 * establish new connections to the service. In contrast, copying or moving
 * an existing `Client` object is a relatively cheap operation. Copied clients
 * share the underlying resources.
 *
 * @par Thread Safety
 *
 * Instances of this class created via copy-construction or copy-assignment
 * share the underlying pool of connections. Access to these copies via multiple
 * threads is guaranteed to work. Two threads operating on the same instance of
 * this class is not guaranteed to work.
 *
 * @par Error Handling
 *
 * This class uses `StatusOr<T>` to report errors. When an operation fails to
 * perform its work the returned `StatusOr<T>` contains the error details. If
 * the `ok()` member function in the `StatusOr<T>` returns `true` then it
 * contains the expected result. Please consult the
 * [`StatusOr<T>` documentation](#google::cloud::v0::StatusOr) for more details.
 *
 * @code
 * namespace cs = ::google::cloud::spanner;
 * using ::google::cloud::StatusOr;
 * auto client = cs::MakeClient();
 * if (!client) {
 *   return client.status();
 * }
 * StatusOr<cs::ResultSet> result = client->Read(...);
 * if (!result) {
 *   return result.status();
 * }
 * for (auto row : result.Rows<std::int64_t, std::string>()) { ... }
 * @endcode
 *
 * [spanner-doc-link]:
 * https://cloud.google.com/spanner/docs/api-libraries-overview
 */
class Client {
 public:
  //@{
  /**
   * Reads rows from the database using key lookups and scans, as a simple
   * key/value style alternative to `ExecuteSql()`.
   *
   * Callers can optionally supply a `Transaction` or
   * `Transaction::SingleUseOptions` used to create a single-use transaction -
   * or neither, in which case a single-use transaction with default options
   * is used.
   *
   * @param table The name of the table in the database to be read.
   * @param keys Identifies the rows to be yielded. If `read_options.index_name`
   *     is set, names keys in that index; otherwise names keys in the primary
   *     index of `table`. It is not an error for `keys` to name rows that do
   *     not exist in the database; `Read` yields nothing for nonexistent rows.
   * @param columns The columns of `table` to be returned for each row matching
   *     this request.
   * @param read_options `ReadOptions` used for this request.
   *
   * - *transaction_options Execute this read in a single-use transaction with
   *     these options.*
   * - *transaction Execute this read as part of an existing transaction.*
   *
   * @return A `StatusOr` containing a `ResultSet` or error status on failure.
   *     No individual row in the `ResultSet` can exceed 100 MiB, and no column
   *     value can exceed 10 MiB.
   */
  StatusOr<ResultSet> Read(std::string const& table, KeySet const& keys,
                           std::vector<std::string> const& columns,
                           ReadOptions const& read_options = {});
  StatusOr<ResultSet> Read(
      Transaction::SingleUseOptions const& transaction_options,
      std::string const& table, KeySet const& keys,
      std::vector<std::string> const& columns,
      ReadOptions const& read_options = {});
  StatusOr<ResultSet> Read(Transaction const& transaction,
                           std::string const& table, KeySet const& keys,
                           std::vector<std::string> const& columns,
                           ReadOptions const& read_options = {});
  //@}

  /**
   * Reads rows from a subset of rows in a database. Requires a prior call
   * to `PartitionRead` to obtain the partition information; see the
   * documentation of that method for full details.
   *
   * @param partition A `SqlPartition`, obtained by calling `PartitionRead`.
   *
   * @return A `StatusOr` containing a `ResultSet` or error status on failure.
   *     No individual row in the `ResultSet` can exceed 100 MiB, and no column
   *     value can exceed 10 MiB.
   */
  StatusOr<ResultSet> Read(SqlPartition const& partition);

  /**
   * Creates a set of partitions that can be used to execute a read
   * operation in parallel.  Each of the returned partitions can be passed
   * to `Read` to specify a subset of the read result to read.
   *
   * There are no ordering guarantees on rows returned among the returned
   * partition, or even within each individual `Read` call issued with a given
   * partition.
   *
   * Partitions become invalid when the session used to create them is deleted,
   * is idle for too long, begins a new transaction, or becomes too old. When
   * any of these happen, it is not possible to resume the read, and the whole
   * operation must be restarted from the beginning.
   *
   * @param transaction The transaction to execute the operation in.
   *     **Must** be a read-only snapshot transaction.
   * @param table The name of the table in the database to be read.
   * @param keys Identifies the rows to be yielded. If `read_options.index_name`
   *     is set, names keys in that index; otherwise names keys in the primary
   *     index of `table`. It is not an error for `keys` to name rows that do
   *     not exist in the database; `Read` yields nothing for nonexistent rows.
   * @param columns The columns of `table` to be returned for each row matching
   *     this request.
   * @param read_options `ReadOptions` used for this request.
   * @param partition_options `PartitionOptions` used for this request.
   *
   * @return A `StatusOr` containing a vector of `SqlPartition` or error
   *     status on failure.
   */
  StatusOr<std::vector<SqlPartition>> PartitionRead(
      Transaction const& transaction, std::string const& table,
      spanner::KeySet const& keys, std::vector<std::string> const& columns,
      ReadOptions const& read_options = {},
      PartitionOptions const& partition_options = {});

  //@{
  /**
   * Executes a SQL query.
   *
   * Operations inside read-write transactions might return `ABORTED`. If this
   * occurs, the application should restart the transaction from the beginning.
   *
   * Callers can optionally supply a `Transaction` or
   * `Transaction::SingleUseOptions` used to create a single-use transaction -
   * or neither, in which case a single-use transaction with default options
   * is used.
   *
   * @param statement The SQL statement to execute.
   *
   * - *transaction_options Execute this query in a single-use transaction with
   *     these options.*
   * - *transaction Execute this query as part of an existing transaction.*
   *
   * @return A `StatusOr` containing a `ResultSet` or error status on failure.
   *     No individual row in the `ResultSet` can exceed 100 MiB, and no column
   *     value can exceed 10 MiB.
   */
  StatusOr<ResultSet> ExecuteSql(SqlStatement const& statement);
  StatusOr<ResultSet> ExecuteSql(
      Transaction::SingleUseOptions const& transaction_options,
      SqlStatement const& statement);
  StatusOr<ResultSet> ExecuteSql(Transaction const& transaction,
                                 SqlStatement const& statement);
  //@}

  /**
   * Executes a SQL query on a subset of rows in a database. Requires a prior
   * call to `PartitionQuery` to obtain the partition information; see the
   * documentation of that method for full details.
   *
   * @param partition A `SqlPartition`, obtained by calling `PartitionRead`.
   *
   * @return A `StatusOr` containing a `ResultSet` or error status on failure.
   *     No individual row in the `ResultSet` can exceed 100 MiB, and no column
   *     value can exceed 10 MiB.
   */
  StatusOr<ResultSet> ExecuteSql(SqlPartition const& partition);

  /**
   * Creates a set of partitions that can be used to execute a query
   * operation in parallel.  Each of the returned partitions can be passed
   * to `ExecuteSql` to specify a subset of the query result to read.
   *
   * Partitions become invalid when the session used to create them is deleted,
   * is idle for too long, begins a new transaction, or becomes too old. When
   * any of these happen, it is not possible to resume the query, and the whole
   * operation must be restarted from the beginning.
   *
   * @param transaction The transaction to execute the operation in.
   *     **Must** be a read-only snapshot transaction.
   * @param statement The SQL statement to execute.
   * @param partition_options `PartitionOptions` used for this request.
   *
   * @return A `StatusOr` containing a vector of `SqlPartition`s or error
   *     status on failure.
   */
  StatusOr<std::vector<SqlPartition>> PartitionQuery(
      Transaction const& transaction, SqlStatement const& statement,
      PartitionOptions const& partition_options = {});

  /**
   * Executes a batch of SQL DML statements. This method allows many statements
   * to be run with lower latency than submitting them sequentially with
   * `ExecuteSql`.
   *
   * @warning This method is not supported in the alpha release and will return
   *     "Unimplemented" status.
   *
   * Statements are executed in order, sequentially. Execution will stop at
   * the first failed statement; the remaining statements will not run.
   *
   * @param transaction The transaction to execute the operation in.
   * @param statements The list of statements to execute in this batch.
   *     Statements are executed serially, such that the effects of statement i
   *     are visible to statement i+1. Each statement must be a DML statement.
   *     Execution will stop at the first failed statement; the remaining
   *     statements will not run. Must not be empty.
   *
   * @return A vector of `StatusOr` corresponding to each statement in
   *     `statements`. Statements that were successfully executed return
   *     statistics. If a statement fails, its error status is returned and
   *     entries for subsequent statements are not present in the returned
   *     vector.
   */
  std::vector<StatusOr<google::spanner::v1::ResultSetStats>> ExecuteBatchDml(
      Transaction const& transaction,
      std::vector<SqlStatement> const& statements);

  /**
   * Executes single Partitioned DML statement. Partitions the key space and
   * runs the DML statement over each partition in parallel using separate,
   * internal transactions that commit independently.
   *
   * @param statement The SQL DML statement to execute.
   *
   * @return A `StatusOr` containing a lower bound on the number of modified
   *     rows or error status on failure.
   */
  StatusOr<std::int64_t> ExecutePartitionedDml(SqlStatement const& statement);

  /**
   * Commits a transaction.
   *
   * The commit might return an `ABORTED` error. This can occur at any time;
   * commonly, the cause is conflicts with concurrent transactions. However, it
   * can also happen for a variety of other reasons. If `Commit` returns
   * `ABORTED`, the caller should re-attempt the transaction from the beginning,
   * re-using the same session.
   *
   * @param transaction The transaction to commit.
   * @param mutations The mutations to be executed when this transaction
   *     commits. All mutations are applied atomically, in the order they appear
   *     in this list.
   *
   * @return A `StatusOr` containing the result of the commit or error status
   *     on failure.
   */
  StatusOr<CommitResult> Commit(Transaction const& transaction,
                                std::vector<Mutation> const& mutations);
  /**
   * Rolls back a transaction, releasing any locks it holds. It is a good idea
   * to call this for any transaction that includes one or more `Read` or
   * `ExecuteSql` requests and ultimately decides not to commit.
   *
   * @param transaction The transaction to roll back.
   *
   * @return The error status of the rollback.
   */
  Status Rollback(Transaction const& transaction);

  /**
   * Create a new client with the given stub.
   * @warning Do not call this directly (it's exposed for testing only); call
   *     `MakeClient()` instead.
   */
  explicit Client(std::shared_ptr<internal::SpannerStub> s)
      : stub_(std::move(s)) {}

 private:
  std::shared_ptr<internal::SpannerStub> stub_;
};

/// Factory method to create a `Client` with the given `client_options`.
StatusOr<Client> MakeClient(ClientOptions const& client_options = {});

}  // namespace SPANNER_CLIENT_NS
}  // namespace spanner
}  // namespace cloud
}  // namespace google

#endif  // GOOGLE_CLOUD_CPP_SPANNER_GOOGLE_CLOUD_SPANNER_CLIENT_H_