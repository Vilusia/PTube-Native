#!/bin/sh
set -eu
git config --global --add safe.directory '/work/*'
python3 /work/native/patch-upstream.py
cd /work/upstream/komi-tube/vendor/tilefinch
sh scripts/fetch-psp-transport-deps.sh
cmake --preset psp \
    -DPython3_EXECUTABLE=/usr/bin/python3 \
    -DCMAKE_AR="$PSPDEV/bin/psp-ar" -DCMAKE_RANLIB="$PSPDEV/bin/psp-ranlib" \
    -DCMAKE_NM="$PSPDEV/bin/psp-nm" -DCMAKE_STRIP="$PSPDEV/bin/psp-strip" \
    -DCMAKE_LINKER="$PSPDEV/bin/psp-ld" -DCMAKE_OBJDUMP="$PSPDEV/bin/psp-objdump" \
    -DCMAKE_C_FLAGS_MINSIZEREL='-Os -DNDEBUG' \
    -DPSP_BROWSER_ENABLE_PSP_VOICE=OFF \
    -DPSP_BROWSER_ENABLE_HOST_WEBASSEMBLY=OFF \
    -DKOMI_EXTRA_CMAKE=/work/native/ptube.cmake
cmake --build build-preset-psp --target ptube-app-elf ptube-app-prx -j4
cmake -DPSP_OBJDUMP="$PSPDEV/bin/psp-objdump" \
    -DPSP_ELF="$PWD/build-preset-psp/ptube-app/ptube-app-elf.elf" \
    -DPSP_TEXT_LIMIT=4480000 -P cmake/CheckPspTextSize.cmake
python3 /work/native/package.py
