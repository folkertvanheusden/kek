TMCS1 = 172522
TMWC  = 172524
TMBA  = 172526

READ = 000002       ; Read forward
GO   = 000001       ; Go bit
UNIT0= 000000       ; Unit 0

    mov #01000, r0
    mov r0,sp       ; needed for the rts pc at the end
    push r0
    mov #2, r1      ; 2 blocks to read
READ.BLOCK:
    mov r0, @#TMBA  ; Set Memory Address
    mov @#TMBA,R2
    clr @#TMCS1
    MOV #-512., @#TMWC ; Set negative byte count
    MOV #UNIT0+READ+GO, @#TMCS1  ; Do read

WAIT.TM:
    BIT #000200, @#TMCS1 ; Bit 7: Ready
    BEQ WAIT.TM     ; If not ready, wait

;    mov @#TMCS1,R2   ; any errors?
;    mov @#172520,R3  ; what (if any) errors?
;    halt

    add #512.,R0    ; on to the next record
    sob R1, READ.BLOCK

    rts pc
