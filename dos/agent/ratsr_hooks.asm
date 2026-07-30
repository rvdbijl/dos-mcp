; RA-TSR timer, DOS-idle, and multiplex interrupt hooks.
;
; INT 1Ch and INT 28h switch to a private resident stack before entering C.
; The timer entry never permits DOS file work. INT 28h permits the bounded DOS
; file operations that are safe during the DOS idle interrupt.
;
; INT 2Fh AX=D05Ah provides install/uninstall discovery:
;   BX=0 -> AX=5A5Ah, BX=PSP, CX:DX=INT1C handler off:seg,
;           SI:DI=INT2F handler off:seg
;   BX=1 -> stop delivery, AX=5A5Ah, BX=PSP,
;           CX:DX=old INT1C off:seg, SI:DI=old INT2F off:seg

        .8086

DGROUP  GROUP   _DATA,_BSS

_DATA   SEGMENT WORD PUBLIC 'DATA'
        EXTRN   _ratsr_old_int1c:DWORD
        EXTRN   _ratsr_old_int28:DWORD
        EXTRN   _ratsr_old_int2f:DWORD
        EXTRN   _ratsr_psp:WORD
        EXTRN   _ratsr_ticks:WORD
        EXTRN   _ratsr_last_protocol_result:WORD
        EXTRN   _ratsr_last_send_result:WORD
        EXTRN   _dm_receive_ready:BYTE
        EXTRN   _dm_receive_length:WORD
        EXTRN   _transfer_kind:BYTE
_DATA   ENDS

_BSS    SEGMENT WORD PUBLIC 'BSS'
idle_active     DB      ?
saved_ss        DW      ?
saved_sp        DW      ?
; Packet decoding and response encoding each use roughly 1 KiB of bounded
; automatic storage and are nested below the worker.  Leave ample headroom for
; the DOS/BIOS wrappers used by file and keyboard operations.
resident_stack  DB      32768 DUP (?)
resident_top    LABEL   WORD
        PUBLIC  _ratsr_resident_end
_ratsr_resident_end LABEL BYTE
_BSS    ENDS

_TEXT   SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:_TEXT, DS:NOTHING
        EXTRN   ratsr_idle_worker_:NEAR
        EXTRN   ratsr_prepare_unload_:NEAR
        PUBLIC  ratsr_idle_handler_
        PUBLIC  ratsr_dos_idle_handler_
        PUBLIC  ratsr_multiplex_handler_
        PUBLIC  ratsr_bios_queue_word_
        PUBLIC  ratsr_set_old_vectors_
        PUBLIC  ratsr_set_old_int28_

chain_int1c     DD      0
chain_int28     DD      0
chain_int2f     DD      0

; Watcom register convention:
;   AX=INT 1Ch offset, DX=INT 1Ch segment
;   BX=INT 2Fh offset, CX=INT 2Fh segment
ratsr_set_old_vectors_ PROC NEAR
        mov     word ptr cs:[chain_int1c],ax
        mov     word ptr cs:[chain_int1c+2],dx
        mov     word ptr cs:[chain_int2f],bx
        mov     word ptr cs:[chain_int2f+2],cx
        ret
ratsr_set_old_vectors_ ENDP

; AX=INT 28h offset, DX=INT 28h segment
ratsr_set_old_int28_ PROC NEAR
        mov     word ptr cs:[chain_int28],ax
        mov     word ptr cs:[chain_int28+2],dx
        ret
ratsr_set_old_int28_ ENDP

; int ratsr_bios_queue_word(unsigned word)
;
; Append one BIOS key word to the original PC/XT 16-word keyboard ring.  The
; operation preserves the caller's interrupt state and performs no BIOS or DOS
; call, making it safe for the resident timer worker.
ratsr_bios_queue_word_ PROC NEAR
        push    bp
        mov     bp,sp
        push    es
        push    bx
        push    cx
        push    dx
        mov     cx,ax
        pushf
        cli
        mov     ax,0040h
        mov     es,ax
        mov     bx,word ptr es:[001Ch]
        cmp     bx,001Eh
        jb      queue_full
        cmp     bx,003Eh
        jae     queue_full
        test    bl,1
        jnz     queue_full
        mov     dx,word ptr es:[001Ah]
        cmp     dx,001Eh
        jb      queue_full
        cmp     dx,003Eh
        jae     queue_full
        test    dl,1
        jnz     queue_full
        mov     ax,bx
        add     ax,2
        cmp     ax,003Eh
        jb      queue_no_wrap
        mov     ax,001Eh
queue_no_wrap:
        cmp     ax,dx
        je      queue_full
        mov     word ptr es:[bx],cx
        mov     word ptr es:[001Ch],ax
        mov     ax,1
        jmp     short queue_done
queue_full:
        xor     ax,ax
queue_done:
        popf
        pop     dx
        pop     cx
        pop     bx
        pop     es
        pop     bp
        ret
ratsr_bios_queue_word_ ENDP

ratsr_idle_handler_ PROC FAR
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es
        mov     ax,DGROUP
        mov     ds,ax
        cmp     byte ptr [idle_active],0
        jne     idle_chain
        mov     byte ptr [idle_active],1
        mov     word ptr [saved_ss],ss
        mov     word ptr [saved_sp],sp
        cli
        mov     ss,ax
        mov     sp,offset resident_top
        cld
        sti
        xor     ax,ax
        call    ratsr_idle_worker_
        cli
        mov     ax,word ptr [saved_ss]
        mov     ss,ax
        mov     sp,word ptr [saved_sp]
        mov     byte ptr [idle_active],0

idle_chain:
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        jmp     dword ptr cs:[chain_int1c]
ratsr_idle_handler_ ENDP

ratsr_dos_idle_handler_ PROC FAR
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es
        mov     ax,DGROUP
        mov     ds,ax
        cmp     byte ptr [idle_active],0
        jne     dos_idle_chain
        mov     byte ptr [idle_active],1
        mov     word ptr [saved_ss],ss
        mov     word ptr [saved_sp],sp
        cli
        mov     ss,ax
        mov     sp,offset resident_top
        cld
        sti
        mov     ax,1
        call    ratsr_idle_worker_
        cli
        mov     ax,word ptr [saved_ss]
        mov     ss,ax
        mov     sp,word ptr [saved_sp]
        mov     byte ptr [idle_active],0

dos_idle_chain:
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        jmp     dword ptr cs:[chain_int28]
ratsr_dos_idle_handler_ ENDP

ratsr_multiplex_handler_ PROC FAR
        cmp     ax,0D05Ah
        jne     multiplex_chain
        cmp     bx,0
        je      multiplex_query
        cmp     bx,1
        je      multiplex_unload
        cmp     bx,2
        je      multiplex_ticks
        cmp     bx,3
        je      multiplex_int28
        xor     ax,ax
        iret

multiplex_query:
        push    ds
        mov     ax,DGROUP
        mov     ds,ax
        mov     bx,word ptr [_ratsr_psp]
        pop     ds
        mov     ax,05A5Ah
        mov     cx,offset ratsr_idle_handler_
        mov     dx,cs
        mov     si,offset ratsr_multiplex_handler_
        mov     di,cs
        iret

multiplex_unload:
        push    ds
        mov     ax,DGROUP
        mov     ds,ax
        cmp     byte ptr [_transfer_kind],0
        jne     multiplex_unload_busy
        call    ratsr_prepare_unload_
        mov     bx,word ptr [_ratsr_psp]
        mov     cx,word ptr [_ratsr_old_int1c]
        mov     dx,word ptr [_ratsr_old_int1c+2]
        mov     si,word ptr [_ratsr_old_int2f]
        mov     di,word ptr [_ratsr_old_int2f+2]
        pop     ds
        mov     ax,05A5Ah
        iret

multiplex_unload_busy:
        pop     ds
        xor     ax,ax
        mov     bx,1
        iret

multiplex_ticks:
        push    ds
        mov     ax,DGROUP
        mov     ds,ax
        mov     bx,word ptr [_ratsr_ticks]
        xor     cx,cx
        mov     cl,byte ptr [_dm_receive_ready]
        mov     dx,word ptr [_dm_receive_length]
        mov     si,word ptr [_ratsr_last_protocol_result]
        mov     di,word ptr [_ratsr_last_send_result]
        pop     ds
        mov     ax,05A5Ah
        iret

multiplex_int28:
        push    ds
        mov     ax,DGROUP
        mov     ds,ax
        mov     si,word ptr [_ratsr_old_int28]
        mov     di,word ptr [_ratsr_old_int28+2]
        pop     ds
        mov     ax,05A5Ah
        mov     cx,offset ratsr_dos_idle_handler_
        mov     dx,cs
        iret

multiplex_chain:
        jmp     dword ptr cs:[chain_int2f]
ratsr_multiplex_handler_ ENDP

_TEXT   ENDS
        END
