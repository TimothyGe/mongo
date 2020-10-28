#include "mongo/db/repl/ec_split_collector.h"

namespace mongo {
namespace repl {

namespace {
const Milliseconds kSplitCollectorSocketTimeout(30 * 1000);  // 30s
}
SplitCollector::SplitCollector(executor::TaskExecutor* executor, ReplSetConfig config)
    : _executor(executor), _rsConfig(config), _pool(0) {}

SplitCollector::~SplitCollector() {}

Status _connect() {
    Status connectStatus = Status::OK();

    return connectStatus;
}

DBClientBase* _getConnection(const HostAndPort& target) {
    if (!_conn.get()) {
        _conn.reset(new ConnectionPool::ConnectionPtr(
            &_pool, target, Date_t::now(), kSplitCollectorSocketTimeout));
    };
    return _conn->get();
};

void SplitCollector::_collect(const NamespaceString& nss) noexcept {
    for (auto& target : servers) {
        _cursor =
            std::make_unique<DBClientCursor>(_getConnection(target),
                                             _nss,
                                             _makeFindQuery(nss),
                                             0,
                                             0,
                                             QueryOption_CursorTailable | QueryOption_AwaitData |
                                                 QueryOption_OplogReplay | QueryOption_Exhaust,
                                             1);
    }
}


}  // namespace repl
}  // namespace mongo