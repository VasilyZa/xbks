#ifndef XBKS_COMPILER_H
#define XBKS_COMPILER_H

#define XBKS_NORETURN __attribute__((noreturn))
#define XBKS_USED __attribute__((used))
#define XBKS_SECTION(name) __attribute__((section(name)))
#define XBKS_PACKED __attribute__((packed))
#define XBKS_ALIGNED(bytes) __attribute__((aligned(bytes)))

#endif
