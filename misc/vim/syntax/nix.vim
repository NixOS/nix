" Vim syntax file
" Language:	nix
" Maintainer:	Marc Weber <marco-oweber@gmx.de>
"               Modify and commit if you feel that way
" Last Change:	2007 Dec

" Quit when a (custom) syntax file was already loaded
if exists("b:current_syntax")
  finish
endif

syn keyword	nixKeyword	let throw inherit import true false null with
syn keyword	nixConditional	if else then
syn keyword     nixBrace        ( ) { } =
syn keyword     nixBuiltin         __currentSystem __currentTime __isFunction __getEnv __trace __toPath __pathExists 
  \ __readFile __toXML __toFile __filterSource __attrNames __getAttr __hasAttr __isAttrs __listToAttrs __isList 
  \ __head __tail __add __sub __lessThan __substring __stringLength

syn match nixAttr "\w\+\ze\s*="
syn match nixFuncArg "\zs\w\+\ze\s*:"
syn region nixStringParam start=+\${+ end=+}+
syn region nixMultiLineComment start=+/\*+ skip=+\\"+ end=+\*/+
syn match  nixEndOfLineComment "#.*$"
syn region nixString      start=+"+ skip=+\\"+ end=+"+ contains=nixStringParam

hi def link nixKeyword       Keyword
hi def link nixConditional   Conditional
hi def link nixBrace         Special
hi def link nixString        String
hi def link nixBuiltin       Special
hi def link nixStringParam   Macro
hi def link nixMultiLineComment Comment
hi def link nixEndOfLineComment Comment
hi def link nixAttr        Identifier
hi def link nixFuncArg     Identifier
