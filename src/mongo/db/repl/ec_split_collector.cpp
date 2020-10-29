#include "mongo/db/repl/ec_split_collector.h"

namespace mongo {
namespace repl {

namespace {
const Milliseconds kSplitCollectorSocketTimeout(30 * 1000);  // 30s
}
SplitCollector::SplitCollector(ReplSetConfig config, const NamespaceString& nss, )
    : _rsConfig(config), _nss(nss), _documentID(id) {}

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

BSONObj SplitCollector::_makeFindQuery() const {
    BSONObjBuilder queryBob;
    queryBob.append("query", BSON("_id" << _documentID));
    return queryBob.obj();
}


// {
//     projection:{
//         split:{
//             $arrayElemAt: ['$ec', 0]
//         }
//     }
// }
BSONObj* SplitCollector::_makeProjection(int mid) const {
    BSONObjBuilder projBob;
    std::string arrJSON = "{$arrayElemAt: ['ec', " + std::to_string(mid) + "]}";
    projBob.append("projection", BSON("split" << fromjson(arrJSON)));
    return &projBob.obj();
}

void SplitCollector::_collect() noexcept {
    for (auto& m : _rsConfig.members()) {
        auto target = m.getHostAndPort()
        // if (target.isSelf()) {
        //     continue;
        // }
        int mid = 0;
        auto conn = std::make_unique<DBClientConnection>(true);
        auto connStatus = _connect(conn, target);  // loop
        auto handler = [mid](const BSONObj& queryResult) {
            _splits[mid] = std::move(queryResult["split"].String());
        };

        // QueryOption_Exhaust
        auto count = conn->query(handler,
                                 _nss,
                                 _makeFindQuery(),
                                 _makeProjection(mid),
                                 QueryOption_SlaveOk | QueryOption_Exhaust);
    }
    for (auto& s : _splits) {
        
    }
}


}  // namespace repl
}  // namespace mongo