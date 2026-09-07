#!/bin/bash
set -euo pipefail

if [[ ! -x "./bintoc" || bintoc.c -nt ./bintoc ]]
then
	gcc bintoc.c -o bintoc
fi

find -type f -name "*.vert" | \
	while read f; do glslangValidator -V ${f} -o "Compiled/${f%.*}.vspv"; done

find -type f -name "*.frag" | \
	while read f; do glslangValidator -V ${f} -o "Compiled/${f%.*}.fspv"; done



find -type f -name "*.vspv" | \
	while read f; do ./bintoc ${f} `basename ${f%.*}`_vert_spv > ${f%.*}_vert.c; done

find -type f -name "*.fspv" | \
	while read f; do ./bintoc ${f} `basename ${f%.*}`_frag_spv > ${f%.*}_frag.c; done

for f in ./*.comp; do
    name="${f##*/}"
    name="${name%.comp}"
    glslangValidator --target-env vulkan1.2 -V "$f" -o "Compiled/$name.cspv"
    spirv-opt --target-env=vulkan1.2 -O "Compiled/$name.cspv" -o "Compiled/$name.cspv"
    spirv-val --target-env vulkan1.2 "Compiled/$name.cspv"
    ./bintoc "Compiled/$name.cspv" "${name}_comp_spv" "Compiled/${name}_comp.c"
done
