        .link 1000
init:
        mov #init, sp
	jmp start

run_counter:
	.dw 0
run_counter_mw:
	.dw 0
run_counter_hw:
	.dw 0

kw11l_counter:
	.dw 0
kw11l_temp:
	.dw 0
	.dw 0

	.dw 0
	.dw 0
	.dw 0
	.dw 0
	.dw 0
scratch:
	.dw 0
	.dw 0
	.dw 0
	.dw 0
	.dw 0
scratch2:
	.dw 0
	.dw 0
	.dw 0
	.dw 0
	.dw 0

hex_chars: .ascii "0123456789abcdef"
value_buffer: .ascii "________________________"

text_hello: .ascii "Benchmark starting...\r\n"
	.db 0
text_go: .ascii "Go!\r\n"
	.db 0
text_finished: .ascii "Finished: "
	.db 0
text_crlf: .ascii "\r\n"
	.db 0

kw11l_isr:
	mov R0,(kw11l_temp)
	mov R1,(kw11l_temp+2)
        MOV     #0177546, R0    ; KW11-L CSR
        MOV     (R0), R1        ; read CSR
        BIC     #200, R1        ; clear DONE bit (bit 7)
        MOV     R1, (R0)        ; write back to acknowledge
	mov (kw11l_temp),R0
	mov (kw11l_temp+2),R1

	inc (kw11l_counter)
	rti

start:
	mov #text_hello,R2
	JSR PC,PRINT

	; setup KW11-L ISR
	MOV     #kw11l_isr, @#100      ;  ISR address
        MOV     #0300,      @#102      ;  PSW (IPL 6)
	mov     #0177546,R0
	MOV     #0101,(R0)          ; enable KW11-L interrupts

	spl 0

	mov #text_go,R2
	JSR PC,PRINT

loop:
	; statistics
	add #1,(run_counter)
	adc (run_counter_mw)
	adc (run_counter_hw)

	; finished?
	cmp #310,(kw11l_counter)   ; 50 Hz, 4 seconds
	ble  print_results

	; access to registers
	mov #123, R0
	add #123, R0
	sub #123, R0
	mov R0, R1
	; SP
	mov R6, R1
	SPL 7
	mov #123, R6
	add #123, R6
	sub #123, R6
	mov R1,R6
	SPL 0
	; PC
	ADD #2, R7
	NOP

	; shift + div
	mov #123456, R0
	ASH #-2,R0
	mov #1324, R1
	ASH #1,R1
	mov #37777, R2
	div r2,r0

	; mul
	mov #7,R0
	mov #17,R1
	MUL R2,R0

	; compare
	CMP R0,R1
	bne bne_hit
	beq beq_hit
bne_hit:
beq_hit:

	; addressing modes
	mov #400, R5
addr_move_loop:
	;mov #scratch2,(#scratch)

	MOV R0, R1     ; 0
	MOV #scratch,R1
	MOV R0, (R1)   ; 1
	MOV R0, (R1)+  ; 2
	mov R0, @#scratch; 3
	MOV R0, -(R1)  ; 4
	MOV R0, -(R1)  ; 4
	;MOV R0, @-(R0) ; 5  <-- triggers a halt in simh
	MOV R0, 2(R1)  ; 6
	MOV R0, #scratch; 7

	DEC R5
	BNE addr_move_loop


	jmp loop

print_results:                     ; print results
	spl 7             ; stop increasing counter

	mov #value_buffer, R2
	mov #run_counter_hw, R1
	jsr pc,make_hex
	mov #run_counter_mw, R1
	jsr pc,make_hex
	mov #run_counter, R1
	jsr pc,make_hex
	movb #0,(R2)+

	mov #text_finished, R2
	JSR PC,PRINT
	mov #value_buffer, R2
	JSR PC,PRINT
	mov #text_crlf, R2
	JSR PC,PRINT

finished: halt
	jmp finished

make_hex: 
	movb (R1), R4     ; get first byte to process
	ash #-4,R4        ; high nibble
	BIC #177760,R4
	add #hex_chars, R4
	movb (R4),(R2)+
	movb (R1), R4     ; re-get byte to process
	BIC #177760,R4        ; low nibble
	add #hex_chars, R4
	movb (R4),(R2)+
	;
	inc R1
	movb (R1), R4     ; get second byte to process
	ash #-4,R4        ; high nibble
	BIC #177760,R4
	add #hex_chars, R4
	movb (R4),(R2)+
	movb (R1), R4     ; re-get byte to process
	BIC #177760,R4        ; low nibble
	add #hex_chars, R4
	movb (R4),(R2)+
	RTS PC

PRINT:  movb    (r2), r0
        beq     done_print
WAITTPS:   tstb    (0177564)       ; Check TPS
        bpl     WAITTPS
        movb    r0, (0177566)       ; Send character through 'TBP'
        inc     r2
        br      PRINT
done_print:
	RTS PC
