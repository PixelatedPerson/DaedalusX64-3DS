#include <3ds.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include "LogCTR.h"

static FILE *sLog = NULL;

void CtrLogOpen(void)
{
	if (sLog) return;
	sLog = fopen("sdmc:/3ds/DaedalusX64/log.txt", "w");
	if (sLog) setvbuf(sLog, NULL, _IONBF, 0);
}

void CtrLog(const char *fmt, ...)
{
	if (!sLog) return;
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fprintf(sLog, "[%llu] %s\n", (unsigned long long)osGetTime(), buf);
	fflush(sLog);
	fsync(fileno(sLog));
}
