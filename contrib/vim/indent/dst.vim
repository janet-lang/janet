" Vim filetype plugin file
" Language: DST
" Maintainer: Calvin Rose

if exists("b:did_indent")
	finish
endif
let b:did_indent = 1

let s:cpo_save = &cpo
set cpo&vim

setlocal noautoindent nosmartindent
setlocal softtabstop=2 shiftwidth=2 expandtab
setlocal indentkeys=!,o,O

let &cpo = s:cpo_save
