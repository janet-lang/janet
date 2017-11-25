" Vim syntax file
" Language: DST
" Maintainer: Calvin Rose

if exists("b:current_syntax")
    finish
endif

let s:cpo_sav = &cpo
set cpo&vim

if has("folding") && exists("g:dst_fold") && g:dst_fold > 0
    setlocal foldmethod=syntax
endif

syntax keyword DstCommentTodo contained FIXME XXX TODO FIXME: XXX: TODO:

" DST comments
syn match DstComment "#.*$" contains=DstCommentTodo,@Spell

syntax match DstStringEscape '\v\\%([\\btnfr"])' contained
syntax region DstString matchgroup=DstStringDelimiter start=/"/ skip=/\\\\\|\\"/ end=/"/ contains=DstStringEscape,@Spell

" Dst Symbols
syn match DstSymbol '\v<%([a-zA-Z!$&*_+=|<.>?-])+%([a-zA-Z0-9!#$%&*_+=|'<.>/?-])*>'

" DST numbers
syn match DstReal '\v<[-+]?%(\d+|\d+\.\d*)%([eE][-+]?\d+)?>'
syn match DstInteger '\v<[-+]?%(\d+)>'

syn match DstConstant 'nil' 
syn match DstConstant 'true' 
syn match DstConstant 'false'

" Dst Keywords
syn match DstKeyword '\v<:[a-zA-Z0-9_\-]*>'

syntax match DstQuote "'"

" -*- TOP CLUSTER -*-
syntax cluster DstTop contains=@Spell,DstComment,DstConstant,DstQuote,DstKeyword,DstSymbol,DstInteger,DstReal,DstString,DstTuple,DstArray,DstTable,DstStruct

syntax region DstTuple matchgroup=DstParen start="("  end=")" contains=@DstTop fold
syntax region DstArray matchgroup=DstParen start="\[" end="]" contains=@DstTop fold
syntax region DstTable matchgroup=DstParen start="{"  end="}" contains=@DstTop fold
syntax region DstStruct matchgroup=DstParen start="@{"  end="}" contains=@DstTop fold

" Highlight superfluous closing parens, brackets and braces.
syntax match DstError "]\|}\|)"

syntax sync fromstart

" Highlighting
hi def link DstComment Comment
hi def link DstSymbol Identifier
hi def link DstInteger Number
hi def link DstReal Type
hi def link DstConstant Constant
hi def link DstKeyword Keyword
hi def link DstString String
hi def link DstStringDelimiter String

hi def link DstQuote SpecialChar
hi def link DstParen Delimiter

let b:current_syntax = "dst"

let &cpo = s:cpo_sav
unlet! s:cpo_sav
