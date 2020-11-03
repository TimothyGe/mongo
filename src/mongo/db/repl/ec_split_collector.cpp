#include "mongo/db/repl/ec_split_collector.h"

#include "mongo/db/jsobj.h"
#include "mongo/logv2/log.h"

namespace mongo {
namespace repl {

namespace {
const Milliseconds kSplitCollectorSocketTimeout(30 * 1000);  // 30s
}
SplitCollector::SplitCollector(ReplSetConfig config, const NamespaceString& nss, const OID& oid)
    : _rsConfig(config), _nss(nss), _oid(oid) {}

SplitCollector::~SplitCollector() {}

Status SplitCollector::_connect(ConnPtr& conn, const HostAndPort& target) {
    Status connectStatus = Status::OK();
    do {
        if (!connectStatus.isOK()) {
            conn->checkConnection();
        } else {
            conn->connect(target, "SplitCollector");
        }
    } while (!connectStatus.isOK());

    return connectStatus;
}

// {
//     projection:{
//         split:{
//             $arrayElemAt: ['$ec', 0]
//         }
//     }
// }

BSONObj SplitCollector::_makeFindQuery(int mid) const {
    BSONObjBuilder queryBob;
    queryBob.append("query", BSON("_id" << _oid));
    queryBob.append("projection", BSON("split" << BSON("$arrayElemAt" << BSON_ARRAY("$ec" << mid))));
    return queryBob.obj();
}

void SplitCollector::_collect() noexcept {
    const auto& members = _rsConfig.members();
    for (auto mid = 0; mid < members.size(); ++mid) {
        auto target = members[mid].getHostAndPort();
        auto conn = std::make_unique<DBClientConnection>(true);
        auto connStatus = _connect(conn, target);  // loop
        auto handler = [mid](const BSONObj& queryResult) {
            _splits[mid] = std::move(queryResult["split"].String());
        };

        // QueryOption_Exhaust
        auto count = conn->query(handler,
                                 _nss,
                                 _makeFindQuery(mid),
                                 nullptr,
                                 QueryOption_SlaveOk | QueryOption_Exhaust);
    }
    for (auto& s : _splits) {
    }
}


}  // namespace repl
}  // namespace mongo