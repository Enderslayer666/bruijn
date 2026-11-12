#pragma once

#ifndef DX_ALLOC_H
#define DX_ALLOC_H

struct Allocator;
typedef struct Allocator Allocator;

Allocator* construct(void);
void destroy(Allocator*);
void* alloc(Allocator*, unsigned long long);
void free(Allocator*, void*);
void* realloc(Allocator*, void*, unsigned long long);
void dump_data(Allocator*);

#endif
