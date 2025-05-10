#pragma once

#define LXC_LOG_BUFFER_SIZE	4096

int isula_libutils_get_log_fd(void);

#define C_SPDLOG_FATAL(format, ...)                                           \
  do {                                                                        \
    c_spdlog_error("[{fatal} %s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define C_SPDLOG_ERROR(format, ...)                                           \
  do {                                                                        \
    c_spdlog_error("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define C_SPDLOG_WARN(format, ...)                                            \
  do {                                                                        \
    c_spdlog_warn("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);      \
  } while (0)

#define C_SPDLOG_INFO(format, ...)                                            \
  do {                                                                        \
    c_spdlog_info("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);      \
  } while (0)

#define C_SPDLOG_DEBUG(format, ...)                                           \
  do {                                                                        \
    c_spdlog_debug("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define CRIT(format, ...)                                           \
  do {                                                                        \
    c_spdlog_error("[CRIT][%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define FATAL(format, ...)                                           \
  do {                                                                        \
    c_spdlog_error("[FATAL][%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define ERROR(format, ...)                                           \
  do {                                                                        \
    c_spdlog_error("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define WARN(format, ...)                                            \
  do {                                                                        \
    c_spdlog_warn("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);      \
  } while (0)

#define INFO(format, ...)                                            \
  do {                                                                        \
    c_spdlog_info("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);      \
  } while (0)

#define DEBUG(format, ...)                                           \
  do {                                                                        \
    c_spdlog_debug("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define EVENT(format, ...)                                           \
  do {                                                                        \
    c_spdlog_error("[EVENT][%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define COMMAND_ERROR(format, ...)                                           \
  do {                                                                        \
    c_spdlog_error("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define SYSERROR(format, ...)                                           \
  do {                                                                        \
    c_spdlog_error("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define SYSWARN(format, ...)                                           \
  do {                                                                        \
    c_spdlog_warn("[%s:%d] " format, __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#define DECLARE_C_SPDLOG_LEVEL(level) \
    void c_spdlog_##level(const char *format, ...);


#ifdef __cplusplus
extern "C" {
#endif


DECLARE_C_SPDLOG_LEVEL(error)
DECLARE_C_SPDLOG_LEVEL(warn)
DECLARE_C_SPDLOG_LEVEL(info)
DECLARE_C_SPDLOG_LEVEL(debug)

#ifdef __cplusplus
}
#endif
