#pragma once
#include "../../modules/threading/timer_scheduler.hpp"

#ifndef NEBULA_COMPAT_NS_THREADING
#define NEBULA_COMPAT_NS_THREADING
namespace threading = nebula::threading;
using TimerScheduler = nebula::threading::TimerScheduler;
#endif
