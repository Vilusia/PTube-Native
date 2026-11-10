#!/bin/sh
set -eu
git config --global --add safe.directory '/work/*'
python3 /work/native/patch-upstream.py
TF=/work/upstream/komi-tube/vendor/tilefinch
cd "$TF"
cmake --preset release \
    -DCMAKE_C_FLAGS='-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -include limits.h' \
    -DCMAKE_C_STANDARD_LIBRARIES=-lm \
    -DPSP_BROWSER_BUILD_JSC_SPIKE=OFF \
    -DPSP_BROWSER_TRANSPORT_SOURCE="$TF/src/fetch.c"
cmake --build build-preset-release -j4
OUT=/work/native/test-results
mkdir -p "$OUT"
cc -std=c11 -O2 -Wall -Wextra -Werror /work/native/tests/guard_test.c -o "$OUT/guard_test"
"$OUT/guard_test"
cc -std=c11 -O2 -Wall -Wextra -Werror /work/native/tests/recovery_test.c -o "$OUT/recovery_test"
"$OUT/recovery_test"
cc -std=c11 -O2 -Wall -Wextra -Werror /work/native/tests/discover_test.c /work/native/app/discover.c -o "$OUT/discover_test"
"$OUT/discover_test"
APP=/work/native/app
TESTS=/work/upstream/komi-tube/native/tests
cc -std=c11 -O1 -Wall -Wextra -o "$OUT/search_probe" \
    -I"$TF/include" -I"$APP" "$TESTS/search_probe.c" "$APP/yt_results.c" \
    -L"$TF/build-preset-release" -ltilefinch_core -Wl,-rpath,"$TF/build-preset-release"
cc -std=c11 -O1 -Wall -Wextra -o "$OUT/mylist_test" \
    -I"$APP" "$TESTS/mylist_test.c" "$APP/mylist.c"
"$OUT/mylist_test" "$OUT/mylist-test.txt"
cc -std=c11 -O1 -Wall -Wextra -o "$OUT/kanji_test" \
    -I"$APP" "$TESTS/kanji_test.c" "$APP/kanji.c"
"$OUT/kanji_test"
for fixture in "$TESTS"/fixtures/search-*.html; do
    "$OUT/search_probe" --file "$fixture" > "$OUT/$(basename "$fixture").txt"
done
for fixture in "$TESTS"/fixtures/api-search-*.html; do
    "$OUT/search_probe" --json 'https://m.youtube.com/results?search_query=psp' "$fixture" > "$OUT/$(basename "$fixture").txt"
done
echo 'Native frontend fixture tests passed.'
ctest --test-dir build-preset-release -j1 --output-on-failure
