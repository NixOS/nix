;;; nix-mode.el --- Major mode for editing Nix expressions

;; Author: Eelco Dolstra
;; URL: https://github.com/NixOS/nix/tree/master/misc/emacs
;; Version: 1.0

;;; Commentary:

;;; Code:

(defconst nix-font-lock-keywords
  '("\\<if\\>" "\\<then\\>" "\\<else\\>" "\\<assert\\>" "\\<with\\>"
    "\\<let\\>" "\\<in\\>" "\\<rec\\>" "\\<inherit\\>" "\\<or\\>"
    ("\\<true\\>" . font-lock-builtin-face)
    ("\\<false\\>" . font-lock-builtin-face)
    ("\\<null\\>" . font-lock-builtin-face)
    ("\\<import\\>" . font-lock-builtin-face)
    ("\\<derivation\\>" . font-lock-builtin-face)
    ("\\<baseNameOf\\>" . font-lock-builtin-face)
    ("\\<toString\\>" . font-lock-builtin-face)
    ("\\<isNull\\>" . font-lock-builtin-face)
    ("[a-zA-Z][a-zA-Z0-9\\+-\\.]*:[a-zA-Z0-9%/\\?:@&=\\+\\$,_\\.!~\\*'-]+"
     . font-lock-constant-face)
    ("\\<\\([a-zA-Z_][a-zA-Z0-9_'\-\.]*\\)[ \t]*="
     (1 font-lock-variable-name-face nil nil))
    ("<[a-zA-Z0-9._\\+-]+\\(/[a-zA-Z0-9._\\+-]+\\)*>"
     . font-lock-constant-face)
    ("[a-zA-Z0-9._\\+-]*\\(/[a-zA-Z0-9._\\+-]+\\)+"
     . font-lock-constant-face))
  "Font lock keywords for nix.")

(defvar nix-mode-syntax-table
  (let ((table (make-syntax-table)))
    (modify-syntax-entry ?/ ". 14" table)
    (modify-syntax-entry ?* ". 23" table)
    (modify-syntax-entry ?# "< b" table)
    (modify-syntax-entry ?\n "> b" table)
    table)
  "Syntax table for Nix mode.")

(defun nix-indent-line ()
  "Indent current line in a Nix expression."
  (interactive)
  (indent-relative-maybe))


;;;###autoload
(define-derived-mode nix-mode prog-mode "Nix"
  "Major mode for editing Nix expressions.

The following commands may be useful:

  '\\[newline-and-indent]'
    Insert a newline and move the cursor to align with the previous
    non-empty line.

  '\\[fill-paragraph]'
    Refill a paragraph so that all lines are at most `fill-column'
    lines long.  This should do the right thing for comments beginning
    with `#'.  However, this command doesn't work properly yet if the
    comment is adjacent to code (i.e., no intervening empty lines).
    In that case, select the text to be refilled and use
    `\\[fill-region]' instead.

The hook `nix-mode-hook' is run when Nix mode is started.

\\{nix-mode-map}
"
  (set-syntax-table nix-mode-syntax-table)

  ;; Font lock support.
  (setq font-lock-defaults '(nix-font-lock-keywords nil nil nil nil))

  ;; Automatic indentation [C-j].
  (set (make-local-variable 'indent-line-function) 'nix-indent-line)

  ;; Indenting of comments.
  (set (make-local-variable 'comment-start) "# ")
  (set (make-local-variable 'comment-end) "")
  (set (make-local-variable 'comment-start-skip) "\\(^\\|\\s-\\);?#+ *")

  ;; Filling of comments.
  (set (make-local-variable 'adaptive-fill-mode) t)
  (set (make-local-variable 'paragraph-start) "[ \t]*\\(#+[ \t]*\\)?$")
  (set (make-local-variable 'paragraph-separate) paragraph-start))


;;;###autoload
(progn
  (add-to-list 'auto-mode-alist '("\\.nix\\'" . nix-mode))
  (add-to-list 'auto-mode-alist '("\\.nix.in\\'" . nix-mode)))

(provide 'nix-mode)

;;; nix-mode.el ends here
