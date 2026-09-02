#include <stdio.h>
#include <string.h>
#include <unistd.h>

int putchar(int character)
{
	unsigned char byte = (unsigned char)character;
	return write(1, &byte, 1) == 1 ? byte : EOF;
}

int puts(const char *text)
{
	size_t length = strlen(text);
	if (write(1, text, length) != (ssize_t)length || write(1, "\n", 1) != 1)
		return EOF;
	return 0;
}
