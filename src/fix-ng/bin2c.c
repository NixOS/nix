#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

void print(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    if (vprintf(format, ap) < 0) abort();
    va_end(ap);
}

int main(int argc, char * * argv)
{
    int c;
    if (argc != 2) abort();
    print("static unsigned char %s[] = {", argv[1]);
    while ((c = getchar()) != EOF) {
        print("0x%02x, ", (unsigned char) c);
    }
    print("};\n");
    return 0;
}
