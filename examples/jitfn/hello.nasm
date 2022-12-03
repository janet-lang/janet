BITS 64

;;;
;;; Code
;;;
mov rax, 1          ; write(
mov rdi, 1          ;   STDOUT_FILENO,
lea rsi, [rel msg]  ;   msg,
mov rdx, msglen     ;   sizeof(msg)
syscall             ; );
ret                 ; return;

;;;
;;; Constants
;;;
msg: db "Hello, world!", 10
msglen: equ $ - msg
