#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "./arg.h"

int arg_trimn(char const *const str, uint const len, char **const newstr) {
    if (len == 0) {
        *newstr = malloc(1);
        **newstr = '\0';
        return 0;
    }

    uint begin = 0;

    for (int i = 0; i < len; i++) {
        char c = *(str + i);
        if (isspace(c) || c == '"') begin++;
        else break;
    }

    uint end = len;

    for (int i = len - 1; i >= begin; i--) {
        char c = *(str + i);
        if (isspace(c) || c == '"') end--;
        else break;
    }

    uint const newlen = end - begin;

    *newstr = malloc(newlen + 1);
    memcpy(newstr, str + begin, newlen);
    **(newstr + newlen) = '\0';

    return newlen;
}

