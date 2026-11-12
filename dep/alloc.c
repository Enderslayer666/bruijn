//gcc this -o alloc.o -c -O2 -fPIC -Wno-builtin-declaration-mismatch -fno-builtin -nostdlib
//gcc this -o alloc.o -c -O0 -Wno-builtin-declaration-mismatch -fno-builtin -nostdlib -ggdb
#include "alloc.h"

#define PAGE_SIZE ((4 * 1024 * 1024) - sizeof(Allocator))
#define SECT_SIZE (PAGE_SIZE - sizeof(struct Page) - 2*sizeof(Section))
#define SENTINEL_OFF (PAGE_SIZE - sizeof(Section))

#define MIN_SECT 32

#define ALLOC_SEPARATE ((u64)-1)
#define NULL ((void*)0)

#define PROT_READ 1
#define PROT_WRITE 2

#define MAP_PRIVATE 2
#define MAP_ANON 0x20

typedef unsigned long long u64;
typedef signed long long i64;

static inline void perror(const char* src, u64 length) {
	i64 out;
	__asm__ volatile("syscall" : "=a"(out) : "a"(1), "D"(2), "S"(src), "d"(length) : "rcx", "r11", "memory");
}

static inline void print(const char* src, u64 length) {
	i64 out;
	__asm__ volatile("syscall" : "=a"(out) : "a"(1), "D"(1), "S"(src), "d"(length) : "rcx", "r11", "memory");
}

static inline void* mmap(void* addr, u64 size, u64 prot, u64 flags, int fd, int offset) {
	void* ret;
	register u64 r10 __asm__("r10") = flags;
	register int r8 __asm__("r8") = fd;
	register int r9 __asm__("r9") = offset;
	__asm__ volatile("syscall" : "=a"(ret) : "a"(9), "D"(addr), "S"(size), "d"(prot), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
	return ret;
}

static inline void munmap(void* addr, u64 size) {
	i64 out;
	__asm__ volatile("syscall" : "=a"(out) : "a"(11), "D"(addr), "S"(size) : "rcx", "r11", "memory");
}

static inline void __attribute__((noreturn)) exit(i64 code) {
	__asm__ volatile("syscall" :: "a"(60), "D"(code));
	__builtin_unreachable();
}

static inline void printhex(u64 n) {
	char buf[24];
	u64 pos = 23;

	do {
		buf[pos--] = (n&15)>9 ? 'A'+(n&15)-10 : '0'+(n&15);
		n >>= 4;
	} while (n != 0);

	buf[pos--] = 'x';
	buf[pos--] = '0';

	print(buf+pos+1, 23 - pos);
}

static inline void memcpy(char* restrict dst, const char* restrict src, u64 n) {
	if (dst == NULL || src == NULL || n == 0) return;
	char* d = (char*)dst;
	char* s = (char*)src;

	while (n > 0 && ((u64)d & (sizeof(u64)-1))) {
		*d++ = *s++; n--;
	}

	u64* dw = (u64*)d;
	u64* sw = (u64*)s;

	while (n >= sizeof(u64)) {
		*dw++ = *sw++; n-=sizeof(u64);
	}

	d = (char*)dw;
	s = (char*)sw;
	while (n > 0) {
		*d++ = *s++; n--;
	}
}

enum State { FREE, ALLOCD, MMAPPED, PAGE_END };

struct Section {
	u64 length;
	struct Section* next;
	struct Section* prev;
	struct Section* prev_mem;
};

typedef struct Section Section;

struct Page {
	struct Page* next;
	struct Page* prev;
};

struct Allocator {
	struct Page* pages;
	Section* free_list;
};

static inline u64 len(Section* sect) { return sect->length & ~15ULL; }
static inline enum State state(Section* sect) { return sect->length & 15; }
static inline Section* next_mem(Section* sect) { return (void*)((char*)(sect+1) + len(sect)); }

Allocator* construct(void) {
	Allocator* allocator = mmap(NULL, PAGE_SIZE + sizeof(Allocator), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if ((i64)allocator < 0) {
		perror("\n[alloc] \x1b[31mFATAL: mmap failed\x1b[0m\n", 37);
		exit(-(i64)allocator);
	}
	struct Page* page = (void*)(allocator+1);
	page->next = NULL;
	page->prev = NULL;
	allocator->pages = page;

	Section* sect = (void*)(page+1);
	sect->length = SECT_SIZE;
	sect->next = NULL;
	sect->prev = NULL;
	sect->prev_mem = NULL;
	allocator->free_list = sect;

	Section* sentinel = (void*)((char*)page + SENTINEL_OFF);
	sentinel->length = PAGE_END;

	return allocator;
}

void destroy(Allocator* allocator) {
	struct Page* page = allocator->pages;
	struct Page* next = page->next;
	while (next != NULL) { munmap(page, PAGE_SIZE); page = next; next = page->next; }
	munmap(allocator, PAGE_SIZE + sizeof(Allocator));
}

static void alloc_page(Allocator* allocator) {
	struct Page* page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if ((i64)page < 0) {
		perror("\n[alloc] \x1b[31mFATAL: mmap failed\x1b[0m\n", 37);
		exit(-(i64)page);
	}
	page->next = allocator->pages;
	page->prev = NULL;
	allocator->pages->prev = page;
	allocator->pages = page;

	Section* sect = (void*)(page+1);
	sect->length = SECT_SIZE | FREE;
	sect->next = allocator->free_list;
	sect->prev = NULL;
	sect->prev_mem = NULL;
	if (allocator->free_list != NULL) allocator->free_list->prev = sect;
	allocator->free_list = sect;

	Section* sentinel = (void*)((char*)page + SENTINEL_OFF);
	sentinel->length = PAGE_END;
}

static inline Section* find_fit(Allocator* allocator, u64 size) {
	Section* sect = allocator->free_list;

	while (sect != NULL && len(sect) < size) sect = sect->next;

	return sect;
}

static inline void* alloc_from_section(Allocator* allocator, Section* sect, u64 size) {
	Section* prev = sect->prev;

	if (len(sect) > size + sizeof(Section) + MIN_SECT) { // [ (len next) ...... ] => [ (len) result ] + [ (rem next) .... ]
		char* result = (void*)(sect + 1);
		Section* rem = (void*)(result + size);
		rem->length = (len(sect) - size - sizeof(Section)) | FREE;
		rem->next = sect->next;
		rem->prev = sect->prev;
		rem->prev_mem = sect;
		next_mem(rem)->prev_mem = rem;
		if (sect->next != NULL) sect->next->prev = rem;
		sect->length = size | ALLOCD;
		if (prev == NULL) allocator->free_list = rem;
		else prev->next = rem;
		return result;
	} else { // [ (len next) ... ] => [ (len) result ]
		if (sect->next != NULL) sect->next->prev = prev;
		if (prev == NULL) allocator->free_list = sect->next;
		else prev->next = sect->next;
		sect->length = len(sect) | ALLOCD;
		return sect + 1;
	}
}

void* alloc(Allocator* allocator, u64 size) {
	if (size == 0) size = 16;
	else size = (size + 15) & ~15ULL;
	if (size > SECT_SIZE) {
		Section* ptr = mmap(NULL, size + sizeof(Section), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if ((i64)ptr < 0) {
			perror("\n[alloc] \x1b[31mFATAL: mmap failed\x1b[0m\n", 37);
			exit(-(i64)ptr);
		}
		ptr->length = size | MMAPPED;
		return (void*)(ptr+1);
	}

	Section* sect = find_fit(allocator, size);

	if (sect == NULL) {
		// retry once
		//defragment(allocator);
		//sect = find_fit(allocator, size);
		//if (sect != NULL) return alloc_from_section(allocator, sect, size);

		alloc_page(allocator);
		return alloc_from_section(allocator, allocator->free_list, size);
	} else return alloc_from_section(allocator, sect, size);
}

void free(Allocator* allocator, void* ptr) {
	if (ptr == NULL) return;
	Section* sect = (void*)((Section*)ptr - 1);
	if (state(sect) == MMAPPED) munmap(sect, len(sect) + sizeof(Section));
	else if (state(sect) == ALLOCD) {
		Section* next = next_mem(sect);
		while (state(next) == FREE) {
			sect->length = (len(sect) + sizeof(Section) + len(next)) | FREE;
			if (next->prev == NULL) allocator->free_list = next->next;
			else next->prev->next = next->next;
			if (next->next != NULL) next->next->prev = next->prev;

			next = next_mem(sect);
		}

		if (sect->prev_mem == NULL || state(sect->prev_mem) != FREE) {
			sect->next = allocator->free_list;
			sect->prev = NULL;
			if (allocator->free_list != NULL) allocator->free_list->prev = sect;
			sect->length = len(sect) | FREE;
			allocator->free_list = sect;
		} else {
			sect->prev_mem->length = (len(sect->prev_mem) + sizeof(Section) + len(sect));
			sect = sect->prev_mem;
			if (sect->prev_mem != NULL && state(sect->prev_mem) == FREE) perror("\n[alloc]: DANGEROUS: pool invariant broken\n", 43);
		}
		next->prev_mem = sect;

		if (sect->prev_mem == NULL && state(next) == PAGE_END && (void*)((struct Page*)sect-1)->next != NULL) {
			if (sect->prev == NULL) allocator->free_list = sect->next;
			else sect->prev->next = sect->next;
			if (sect->next != NULL) sect->next->prev = sect->prev;

			struct Page* page = (struct Page*)sect - 1;
			if (page->prev == NULL) allocator->pages = page->next;
			else page->prev->next = page->next;
			page->next->prev = page->prev;
			munmap((struct Page*)sect - 1, PAGE_SIZE);
		}
	} else if (state(sect) == FREE) perror("\n[alloc]: ERROR: double-free detected\n", 38);
	else perror("\n[alloc]: ERROR: tried to free invalid pointer\n", 47);
}

void dump_data(Allocator* allocator) {
	print("ALLOCATOR:\n", 11);
	for (Section* sect = allocator->free_list; sect != NULL; sect = sect->next) {
		print("\tSECT ", 6);
		printhex(len(sect));
		print(" : <ptr:", 8);
		printhex((u64)sect);
		print(">\n", 2);
	}
}

void* realloc(Allocator* allocator, void* ptr, u64 size) {
	if (size == 0) size = 16;
	size = (size + 15) & ~15ULL;
	Section* sect = (Section*)ptr - 1;
	if (size == len(sect)) return ptr;
	if (state(sect) != ALLOCD) {
		if (state(sect) == MMAPPED) {
			void* new = alloc(allocator, size);
			memcpy(new, ptr, size < len(sect) ? size : len(sect));
			free(allocator, ptr);
			return new;
		} else if (state(sect) == FREE) perror("\n[alloc] ERROR: use-after-free detected in realloc\n", 51);
		else perror("\n[alloc] ERROR: invalid pointer for realloc\n", 44);
		return ptr;
	}

	if (size < len(sect)) {
		Section* next = next_mem(sect);
		u64 const old_size = len(sect);
		if (state(next) == FREE) {
			Section const next_old = *next;
			sect->length = size | ALLOCD;
			next = next_mem(sect);
			next->length = ((next_old.length & ~15ULL) + old_size - size) | FREE;
			next->prev = next_old.prev;
			next->next = next_old.next;
			next->prev_mem = sect;
			if (next->prev == NULL) allocator->free_list = next;
			else next->prev->next = next;
			if (next->next != NULL) next->next->prev = next;
			next_mem(next)->prev_mem = next;
		} else if (len(sect) - size > sizeof(Section) + MIN_SECT) {
			sect->length = size | ALLOCD;
			Section* rem = next_mem(sect);
			rem->length = (old_size - size - sizeof(Section)) | FREE;
			rem->prev_mem = sect;
			next_mem(rem)->prev_mem = rem;
			rem->prev = NULL;
			rem->next = allocator->free_list;
			if (allocator->free_list != NULL) allocator->free_list->prev = rem;
			allocator->free_list = rem;
		}
		return ptr;
	}

	Section* next = (void*)((char*)ptr + len(sect));
	if (state(next) != FREE || len(next) + len(sect) + sizeof(Section) < size) {
		void* new = alloc(allocator, size);
		memcpy(new, ptr, len(sect));
		free(allocator, ptr);
		return new;
	}

	Section* prev = next->prev;

	if (len(next) + len(sect) > size + MIN_SECT) {
		Section next_old = *next;
		Section* rem = (void*)((char*)(sect+1) + size);
		rem->length = ((next_old.length & ~15ULL) + len(sect) - size) | FREE;
		if (prev == NULL) allocator->free_list = rem;
		else prev->next = rem;
		rem->next = next_old.next;
		if (next_old.next != NULL) next_old.next->prev = rem;
		rem->prev = next_old.prev;
		rem->prev_mem = sect;
		next_mem(rem)->prev_mem = rem;
		sect->length = size | ALLOCD;
		return sect+1;
	} else {
		if (prev == NULL) allocator->free_list = next->next;
		else prev->next = next->next;
		if (next->next != NULL) next->next->prev = prev;
		sect->length += sizeof(Section) + len(next);
		next_mem(sect)->prev_mem = sect;
		return sect+1;
	}
}
