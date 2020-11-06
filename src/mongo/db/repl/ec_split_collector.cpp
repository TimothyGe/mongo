#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kReplication

#include "mongo/db/repl/ec_split_collector.h"

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/mutable/document.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/logv2/log.h"

namespace mongo {
namespace repl {

namespace {
const Milliseconds kSplitCollectorSocketTimeout(30 * 1000);  // 30s
}
SplitCollector::SplitCollector(const ReplicationCoordinator* replCoord,
                               const NamespaceString& nss,
                               BSONObj* out)
    : _replCoord(replCoord),
      _nss(nss),
      _oid(out->getStringField("_id")),
      _out(out) {

    LOGV2(30008,
            "SplitCollector::SplitCollector",
            "ns"_attr = _nss.toString(),
            "self"_attr = _replCoord->getSelfIndex(),
            "_oid"_attr = _oid.toString());
}

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

    LOGV2_DEBUG(30009,
                2,
                "SplitCollector::_connect success");

    return connectStatus;
}

BSONObj SplitCollector::_makeFindQuery() const {
    BSONObjBuilder queryBob;
    queryBob.append("query", BSON("_id" << _oid));
    return queryBob.obj();
}

void SplitCollector::collect() noexcept {
    const auto& members = _replCoord->getConfig().members();
    LOGV2(30011,
            "SplitCollector::collect, members",
            "member.size"_attr = members.size());
    for (auto mid = 0; mid < members.size(); ++mid) {
        if (mid == _replCoord->getSelfIndex())
            continue;
        auto target = members[mid].getHostAndPort();
        LOGV2(30012,
            "SplitCollector::collect, target",
            "target"_attr = target.toString());
        auto conn = std::make_unique<DBClientConnection>(true);
        auto connStatus = _connect(conn, target);
        auto handler = [mid, this](const BSONObj& queryResult) {
            _splits[mid] = queryResult.getStringField(splitsFieldName);
        };

        _projection =
            BSON("o" << BSON(splitsFieldName
                             << BSON("$arrayElemAt" << BSON_ARRAY(std::string("$") + splitsFieldName << mid))));

        LOGV2_DEBUG(30007,
                    2,
                    "SplitCollector::collect",
                    "mid"_attr = mid,
                    "self"_attr = _replCoord->getSelfIndex(),
                    "_projection"_attr = _projection.toString());

        // QueryOption_Exhaust
        auto count = conn->query(handler,
                                 _nss,
                                 _makeFindQuery(),
                                 &_projection,
                                 QueryOption_SlaveOk | QueryOption_Exhaust);
    }

    _toBSON();
}

void SplitCollector::_toBSON() const {
    // _splits[replCoord->getSelfIndex()] = _out->getStringField(splitsFieldName);

    mutablebson::Document document(*_out);
    // auto splitsField = mutablebson::findFirstChildNamed(document.root(), splitsFieldName);
    // invariant(splitsField.countChildren() == 1);
    // splitsField.popBack();

    // for (const auto& split : _splits) {
    //     splitsField.pushBack(split);
    // }
    *_out = document.getObject();
}


}  // namespace repl
}  // namespace mongo
