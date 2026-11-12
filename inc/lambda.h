#pragma once

#ifndef DX_LAMBDA_H
#define DX_LAMBDA_H

typedef struct Lambda Lambda;

Lambda* var(int);
Lambda* func(Lambda*);
Lambda* apply(Lambda*, Lambda*);

Lambda* own(Lambda*);
Lambda* copy(Lambda*);
void drop(Lambda*);

Lambda* beta_reduce(Lambda*);
unsigned long long snprint_lmd(char* buf, unsigned long long len, Lambda*);

void pool_drop(void);
#endif
