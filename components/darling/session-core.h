/* Internal host-only interface. Never called after entering guest TLS. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int arlinux_session_core_start(const char *prefix);
#ifdef __cplusplus
}
#endif
