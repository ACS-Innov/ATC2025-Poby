#pragma once
#include <doca_argp.h>
#include <doca_error.h>
#include <optional>
using std::optional;
doca_error_t register_params();
doca_error_t register_param(optional<const char *> short_name,
                            optional<const char *> long_name,
                            optional<const char *> description,
                            callback_func callback, doca_argp_type type);