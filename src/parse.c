//gcc -c this -o ../bin/parse.o -O3 -nostdlib -fno-builtin -Wall -Wextra -Wno-builtin-declaration-mismatch
#include "../inc/lambda.h"
#include "../inc/parse.h"
#include "../inc/interp.h"

#define NULL ((void*)0)
typedef unsigned long long u64;

static struct ParseResult parse_file(file_t*, def_t*);

__attribute__((noreturn, cold))
static inline void exit(int code) {
	__asm__ volatile("syscall" :: "a"(60), "D"(code));
	__builtin_unreachable();
}

static inline _Bool isspace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

static inline _Bool isdigit(char c) {
	return c >= '0' && c <= '9';
}

void skip_space(file_t* file) {
	while (isspace(peek(file))) pop(file);
}

static inline struct ParseResult ok(Lambda* l) {
	struct ParseResult ret = {0};
	ret.type = OK;
	ret.val.ok = l;
	return ret;
}

static inline struct ParseResult err(int code) {
	struct ParseResult ret = {0};
	ret.type = ERR;
	ret.val.err = code;
	return ret;
}

static inline struct ParseResult eof() { return (struct ParseResult) { EOF, {0} }; }

struct ParseResult parse_expr(file_t* file, def_t* defs) {
	if (peek(file) == ')' || peek(file) == '\n') return err(EMPTY_SET);
	if (peek(file) == '\0') return eof();
	struct ParseResult p = parse_file(file, defs);
	if (p.type != OK) return p;
	Lambda* val = p.val.ok;
	while (peek(file) != ')' && peek(file) != '\n') {
		struct ParseResult p = parse_file(file, defs);
		if (p.type != OK) { drop(val); return p; }
		val = apply(val, p.val.ok);
		skip_space(file);
	}
	return ok(val);
}

static struct ParseResult parse_lambda(file_t* file, def_t* defs) {
	struct ParseResult ret = parse_expr(file, defs);
	if (ret.type != OK) return ret;
	ret.val.ok = func(ret.val.ok);
	return ret;
}

static struct ParseResult parse_symbol(file_t* file, def_t* defs) {
	char buf[32];
	int len = 0;
	_Bool is_ind = 1;
	char c = peek(file);
	while (!isspace(c) && c != ')' && c != '\n' && c != '(' && c != '\\' && len < 32) {
		if (c == '\0') return eof();
		if (is_ind && (!isdigit(c) || len >= 10)) is_ind = 0;
		buf[len++] = pop(file);
		c = peek(file);
	}

	if (is_ind) {
		int val = 0;
		for (int i = 0; i < len; i++) val = val*10 + buf[i] - '0';
		return ok(var(val));
	} else {
		Lambda* l = def_get(defs, buf, len);
		return l == NULL ? err(UNRECOGNIZED_DEFINITION) : ok(l);
	}
}

static struct ParseResult parse_file(file_t* file, def_t* defs) {
	skip_space(file);
	switch (peek(file)) {
		case '\0': return eof();
		case '(':
			pop(file);
			struct ParseResult ret = parse_expr(file, defs);
			if (ret.type == OK && peek(file) != ')') { // closing paren left for parse_lambda
				drop(ret.val.ok);
				return err(UNMATCHED_PAREN);
			}
			pop(file);
			return ret;
		case ')': return err(UNMATCHED_PAREN);
		case '\\':
			pop(file);
			return parse_lambda(file, defs);
		case '\n': return err(END_OF_LINE);
		default: return parse_symbol(file, defs);
	}
}
