; Packet-driver receive upcall for Open Watcom's 16-bit small model.
;
; A packet driver calls this far entry point twice.  AX=0 asks for a
; destination buffer and expects ES:DI in return.  AX=1 reports that CX
; bytes have been copied.  Crynwr drivers tolerate either RETF or IRET;
; using IRET matches the callback contract and the original Turbo C sample.
;
; This is deliberately assembly rather than a parameterized C interrupt
; function: interrupt parameter stack layouts are compiler-specific.

        .8086

DGROUP  GROUP   _BSS

_BSS    SEGMENT WORD PUBLIC 'BSS'
        EXTRN   _dm_receive_ready:BYTE
        EXTRN   _dm_receive_length:WORD
        EXTRN   _dm_receive_buffer:BYTE
_BSS    ENDS

_TEXT   SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:_TEXT, DS:NOTHING
        PUBLIC  dm_packet_receiver_

dm_packet_receiver_ PROC FAR
        cmp     ax,0
        jne     receive_complete

        push    ax
        mov     ax,DGROUP
        mov     es,ax
        cmp     byte ptr es:[_dm_receive_ready],0
        jne     discard_packet
        cmp     cx,1518
        ja      discard_packet
        mov     di,offset _dm_receive_buffer
        pop     ax
        iret

discard_packet:
        xor     di,di
        mov     es,di
        pop     ax
        iret

receive_complete:
        cmp     ax,1
        jne     receive_return
        push    ds
        push    ax
        mov     ax,DGROUP
        mov     ds,ax
        mov     word ptr [_dm_receive_length],cx
        mov     byte ptr [_dm_receive_ready],1
        pop     ax
        pop     ds

receive_return:
        iret
dm_packet_receiver_ ENDP

_TEXT   ENDS
        END
