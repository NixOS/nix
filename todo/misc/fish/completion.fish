function _nix_complete
  # Get the current command up to a cursor.
  # - Behaves correctly even with pipes and nested in commands like env.
  # - TODO: Returns the command verbatim (does not interpolate variables).
  #   That might not be optimal for arguments like -f.
  set -l nix_args (commandline --current-process --tokenize --cut-at-cursor)
  # --cut-at-cursor with --tokenize removes the current token so we need to add it separately.
  # https://github.com/fish-shell/fish-shell/issues/7375
  # Can be an empty string.
  set -l current_token (commandline --current-token --cut-at-cursor)

  # Nix wants the index of the argv item to complete but the $nix_args variable
  # also contains the program name (argv[0]) so we would need to subtract 1.
  # But the variable also misses the current token so it cancels out.
  set -l nix_arg_to_complete (count $nix_args)

  env NIX_GET_COMPLETIONS=$nix_arg_to_complete $nix_args $current_token
end

function _nix_accepts_files
  set -l response (_nix_complete)
  test $response[1] = 'filenames'
end

function _nix
  set -l response (_nix_complete)
  # Skip the first line since it handled by _nix_accepts_files.
  # Tail lines each contain a command followed by a tab character and, optionally, a description.
  # This is also the format fish expects.
  string collect -- $response[2..-1]
end

# Disable file path completion if paths do not belong in the current context.
complete --command nix --condition 'not _nix_accepts_files' --no-files

complete --command nix --arguments '(_nix)'
