#pragma once

#include "mongo/base/status_with.h"
#include "mongo/bson/timestamp.h"
#include "mongo/client/connection_pool.h"
#include "mongo/client/dbclient_connection.h"
#include "mongo/client/dbclient_cursor.h"
#include "mongo/db/namespace_string.h"

namespace mongo {
namespace repl {

class SplitCollector {
    SplitCollector(const SplitCollector&) = delete;
    SplitCollector& operator=(const SplitCollector&) = delete;

public:
    SplitCollector(executor::TaskExecutor* executor, ReplSetConfig config);

    virtual ~SplitCoollector();

private:
    void _collect(const NamespaceString& nss) noexcept;

    Status _connect();

    DBClientBase* _getConnection(const HostAndPort& target);

    void _createNewCursor(bool initialFind);

    BSONObj _makeFindQuery(const NamespaceString& _nss) const;

    ConnectionPool _pool;
    std::unique_ptr<ConnectionPool::ConnectionPtr> _conn;

    const int _batchSize;
};

}  // namespace repl
}  // namespace mongo