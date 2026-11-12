//gcc -c this -o ../bin/lambda.o -Wall -Wextra -O3 -nostdlib
#include "../inc/lambda.h"

#define NULL ((void*)0)
typedef unsigned long long u64;

struct Lambda {
	enum { VAR, FUNC, APPLY } type;
	union {
		int var;
		Lambda* func;
		struct {
			Lambda* right;
			Lambda* left;
		} apply;
	} as;
	int refs;
};

static inline void* mmap(void* addr, u64 len, u64 prot, u64 flags, int fd, long off) {
	register u64 r10 __asm__("r10") = flags;
	register u64 r8 __asm__("r8") = fd;
	register u64 r9 __asm__("r9") = off;
	__asm__("syscall" : "=a"(addr) : "a"(9), "D"(addr), "S"(len), "d"(prot), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
	return addr;
}

static inline void munmap(void* addr, u64 len) {
	__asm__ volatile("syscall" : "=a"(addr) : "a"(11), "D"(addr), "S"(len) : "rcx", "r11", "memory");
}

static inline void write(int fd, char* buf, u64 len) {
	__asm__ volatile("syscall" : "=a"(len) : "a"(1), "D"(fd), "S"(buf), "d"(len) : "rcx", "r11", "memory");
}

// 2^n-1, to align with pages of memory
#define PAGE_LEN 4095

union PoolEnt {
	Lambda allocd;
	union PoolEnt* freed;
};

struct PoolPage {
	struct PoolPage* next;
	union PoolEnt entries[PAGE_LEN];
};

volatile struct {
	struct PoolPage* pages;
	union PoolEnt* freelist;
	u64 allocs;
	u64 frees;
} POOL;

__attribute__((hot))
static inline Lambda* pool_alloc(void) {
	if (POOL.freelist == NULL) {
		struct PoolPage* page = mmap(NULL, sizeof(struct PoolPage), 3, 0x22, -1, 0);
		for (int i = 0; i < PAGE_LEN; i++) {
			page->entries[i].freed = POOL.freelist;
			POOL.freelist = page->entries + i;
		}
		page->next = POOL.pages;
		POOL.pages = page;
	}

	union PoolEnt* first = POOL.freelist;
	POOL.freelist = first->freed;
	first->allocd.refs = 1;
	POOL.allocs++;
	return &first->allocd;
}

__attribute__((hot))
static inline void pool_free(Lambda* free) {
	union PoolEnt* entry = (void*)free;
	entry->freed = POOL.freelist;
	POOL.freelist = entry;
	POOL.frees++;
}

//__attribute__((destructor))
void pool_drop(void) {
	struct PoolPage* next;
	for (struct PoolPage* page = POOL.pages; page != NULL; page = next) {
		next = page->next;
		munmap(page, sizeof(struct PoolPage));
	}
	if (POOL.allocs != POOL.frees) write(2, "Leftover Allocations!\n", 22);
}

Lambda* var(int ind) {
	Lambda* ret = pool_alloc();
	ret->type = VAR;
	ret->as.var = ind;
	return ret;
}

Lambda* func(Lambda* body) {
	Lambda* ret = pool_alloc();
	ret->type = FUNC;
	ret->as.func = body;
	return ret;
}

Lambda* apply(Lambda* left, Lambda* right) {
	Lambda* ret = pool_alloc();
	ret->type = APPLY;
	ret->as.apply.left = left;
	ret->as.apply.right = right;
	return ret;
}

Lambda* own(Lambda* l) {
	l->refs++;
	return l;
}

Lambda* copy(Lambda* l) {
	if (l->refs == 1) return l;
	l->refs--; // can't hit 0
	switch (l->type) {
		case VAR: return var(l->as.var);
		case FUNC: return func(own(l->as.func));
		case APPLY: return apply(own(l->as.apply.left), own(l->as.apply.right));
	}
	__builtin_unreachable();
}

__attribute__((hot))
void drop(Lambda* l) {
	if (--l->refs != 0) return;
	switch (l->type) {
		case VAR: break;
		case FUNC: drop(l->as.func); break;
		case APPLY: drop(l->as.apply.left); drop(l->as.apply.right); break;
	}
	pool_free(l);
}

static inline Lambda* incr_vars_internal(Lambda* l, int depth) {
	switch (l->type) {
		case VAR:
			if (l->as.var >= depth) return var(l->as.var + 1);
			return NULL;
		case FUNC:
			Lambda* body = incr_vars_internal(l->as.func, depth + 1);
			return body==NULL ? NULL : func(body);
		case APPLY:
			Lambda* left = incr_vars_internal(l->as.apply.left, depth);
			Lambda* right = incr_vars_internal(l->as.apply.right, depth);
			return left==NULL && right==NULL ? NULL
				: apply(
					left==NULL ? own(l->as.apply.left) : left,
					right==NULL ? own(l->as.apply.right) : right);
	}
	__builtin_unreachable();
}

static inline Lambda* incr_vars(Lambda* l) {
	Lambda* ret = incr_vars_internal(l, 0);
	return ret==NULL ? own(l) : ret;
}

static Lambda* subst_fixing(Lambda* body, Lambda* val, int ind) {
	switch (body->type) {
		case VAR:
			if (body->as.var == ind) { drop(body); return own(val); }
			if (body->as.var > ind) { body = copy(body); body->as.var--; } // \(\\2 a) => \\1 ;; \(\\1 (0 0)) => \(\(1 1))
			return body;
		case FUNC:
			Lambda* val_fixed = incr_vars(val);
			body = copy(body);
			body->as.func = subst_fixing(body->as.func, val_fixed, ind+1);
			drop(val_fixed);
			return body;
		case APPLY:
			body = copy(body);
			body->as.apply.left = subst_fixing(body->as.apply.left, val, ind);
			body->as.apply.right = subst_fixing(body->as.apply.right, val, ind);
			return body;
	}
	__builtin_unreachable();
}

static void Apply(Lambda* app) {
	Lambda* fun = own(app->as.apply.left->as.func);
	drop(app->as.apply.left);
	Lambda* val = app->as.apply.right;
	Lambda* res = subst_fixing(fun, val, 0);
	drop(val);
	app->type = res->type;
	app->as = res->as;
	if (res->refs == 1) pool_free(res);
	else { // this shouldn't ever happen, but better safe than sorry
		res->refs--;
		switch (app->type) {
			case VAR: break;
			case FUNC: own(app->as.func); break;
			case APPLY:
				own(app->as.apply.left);
				own(app->as.apply.right);
		}
	}
}

Lambda* beta_reduce(Lambda* l) {
	switch (l->type) {
		case VAR: return l;
		case FUNC:
			l->as.func = beta_reduce(l->as.func);
			return l;
		case APPLY: {
			if (l->as.apply.left->type == FUNC) {
				Apply(l);
				return beta_reduce(l);
			}
			l->as.apply.left = beta_reduce(l->as.apply.left);
			if (l->as.apply.left->type == FUNC) {
				Apply(l);
				return beta_reduce(l);
			}
			l->as.apply.right = beta_reduce(l->as.apply.right);
			return l;
		}
	}
	__builtin_unreachable();
}

static inline u64 snprint_int(char* buf, u64 len, int i) {
	_Bool sign = i < 0;
	if (sign) {
		if (len != 0) { *buf++ = '-'; len--; }
		i = -i;
	}
	char tmp[22];
	u64 l = 0;
	do {
		tmp[l++] = '0' + i%10;
		i /= 10;
	} while (i != 0);

	for (u64 i = 0; i < len && i<l; i++) buf[i] = tmp[l - i - 1];
	return l + sign;
}

__attribute__((hot))
static inline void push(char* buf, u64 len, u64* x, char c) {
	if (len > *x) buf[*x] = c;
	(*x)++;
}

static inline u64 snprint_apply(char* buf, u64 len, Lambda* l, _Bool rec) {
	u64 x = 0;
	switch (l->as.apply.left->type) {
		case APPLY: x += snprint_apply(buf, len, l->as.apply.left, 1); break;
		case FUNC:
			push(buf, len, &x, '(');
			x += snprint_lmd(buf+x, len > x ? len-x : 0, l->as.apply.left);
			push(buf, len, &x, ')'); break;
		case VAR: x += snprint_lmd(buf, len, l->as.apply.left); break;
		default: __builtin_unreachable();
	}
	push(buf, len, &x, ' ');
	if (rec && l->as.apply.right->type == FUNC) {
		push(buf, len, &x, '(');
		x += snprint_lmd(buf+x, len > x ? len-x : 0, l->as.apply.right);
		push(buf, len, &x, ')');
		return x;
	} else return x + snprint_lmd(buf + x, len > x ? len - x : 0, l->as.apply.right);
}

u64 snprint_lmd(char* buf, u64 len, Lambda* l) {
	switch (l->type) {
		case VAR:
			return snprint_int(buf, len, l->as.var);
		case FUNC: {
			if (len > 0) buf[0] = '\\';
			return 1 + snprint_lmd(buf + 1, len > 1 ? len - 1 : 0, l->as.func);
		}
		case APPLY: {
			u64 x = 1; if (len != 0) buf[0] = '(';
			x += snprint_apply(buf + x, len > x ? len - x : 0, l, 0);
			if (len > x) buf[x] = ')';
			x++;
			return x;
		}
	}
	__builtin_unreachable();
}
