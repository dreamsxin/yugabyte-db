# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

add_library(pgsql-pbgen
            pg_coder.cc
            pg_gen_column.cc
            pg_gen_bcall.cc
            pg_gen_expr.cc
            pg_gen_const.cc)

target_link_libraries(pgsql-pbgen
                      pgsql-proto
                      yb_client
                      yb_util)
add_dependencies(pgsql-pbgen pgsql-syn)
