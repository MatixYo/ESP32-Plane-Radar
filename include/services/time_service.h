#pragma once

#include <ctime>

namespace services::time {

void init();
bool isSynced();
bool getLocalTimeInfo(struct tm* info);

}  // namespace services::time
