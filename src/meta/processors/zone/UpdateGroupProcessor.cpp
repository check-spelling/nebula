/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License.
 */

#include "meta/processors/zone/UpdateGroupProcessor.h"

namespace nebula {
namespace meta {

void AddZoneIntoGroupProcessor::process(const cpp2::AddZoneIntoGroupReq& req) {
  folly::SharedMutex::ReadHolder rHolder(LockUtils::groupLock());
  auto groupName = req.get_group_name();
  auto groupIdRet = getGroupId(groupName);
  if (!nebula::ok(groupIdRet)) {
    auto retCode = nebula::error(groupIdRet);
    LOG(ERROR) << "Get Group failed, group " << groupName
               << " error: " << apache::thrift::util::enumNameSafe(retCode);
    handleErrorCode(retCode);
    onFinished();
    return;
  }

  auto groupKey = MetaKeyUtils::groupKey(groupName);
  auto groupValueRet = doGet(std::move(groupKey));
  if (!nebula::ok(groupValueRet)) {
    auto retCode = nebula::error(groupValueRet);
    if (retCode == nebula::cpp2::ErrorCode::E_KEY_NOT_FOUND) {
      retCode = nebula::cpp2::ErrorCode::E_GROUP_NOT_FOUND;
    }
    LOG(ERROR) << "Get group " << groupName << " failed, error "
               << apache::thrift::util::enumNameSafe(retCode);
    handleErrorCode(retCode);
    onFinished();
    return;
  }

  auto zoneName = req.get_zone_name();
  auto zoneNames = MetaKeyUtils::parseZoneNames(std::move(nebula::value(groupValueRet)));
  auto iter = std::find(zoneNames.begin(), zoneNames.end(), zoneName);
  if (iter != zoneNames.end()) {
    LOG(ERROR) << "Zone " << zoneName << " already exist in the group " << groupName;
    handleErrorCode(nebula::cpp2::ErrorCode::E_EXISTED);
    onFinished();
    return;
  }

  const auto& zonePrefix = MetaKeyUtils::zonePrefix();
  auto iterRet = doPrefix(zonePrefix);
  if (!nebula::ok(iterRet)) {
    auto retCode = nebula::error(iterRet);
    LOG(ERROR) << "Get zones failed, error: " << apache::thrift::util::enumNameSafe(retCode);
    handleErrorCode(retCode);
    onFinished();
    return;
  }
  auto zoneIter = nebula::value(iterRet).get();

  bool found = false;
  while (zoneIter->valid()) {
    auto name = MetaKeyUtils::parseZoneName(zoneIter->key());
    if (name == zoneName) {
      found = true;
      break;
    }
    zoneIter->next();
  }

  if (!found) {
    LOG(ERROR) << "Zone " << zoneName << " not found";
    handleErrorCode(nebula::cpp2::ErrorCode::E_ZONE_NOT_FOUND);
    onFinished();
    return;
  }

  zoneNames.emplace_back(zoneName);
  std::vector<kvstore::KV> data;
  data.emplace_back(std::move(groupKey), MetaKeyUtils::groupVal(zoneNames));
  LOG(INFO) << "Add Zone " << zoneName << " Into Group " << groupName;
  doSyncPutAndUpdate(std::move(data));
}

void DropZoneFromGroupProcessor::process(const cpp2::DropZoneFromGroupReq& req) {
  folly::SharedMutex::ReadHolder rHolder(LockUtils::groupLock());
  auto groupName = req.get_group_name();
  auto groupIdRet = getGroupId(groupName);
  if (!nebula::ok(groupIdRet)) {
    auto retCode = nebula::error(groupIdRet);
    LOG(ERROR) << " Get Group " << groupName
               << " failed, error: " << apache::thrift::util::enumNameSafe(retCode);
    handleErrorCode(retCode);
    onFinished();
    return;
  }

  auto groupKey = MetaKeyUtils::groupKey(groupName);
  auto groupValueRet = doGet(groupKey);
  if (!nebula::ok(groupValueRet)) {
    auto retCode = nebula::error(groupValueRet);
    if (retCode == nebula::cpp2::ErrorCode::E_KEY_NOT_FOUND) {
      retCode = nebula::cpp2::ErrorCode::E_GROUP_NOT_FOUND;
    }
    LOG(ERROR) << "Get group " << groupName
               << " failed, error: " << apache::thrift::util::enumNameSafe(retCode);
    handleErrorCode(retCode);
    onFinished();
    return;
  }

  auto zoneName = req.get_zone_name();
  auto zoneNames = MetaKeyUtils::parseZoneNames(std::move(nebula::value(groupValueRet)));
  auto iter = std::find(zoneNames.begin(), zoneNames.end(), zoneName);
  if (iter == zoneNames.end()) {
    LOG(ERROR) << "Zone " << zoneName << " not exist in the group " << groupName;
    handleErrorCode(nebula::cpp2::ErrorCode::E_ZONE_NOT_FOUND);
    onFinished();
    return;
  }

  const auto& spacePrefix = MetaKeyUtils::spacePrefix();
  auto spaceRet = doPrefix(spacePrefix);
  if (!nebula::ok(spaceRet)) {
    auto retCode = nebula::error(spaceRet);
    LOG(ERROR) << "List spaces failed, error " << apache::thrift::util::enumNameSafe(retCode);
    handleErrorCode(retCode);
    onFinished();
    return;
  }

  nebula::cpp2::ErrorCode spaceCode = nebula::cpp2::ErrorCode::SUCCEEDED;
  auto spaceIter = nebula::value(spaceRet).get();
  while (spaceIter->valid()) {
    auto properties = MetaKeyUtils::parseSpace(spaceIter->val());
    if (properties.group_name_ref().has_value() && *properties.group_name_ref() == groupName) {
      LOG(ERROR) << "Space is bind to the group " << *properties.group_name_ref();
      spaceCode = nebula::cpp2::ErrorCode::E_CONFLICT;
    }
    spaceIter->next();
  }

  if (spaceCode != nebula::cpp2::ErrorCode::SUCCEEDED) {
    handleErrorCode(spaceCode);
    onFinished();
    return;
  }

  zoneNames.erase(iter);
  std::vector<kvstore::KV> data;
  data.emplace_back(std::move(groupKey), MetaKeyUtils::groupVal(zoneNames));
  LOG(INFO) << "Drop Zone " << zoneName << " From Group " << groupName;
  doSyncPutAndUpdate(std::move(data));
}

}  // namespace meta
}  // namespace nebula
