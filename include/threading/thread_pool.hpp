#pragma once
#include "../../modules/threading/thread_pool.hpp"

#ifndef NEBULA_COMPAT_NS_THREADING
#define NEBULA_COMPAT_NS_THREADING
namespace threading = nebula::threading;
using ThreadPool = nebula::threading::ThreadPool;
using CallBack = nebula::threading::CallBack;
#endif
