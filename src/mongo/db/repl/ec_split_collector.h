#pragma once

#include "mongo/base/status_with.h"
#include "mongo/bson/timestamp.h"
#include "mongo/client/connection_pool.h"
#include "mongo/client/dbclient_connection.h"
#include "mongo/client/dbclient_cursor.h"
#include "mongo/db/namespace_string.h"

namespace mongo {
namespace repl {

using ConnPtr = std::unique_ptr<DBClientConnection>;
using CursorPtr = std::unique_ptr<DBClientCursor>;

class SplitCollector {
    SplitCollector(const SplitCollector&) = delete;
    SplitCollector& operator=(const SplitCollector&) = delete;

public:
    SplitCollector(ReplSetConfig config, const NamespaceString& nss, const OID& oid);

    virtual ~SplitCoollector();

private:
    void _collect() noexcept;

    Status _connect(ConnPtr& conn, const HostAndPort& target);

    BSONObj _makeFindQuery() const;
    BSONObj* _makeProjection(int mid) const;

    const std::string& _documentID;
    ReplSetConfig _rsConfig;
    NamespaceString _nss;
    std::vector<std::string> _splits;
};

}  // namespace repl
}  // namespace mongo