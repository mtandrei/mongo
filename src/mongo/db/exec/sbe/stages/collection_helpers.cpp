/**
 *    Copyright (C) 2021-present MongoDB, Inc.
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

#include "mongo/db/exec/sbe/stages/collection_helpers.h"

#include "mongo/db/catalog/collection_catalog.h"

namespace mongo::sbe {

// TODO Refactor this so it's shared between the two engines...
const CollectionPtr* restoreCollection(OperationContext* opCtx,
                                       const NamespaceString& collName,
                                       CollectionUUID collUuid,
                                       uint64_t catalogEpoch,
                                       const CollectionPtr* oldCollPtr,
                                       const RestoreContext& context) {
    auto ptr = oldCollPtr;
    if (context.type() == RestoreContext::RestoreType::kExternal) {
        // RequiresCollectionStage requires a collection to be provided in restore. However, it may
        // be null in case the collection got dropped or renamed.
        ptr = context.collection();
        invariant(ptr);

        // If we restore externally and get a null Collection we need to figure out if this was a
        // drop or rename. The external lookup could have been done for UUID or namespace.
        const auto& coll = *ptr;

        // If collection exists uuid does not match assume lookup was over namespace and treat this
        // as a drop.
        if (coll && coll->uuid() != collUuid) {
            PlanYieldPolicy::throwCollectionDroppedError(collUuid);
        }

        // If we didn't get a valid collection but can still find the UUID in the catalog then we
        // treat this as a rename.
        if (!coll) {
            auto catalog = CollectionCatalog::get(opCtx);
            auto newNss = catalog->lookupNSSByUUID(opCtx, collUuid);
            if (newNss && *newNss != collName) {
                PlanYieldPolicy::throwCollectionRenamedError(collName, *newNss, collUuid);
            }
        }
    }

    const auto& coll = *ptr;
    if (!coll) {
        PlanYieldPolicy::throwCollectionDroppedError(collUuid);
    }

    if (collName != coll->ns()) {
        PlanYieldPolicy::throwCollectionRenamedError(collName, coll->ns(), collUuid);
    }

    uassert(ErrorCodes::QueryPlanKilled,
            "the catalog was closed and reopened",
            CollectionCatalog::get(opCtx)->getEpoch() == catalogEpoch);
    return ptr;
}

}  // namespace mongo::sbe
