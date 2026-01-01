#include "./cpuid.h"

inline struct ext_feature_info cpuid_get_ext_feature_info()
{
	struct ext_feature_info res;
	__asm__ volatile("mov $1, %%eax\n cpuid\n mov %%ecx, %k0" : "=a"(res) : : "memory");
	return res;
}

inline struct feature_info cpuid_get_feature_info()
{
	struct feature_info res;
	__asm__ volatile("mov $1, %%eax\n cpuid\n mov %%edx, %k0" : "=a"(res) : : "memory");
	return res;
}

bool cpuid_has_apic()
{
	struct feature_info feat = cpuid_get_feature_info();
	return feat.APIC;
}

uint8_t cpuid_apic_id()
{
	uint32_t ebx;
	__asm__ volatile("mov $1, %%eax\n\t"
			 "cpuid\n\t"
			 "mov %%ebx, %0"
			 : "=r"(ebx)
			 :
			 : "%eax", "%ecx", "%edx", "memory");
	return (ebx >> 24) & 0xFF;
}
