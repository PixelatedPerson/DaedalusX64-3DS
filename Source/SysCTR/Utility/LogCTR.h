#ifndef __LOGCTR_H__
#define __LOGCTR_H__
// Crash-safe log: sdmc:/3ds/DaedalusX64/log.txt (each line flushed + synced,
// so the last line survives a hard freeze / forced power-off).
#ifdef __cplusplus
extern "C" {
#endif
void CtrLogOpen(void);
void CtrLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#ifdef __cplusplus
}
#endif
#endif
