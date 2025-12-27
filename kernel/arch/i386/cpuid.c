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

inline uint32_t cpuid_get_logical_processor_count(){
	uint32_t res;
	__asm__ volatile("mov $1, %%eax\n cpuid\n mov %%ebx, %k0" : "=a"(res) :  : "memory");
	return (res >> 16) & 0xFF;
}
