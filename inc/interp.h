#pragma once

#ifndef DX_LAMBDA_INTERP_H
#define DX_LAMBDA_INTERP_H

#include "lambda.h"

typedef struct Def def_t;

Lambda* def_get(def_t* defs, const char* name, int name_len);

#endif
