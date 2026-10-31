#include "kbcore.h"

const char *kbc_version_string(void)
{
    return KBCORE_VERSION_STRING;
}

unsigned int kbc_version_number(void)
{
    return ((unsigned int)KBCORE_VERSION_MAJOR << 16) |
           ((unsigned int)KBCORE_VERSION_MINOR << 8) |
           (unsigned int)KBCORE_VERSION_PATCH;
}
