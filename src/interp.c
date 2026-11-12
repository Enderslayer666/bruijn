//gcc -c this -o ../bin/interp.o -O3 -nostdlib -fno-builtin -Wall -Wextra -Wno-builtin-declaration-mismatch
#include "../inc/lambda.h"
#include "../inc/parse.h"
#include "../inc/interp.h"
#include "../dep/alloc.h"

typedef unsigned long long u64;
#define NULL ((void*)0)

static inline void write(int fd, char* buf, u64 len) {
	__asm__ volatile("syscall" : "=a"(len) : "a"(1), "D"(fd), "S"(buf), "d"(len) : "rcx", "r11", "memory");
}

__attribute__((noreturn))
static inline void exit(int code) {
	__asm__ volatile("syscall" :: "a"(60), "D"(code));
	__builtin_unreachable();
}

static inline int open(const char* path, int mode) {
	__asm__ volatile("syscall" : "=a"(mode) : "a"(2), "D"(path), "S"(mode) : "rcx", "r11", "memory");
	return mode;
}

static inline void close(int fd) {
	__asm__ volatile("syscall" : "=a"(fd) : "a"(3), "D"(fd) : "rcx", "r11", "memory");
}

static inline file_t fopen(const char* path, int path_len) {
	char pathbuf[path_len+1];
	for (int i = 0; i < path_len; i++) pathbuf[i] = path[i];
	pathbuf[path_len] = '\0';
	int fd = open(pathbuf, 0);
	return (file_t){ fd, '\0' };
}

struct sigaction {
	void (*act)(void);
	u64 flags;
	void (*rec)(void);
	u64 mask;
};

static inline void sigaction(int code, const struct sigaction* new_act, struct sigaction* old_act) {
	register int r10 __asm__("r10") = 8;
	__asm__ volatile("syscall" : "=a"(code) : "a"(13), "D"(code), "S"(new_act), "d"(old_act), "r"(r10) : "rcx", "r11", "memory");
}

struct Def {
	struct Def* left;
	struct Def* right;
	Lambda* value;
	int name_len;
	char name[];
};

Allocator* ALLOC;
def_t DEF_ROOT = {0};

static void def_add(def_t* defs, const char* name, int name_len, Lambda* val) {
	_Bool next = 0;

	struct Def* curr;
	struct Def* prev;
	for (curr = defs; curr != NULL; curr = next ? curr->left : curr->right) {
		if (curr->name_len != name_len) {
			next = curr->name_len < name_len;
			prev = curr;
			continue;
		}

		for (int i = 0; i < name_len; i++) if (curr->name[i] != name[i]) {
			next = curr->name[i] < name[i];
			prev = curr;
			goto over;
		}

		drop(curr->value);
		curr->value = val;
		return;
	over:
		continue;
	}

	def_t* new_def = alloc(ALLOC, sizeof(struct Def) + name_len);
	new_def->left = 0;
	new_def->right = 0;
	new_def->value = val;
	new_def->name_len = name_len;
	for (int i = 0; i<name_len; i++) new_def->name[i] = name[i];

	if (next) prev->left = new_def;
	else prev->right = new_def;
}

__attribute__((hot))
Lambda* def_get(def_t* defs, const char* name, int name_len) {
	_Bool next = 0;
	for (def_t* def = defs; def != NULL; def = next ? def->left : def->right) {
		if (def->name_len != name_len) {
			next = def->name_len < name_len;
			continue;
		}

		for (int i = 0; i < name_len; i++) if (def->name[i] != name[i]) {
			next = def->name[i] < name[i];
			goto over;
		}

		return own(def->value);
	over:
		continue;
	}

	return NULL;
}

static void def_rem(def_t* defs, const char* name, int name_len) {
	def_t* prev = NULL;
	_Bool next = 0;
	for (def_t* def = defs; def != NULL; def = next ? def->left : def->right) {
		if (def->name_len != name_len) {
			next = def->name_len < name_len;
			prev = def;
			continue;
		}

		for (int i = 0; i < name_len; i++) if (def->name[i] != name[i]) {
			next = def->name[i] < name[i];
			prev = def;
			goto over;
		}

		if (prev == NULL) return;

		if (def->left == NULL) {
			if (next) prev->left = def->right;
			else prev->right = def->right;
		} else if (def->right == NULL) {
			if (next) prev->left = def->left;
			else prev->right = def->left;
		} else if (def->right->left == NULL) {
			if (next) prev->left = def->right;
			else prev->right = def->right;
			def->right->left = def->left;
		} else { // one right . all left => closest to the right of the original
			def_t* rep = def->right;
			def_t* rep_prev;
			do {
				rep_prev = rep;
				rep = rep->left;
			} while (rep->left != NULL);
			rep_prev->left = rep->right;
			rep->right = def->right;
			rep->left = def->left;
			if (next) prev->left = rep;
			else prev->right = rep;
		}

		drop(def->value);
		free(ALLOC, def);
		return;
	over:
		continue;
	}
}

static void print_defs(def_t* defs) {
	if (defs->left != NULL) print_defs(defs->left);
	if (defs->name_len != 0) {
		write(1, ":defn ", 6);
		write(1, defs->name, defs->name_len);
		int len = snprint_lmd(NULL, 0, defs->value);
		char buf[len+2]; buf[0] = ' ';
		len = snprint_lmd(buf+1, len, defs->value);
		buf[len+1] = '\n';
		write(1, buf, len+2);
	}
	if (defs->right != NULL) print_defs(defs->right);
}

static void free_def(def_t* defs) {
	drop(defs->value);
	if (defs->left != NULL) free_def(defs->left);
	if (defs->right != NULL) free_def(defs->right);
	free(ALLOC, defs);
}

__attribute__((noreturn))
static void quit(void) {
	write(1, "\n", 1);
	if (DEF_ROOT.left != NULL) free_def(DEF_ROOT.left);
	if (DEF_ROOT.right != NULL) free_def(DEF_ROOT.right);
	destroy(ALLOC);
	pool_drop();
	exit(0);
}

static void nop(void) {};

static _Bool isspace(char c) {
	return c == ' ' || c == '\t' || c == '\v' || c == '\r' || c == '\f';
}

static _Bool streq(char* a, char* b, int len) {
	for (int i = 0; i < len; i++) if (a[i] != b[i]) return 0;
	return 1;
}

static void parse_log(int code) {
	switch (code) {
		case UNMATCHED_PAREN: write(1, "SYNTAX: Unmatched Close Paren\n", 30); break;
		case EMPTY_SET: write(1, "SYNTAX: expressions must not be empty (no empty parens)\n", 56); break;
		case BAD_TOKEN: write(1, "SYNTAX: this interpreter uses De Bruijn indices; atoms should be numbers\n", 73); break;
		case END_OF_LINE: write(1, "SYNTAX: unexpected line break; expected statement on one line\n", 62); break;
		case UNRECOGNIZED_DEFINITION: write(1, "RUNTIME: undefined reference\n", 29); break;
	}
}

void process(file_t* file, char* prompt, int prompt_len, void (*over)(void)) {
	while (1) {
		if (prompt_len != 0) write(1, prompt, prompt_len);

		if (peek(file) == '\n') { pop(file); continue; }
		if (peek(file) == ':') {
			pop(file); // ':'
			char cmd[5];
			int cmd_len = 0;
			while (cmd_len < 5 && !isspace(peek(file)) && peek(file) != '\n' && peek(file) != '\0') cmd[cmd_len++] = pop(file);
			skip_space(file);
			_Bool args = peek(file) != '\n';

			char arg[32];
			int arg_len = 0;
			if (args) while (arg_len < 32 && !isspace(peek(file)) && peek(file) != '\n' && peek(file) != '\0') arg[arg_len++] = pop(file);

			if (cmd_len == 4) {
				if (streq(cmd, "defn", 4)) {
					if (!args) goto expect_args;
					struct ParseResult p = parse_expr(file, &DEF_ROOT);
					if (p.type == ERR) parse_log(p.val.err);
					else if (p.type == EOF) {
						over();
						return;
					} else {
						Lambda* expr = p.val.ok;
						def_add(&DEF_ROOT, arg, arg_len, expr);
					}
				} else if (streq(cmd, "grab", 4)) {
					if (!args) goto expect_args;
					file_t f = fopen(arg, arg_len);
					process(&f, NULL, 0, nop);
					close(f.fd);
				} else if (streq(cmd, "eval", 4)) {
					if (!args) goto expect_args;
					Lambda* l = def_get(&DEF_ROOT, arg, arg_len);
					if (l!=NULL) {
						beta_reduce(l);
						drop(l);
					} else {
						write(1, "ERR: directive :eval : unrecognized identifier `", 48);
						write(1, arg, arg_len);
						write(1, "`\n", 2);
					}
				} else if (streq(cmd, "list", 4)) {
					if (args) {
						Lambda* l = def_get(&DEF_ROOT, arg, arg_len);
						if (l == NULL) {
							write(1, ":undef ", 7);
							write(1, arg, arg_len);
							write(1, "\n", 1);
						} else {
							write(1, ":defn ", 6);
							write(1, arg, arg_len);
							int len = snprint_lmd(NULL, 0, l);
							char buf[len+2]; buf[0] = ' ';
							len = snprint_lmd(buf+1, len, l);
							buf[len+1] = '\n';
							write(1, buf, len+2);
							drop(l);
						}
					} else print_defs(&DEF_ROOT);
				} else goto unrecognized;
			} else if (cmd_len == 5) {
				if (streq(cmd, "undef", 5)) {
					if (!args) goto expect_args;
					def_rem(&DEF_ROOT, arg, arg_len);
				} else if (streq(cmd, "clear", 5)) write(1, "\x1b[2J\x1b[H\x1b[3J", 11);
				else goto unrecognized;
			} else goto unrecognized;
			goto end;
		unrecognized:
			write(1, "ERR: unrecognized directive :", 29); write(1, cmd, cmd_len); write(1, "\n", 1);
			goto end;
		expect_args:
			write(1, "ERR: directive :", 16); write(1, cmd, cmd_len); write(1, " expects arguments\n", 19);
			goto end;
		end:
			while (peek(file) != '\n' && peek(file) != '\0') pop(file);
			pop(file); continue;
		} else {
			struct ParseResult p = parse_expr(file, &DEF_ROOT);
			if (p.type == EOF) { over(); return; }
			if (peek(file) != '\n') {
				if (p.type == OK) {
					drop(p.val.ok);
					write(1, "SYNTAX: one line should not contain multiple statements\n", 56);
				} else parse_log(p.val.err);
				while (peek(file) != '\n' && peek(file) != '\0') pop(file);
				pop(file);
				continue;
			}
			pop(file);
			if (p.type == ERR) parse_log(p.val.err);
			else {
				Lambda* expr = p.val.ok;
				beta_reduce(expr);

				u64 len = snprint_lmd(NULL, 0, expr);
				char buf[len+1];
				len = snprint_lmd(buf, len, expr);
				buf[len] = '\n';
				write(1, buf, len+1);

				drop(expr);
			}
		}
	}
}

__attribute__((noreturn, force_align_arg_pointer))
void _start(void) {
	struct sigaction sa_quit = { quit, 0x04000000, quit, 0 };
	sigaction(2, &sa_quit, NULL);

	ALLOC = construct();
	file_t file = {0};
	process(&file, " > ", 3, quit);
	__builtin_unreachable();
}
