#pragma once

#ifndef DX_PARSE_LAMBDA_H
#define DX_PARSE_LAMBDA_H

#include "lambda.h"
#include "interp.h"

#define u64 unsigned long long

struct ParseResult {
	enum { OK, ERR, EOF } type;
	union {
		Lambda* ok;
		enum { UNMATCHED_PAREN, EMPTY_SET, BAD_TOKEN, END_OF_LINE, UNRECOGNIZED_DEFINITION } err;
	} val;
};

typedef struct {
	int fd;
	char last;
} file_t;

__attribute__((hot))
static inline u64 read(int fd, char* buf, u64 len) {
	__asm__ volatile("syscall" : "=a"(len) : "a"(0), "D"(fd), "S"(buf), "d"(len) : "rcx", "r11", "memory");
	return len;
}

__attribute__((hot))
static inline char peek(file_t* file) {
	if (file->last != '\0') return file->last;
	char buf = '\0';
	read(file->fd, &buf, 1);
	file->last = buf;
	return buf;
}

__attribute__((hot))
static inline char pop(file_t* file) {
	if (file->last != '\0') {
		char tmp = file->last;
		file->last = '\0';
		return tmp;
	}
	char buf = '\0';
	read(file->fd, &buf, 1);
	return buf;
}

void skip_space(file_t*);

struct ParseResult parse_expr(file_t*, def_t*);

#undef u64

#endif
