# xBRZ

This directory contains the xBRZ scaler by Zenju, sourced from PPSSPP v1.20.4
commit `fa50bb1976065c4f8b1b47af227d367fe9771555`:

<https://github.com/hrydgard/ppsspp/tree/fa50bb1976065c4f8b1b47af227d367fe9771555/ext/xbrz>

The files are distributed under the GNU General Public License with the MAME
linking exception stated in their headers; see the full copyright and license
notice at the top of each source file. The local `xbrz.h` removes PPSSPP's
platform-config include and uses the standard `<cstdint>` header directly. The
scaling algorithm is otherwise unchanged.
