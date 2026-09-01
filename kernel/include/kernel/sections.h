#pragma once

#define __error(s)      __attribute__((error (s)))
#define __warning(s)    __attribute__((warning (s)))

#define __const         __attribute__((const))
#define __pure          __attribute__((pure))

#define __hot           __attribute__((hot))
#define __cold          __attribute__((cold))
#define __section(s)    __attribute__((section(s)))

#define __init          __section(".init.text") __cold
#define __initdata      __section(".init.data")
#define __initconst     __section(".init.rodata")
