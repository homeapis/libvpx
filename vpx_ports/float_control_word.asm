;
;  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
;
;  Use of this source code is governed by a BSD-style license
;  that can be found in the LICENSE file in the root of the source
;  tree. An additional intellectual property rights grant can be found
;  in the file PATENTS.  All contributing project authors may
;  be found in the AUTHORS file in the root of the source tree.
;


%include "vpx_ports/x86_abi_support.asm"

section .text

; Dummy function to keep non-MSVC builds from complaining about an empty
; compilation unit.
global sym(vpx_empty_function) PRIVATE
sym(vpx_empty_function):
    mov   rax, 0
    ret

%if LIBVPX_YASM_WIN64
global sym(vpx_winx64_fldcw) PRIVATE
sym(vpx_winx64_fldcw):
    sub   rsp, 8
    mov   [rsp], rcx ; win x64 specific
    fldcw [rsp]
    add   rsp, 8
    ret


global sym(vpx_winx64_fstcw) PRIVATE
sym(vpx_winx64_fstcw):
    sub   rsp, 8
    fstcw [rsp]
    mov   rax, [rsp]
    add   rsp, 8
    ret
%endif
