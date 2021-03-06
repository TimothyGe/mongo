/**
 *    Copyright (C) 2020-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/bson/bsonobj.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/pipeline/document_source_lookup.h"
#include "mongo/db/pipeline/document_source_match.h"
#include "mongo/db/pipeline/document_source_sort.h"
#include "mongo/db/s/resharding_util.h"
#include "mongo/s/grid.h"

namespace mongo {

void checkForHolesAndOverlapsInChunks(std::vector<ReshardedChunk>& chunks,
                                      const KeyPattern& keyPattern) {
    std::sort(chunks.begin(), chunks.end(), [](const ReshardedChunk& a, const ReshardedChunk& b) {
        return SimpleBSONObjComparator::kInstance.evaluate(a.getMin() < b.getMin());
    });
    // Check for global minKey and maxKey
    uassert(ErrorCodes::BadValue,
            "Chunk range must start at global min for new shard key",
            SimpleBSONObjComparator::kInstance.evaluate(chunks.front().getMin() ==
                                                        keyPattern.globalMin()));
    uassert(ErrorCodes::BadValue,
            "Chunk range must end at global max for new shard key",
            SimpleBSONObjComparator::kInstance.evaluate(chunks.back().getMax() ==
                                                        keyPattern.globalMax()));

    boost::optional<BSONObj> prevMax = boost::none;
    for (auto chunk : chunks) {
        if (prevMax) {
            uassert(ErrorCodes::BadValue,
                    "Chunk ranges must be contiguous",
                    SimpleBSONObjComparator::kInstance.evaluate(prevMax.get() == chunk.getMin()));
        }
        prevMax = boost::optional<BSONObj>(chunk.getMax());
    }
}

void validateReshardedChunks(const std::vector<mongo::BSONObj>& chunks,
                             OperationContext* opCtx,
                             const KeyPattern& keyPattern) {
    std::vector<ReshardedChunk> validChunks;
    for (const BSONObj& obj : chunks) {
        auto chunk = ReshardedChunk::parse(IDLParserErrorContext("reshardedChunks"), obj);
        uassertStatusOK(
            Grid::get(opCtx)->shardRegistry()->getShard(opCtx, chunk.getRecipientShardId()));
        validChunks.push_back(chunk);
    }

    checkForHolesAndOverlapsInChunks(validChunks, keyPattern);
}

void checkForOverlappingZones(std::vector<TagsType>& zones) {
    std::sort(zones.begin(), zones.end(), [](const TagsType& a, const TagsType& b) {
        return SimpleBSONObjComparator::kInstance.evaluate(a.getMinKey() < b.getMinKey());
    });

    boost::optional<BSONObj> prevMax = boost::none;
    for (auto zone : zones) {
        if (prevMax) {
            uassert(ErrorCodes::BadValue,
                    "Zone ranges must not overlap",
                    SimpleBSONObjComparator::kInstance.evaluate(prevMax.get() <= zone.getMinKey()));
        }
        prevMax = boost::optional<BSONObj>(zone.getMaxKey());
    }
}

void validateZones(const std::vector<mongo::BSONObj>& zones,
                   const std::vector<TagsType>& authoritativeTags) {
    std::vector<TagsType> validZones;

    for (const BSONObj& obj : zones) {
        auto zone = uassertStatusOK(TagsType::fromBSON(obj));
        auto zoneName = zone.getTag();
        auto it =
            std::find_if(authoritativeTags.begin(),
                         authoritativeTags.end(),
                         [&zoneName](const TagsType& obj) { return obj.getTag() == zoneName; });
        uassert(ErrorCodes::BadValue, "Zone must already exist", it != authoritativeTags.end());
        validZones.push_back(zone);
    }

    checkForOverlappingZones(validZones);
}

std::unique_ptr<Pipeline, PipelineDeleter> createAggForReshardingOplogBuffer(
    const boost::intrusive_ptr<ExpressionContext>& expCtx,
    const boost::optional<ReshardingDonorOplogId>& resumeToken) {
    std::list<boost::intrusive_ptr<DocumentSource>> stages;

    if (resumeToken) {
        stages.emplace_back(DocumentSourceMatch::create(
            BSON("_id" << BSON("$gt" << resumeToken->toBSON())), expCtx));
    }

    stages.emplace_back(DocumentSourceSort::create(expCtx, BSON("_id" << 1)));

    BSONObjBuilder lookupBuilder;
    lookupBuilder.append("from", expCtx->ns.coll());
    lookupBuilder.append("let",
                         BSON("preImageId" << BSON("clusterTime"
                                                   << "$preImageOpTime.ts"
                                                   << "ts"
                                                   << "$preImageOpTime.ts")
                                           << "postImageId"
                                           << BSON("clusterTime"
                                                   << "$postImageOpTime.ts"
                                                   << "ts"
                                                   << "$postImageOpTime.ts")));
    lookupBuilder.append("as", kReshardingOplogPrePostImageOps);

    BSONArrayBuilder lookupPipelineBuilder(lookupBuilder.subarrayStart("pipeline"));
    lookupPipelineBuilder.append(
        BSON("$match" << BSON(
                 "$expr" << BSON("$in" << BSON_ARRAY("$_id" << BSON_ARRAY("$$preImageId"
                                                                          << "$$postImageId"))))));
    lookupPipelineBuilder.done();

    BSONObj lookupBSON(BSON("" << lookupBuilder.obj()));
    stages.emplace_back(DocumentSourceLookUp::createFromBson(lookupBSON.firstElement(), expCtx));

    return Pipeline::create(std::move(stages), expCtx);
}

}  // namespace mongo