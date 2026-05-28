

#ifndef BINARIES_H
#define BINARIES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef struct DemoBinary {
    const char     *name;       
    const uint8_t  *data;       
    size_t          size;       
} DemoBinary;


const DemoBinary *binaries_get_all(int *count);


const DemoBinary *binaries_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif 
