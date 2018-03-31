//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//
// This module defines C++ functions that are to support builtin functions.
//
// See the header of file "/util/bfpg/bfpg.h" for more general overall information.
//--------------------------------------------------------------------------------------------------

#ifndef YB_UTIL_BFPG_BFUNC_H_
#define YB_UTIL_BFPG_BFUNC_H_

#include <iostream>
#include <string>

#include "yb/util/status.h"
#include "yb/util/logging.h"

// Include all builtin function templates.
#include "yb/util/bfpg/bfunc_standard.h"
#include "yb/util/bfpg/bfunc_convert.h"

#endif  // YB_UTIL_BFPG_BFUNC_H_
