#include <string.h>

void *memcpy(void *__restrict dst,
             const void *__restrict src,
             size_t n)
{
    void *ret = dst;

    __asm__ volatile (
        "movl %%ecx, %%eax\n\t"
        "shrl $2, %%ecx\n\t"
        "rep movsl\n\t"
        "movl %%eax, %%ecx\n\t"
        "andl $3, %%ecx\n\t"
        "rep movsb"
        : "+D"(dst), "+S"(src), "+c"(n)
        :
        : "eax", "memory"
    );

    return ret;
}
