> PR_COMMENT Notes:
> + Untracked files won't go into build results (at least, they don't seem to do so for me; changed but uncommitted files are ok.)
> + A [discourse thread]() on how to build the docs from the repo; this is how I do it:
>   ```
>   nix build -vv .#nix^doc --extra-experimental-features nix-command --extra-experimental-features flakes
>   ```
>   It takes about 2m 40s, not sure what I'm doing wrong...
> + It should be noted that these docs use mdBook - and document where to find help because the docs I found wasn't much help... [Their docs](https://rust-lang.github.io/mdBook/) looks incomplete. E.g., how to link to other pages? I constantly get "link errors" when I do the same format that is used on the `command-ref` pages.
