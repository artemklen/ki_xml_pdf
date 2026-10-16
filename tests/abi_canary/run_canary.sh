#!/bin/bash
# Run precompiled ABI canaries against the libki_xml_pdf.so.0 in LIBDIR and
# check their PDFs with Poppler.
# usage: run_canary.sh BINDIR LIBDIR FONT OUTDIR
set -euo pipefail
bindir=$1
libdir=$2
font=$3
out=$4
mkdir -p "$out"
if [[ -n "$(find "$out" -mindepth 1 -print -quit)" ]]; then
    echo "output directory $out is not empty" >&2
    exit 1
fi
(cd "$bindir" && sha256sum -c SHA256SUMS >/dev/null)
export LD_LIBRARY_PATH=$libdir
for bin in abi_canary_c abi_canary_cpp; do
    resolved=$(ldd "$bindir/$bin" | awk '/libki_xml_pdf\.so\.0/ {print $3}')
    if [[ "$(readlink -f "$resolved")" != "$(readlink -f "$libdir/libki_xml_pdf.so.0")" ]]; then
        echo "$bin resolved libki_xml_pdf.so.0 to '$resolved', not $libdir" >&2
        exit 1
    fi
    echo "$bin uses $resolved"
    "$bindir/$bin" "$out" "$font" | tee "$out/$bin.log"
done
check() {
    local pdf=$1
    shift
    local text
    text=$(pdftotext -layout "$out/$pdf" -)
    for want in "$@"; do
        if [[ "$text" != *"$want"* ]]; then
            echo "$pdf lacks $want" >&2
            exit 1
        fi
    done
}
check c-file.pdf item "id: 7" ABI_C_FILE_MARKER
check c-callback.pdf ABI_C_REPORT ABI_C_REC_A ABI_C_REC_B "Всего записей: 3" "Выбрано записей: 2" "Пропущено записей: 1"
if pdftotext "$out/c-callback.pdf" - | grep -q ABI_C_REC_C; then
    echo "c-callback.pdf contains the unselected record" >&2
    exit 1
fi
check c-mapper.pdf MAPPED:ABI_C_MAPPED_ONE MAPPED:ABI_C_MAPPED_TWO
check cpp-file.pdf ABI_CPP_FILE_MARKER
check cpp-callback.pdf ABI_CPP_REPORT ABI_CPP_REC_2 "Пропущено записей: 1"
check cpp-mapper.pdf CPP_MAPPED:ABI_CPP_MAPPED
echo "abi canaries ok libdir=$libdir"
