#pragma once
#include "../../modules/logger/logger.hpp"

#ifndef NEBULA_COMPAT_NS_LOGGER
#define NEBULA_COMPAT_NS_LOGGER
namespace logger = nebula::logger;
using Logger = nebula::logger::Logger;
#endif

#ifndef LOG_VECTOR
#define LOG_VECTOR(vector) LOGMSG_VECTOR(vector)
#endif
