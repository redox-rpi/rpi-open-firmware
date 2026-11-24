#!/bin/bash
# Host test for thermal.c (rof #108): sensor conversion and the ARM clock
# limit steps. Needs gcc.   bootcode/tests/thermal-test.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
gcc -O1 -Wall -Wextra -Werror -o "$t/h" "$here/thermal-host.c"
"$t/h"
