(defun nix-mode ()
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

  (interactive)
  
  (kill-all-local-variables)
  
  (setq major-mode 'nix-mode)
  (setq mode-name "Nix")

  (use-local-map nix-mode-map)

  (set-syntax-table nix-mode-syntax-table)

  ;; Font lock support.
  (setq font-lock-defaults '(nix-keywords nil nil nil nil))

  ;; Automatic indentation [C-j].
  (make-local-variable 'indent-line-function)
  (setq indent-line-function 'nix-indent-line)

  ;; Indenting of comments.
  (make-local-variable 'comment-start)
  (setq comment-start "# ")
  (make-local-variable 'comment-end)
  (setq comment-end "")
  (make-local-variable 'comment-start-skip)
  (setq comment-start-skip "\\(^\\|\\s-\\);?#+ *")

  ;; Filling of comments.
  (make-local-variable 'adaptive-fill-mode)
  (setq adaptive-fill-mode t)
  (make-local-variable 'paragraph-start)
  (setq paragraph-start "[ \t]*\\(#+[ \t]*\\)?$")
  (make-local-variable 'paragraph-separate)
  (setq paragraph-separate paragraph-start)
  
  (run-hooks 'nix-mode-hook)
  )


(defvar nix-mode-map nil
  "Keymap for Nix mode.")

(setq nix-mode-map (make-sparse-keymap))
;(define-key nix-mode-map [tab] 'tab-to-tab-stop)


(defvar nix-keywords
  '("\\<if\\>" "\\<then\\>" "\\<else\\>" "\\<assert\\>" "\\<with\\>"
    "\\<let\\>" "\\<in\\>" "\\<rec\\>" "\\<inherit\\>"
    ("\\<true\\>" . font-lock-builtin-face)
    ("\\<false\\>" . font-lock-builtin-face)
    ("\\<null\\>" . font-lock-builtin-face)
    ("\\<import\\>" . font-lock-builtin-face)
    ("\\<derivation\\>" . font-lock-builtin-face)
    ("\\<baseNameOf\\>" . font-lock-builtin-face)
    ("\\<toString\\>" . font-lock-builtin-face)
    ("\\<isNull\\>" . font-lock-builtin-face)
    ("\\<\\([a-zA-Z_][a-zA-Z0-9_'\.]*\\)[ \t]*="
     (1 font-lock-variable-name-face nil nil))
    ("[a-zA-Z][a-zA-Z0-9\\+-\\.]*:[a-zA-Z0-9%/\\?:@&=\\+\\$,_\\.!~\\*'-]+"
     . font-lock-constant-face)
    ("[a-zA-Z0-9._\\+-]*\\(/[a-zA-Z0-9._\\+-]+\\)+"
     . font-lock-constant-face)
    ))


(defvar nix-mode-syntax-table nil
  "Syntax table for Nix mode.")

(if nix-mode-syntax-table
    nil
    (progn
      (setq nix-mode-syntax-table (make-syntax-table))
      (modify-syntax-entry ?/ ". 14" nix-mode-syntax-table)
      (modify-syntax-entry ?* ". 23" nix-mode-syntax-table)
      (modify-syntax-entry ?# "< b" nix-mode-syntax-table)
      (modify-syntax-entry ?\n "> b" nix-mode-syntax-table)
      ))


(defun nix-indent-line ()
  "Indent current line in a Nix expression."
  (interactive)
  (indent-relative-maybe))


(setq auto-mode-alist (cons '("\\.nix\\'" . nix-mode) auto-mode-alist))
(setq auto-mode-alist (cons '("\\.nix.in\\'" . nix-mode) auto-mode-alist))
