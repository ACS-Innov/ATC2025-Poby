#include <spdlog/spdlog.h>
#include <stdarg.h>
#include <string>

#include "spdlog_wrapper.h"

#define C_SPDLOG_TEMPLATE(level) \
void c_spdlog_##level(const char *format, ...) { \
    va_list args; \
    va_start(args, format); \
    va_list args_copy; \
    va_copy(args_copy, args); \
    int length = vsnprintf(nullptr, 0, format, args_copy); \
    va_end(args_copy); \
    std::string logMessage(length + 1, '\0'); \
    vsprintf(&logMessage[0], format, args); \
    va_end(args); \
    spdlog::level(logMessage.c_str()); \
}

C_SPDLOG_TEMPLATE(error)
C_SPDLOG_TEMPLATE(warn)
C_SPDLOG_TEMPLATE(info)
C_SPDLOG_TEMPLATE(debug)