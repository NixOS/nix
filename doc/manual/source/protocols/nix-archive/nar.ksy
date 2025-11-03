meta:
  id: nix_nar
  title: Nix Archive (NAR)
  file-extension: nar
  endian: le
doc: |
    Nix Archive (NAR) format. A simple, reproducible binary archive
    format used by the Nix package manager to serialize file system objects.
doc-ref: 'https://nixos.org/manual/nix/stable/command-ref/nix-store.html#nar-format'

seq:
  - id: magic
    type: padded_str
    doc: "Magic string, must be 'nix-archive-1'."
    valid:
      expr: _.body == 'nix-archive-1'
  - id: root_node
    type: node
    doc: "The root of the archive, which is always a single node."

types:
  padded_str:
    doc: |
      A string, prefixed with its length (u8le) and
      padded with null bytes to the next 8-byte boundary.
    seq:
      - id: len_str
        type: u8
      - id: body
        type: str
        size: len_str
        encoding: 'ASCII'
      - id: padding
        size: (8 - (len_str % 8)) % 8

  node:
    doc: "A single filesystem node (file, directory, or symlink)."
    seq:
      - id: open_paren
        type: padded_str
        doc: "Must be '(', a token starting the node definition."
        valid:
          expr: _.body == '('
      - id: type_key
        type: padded_str
        doc: "Must be 'type'."
        valid:
          expr: _.body == 'type'
      - id: type_val
        type: padded_str
        doc: "The type of the node: 'regular', 'directory', or 'symlink'."
      - id: body
        type:
          switch-on: type_val.body
          cases:
            "'directory'": type_directory
            "'regular'": type_regular
            "'symlink'": type_symlink
      - id: close_paren
        type: padded_str
        valid:
          expr: _.body == ')'
        if: "type_val.body != 'directory'"
        doc: "Must be ')', a token ending the node definition."

  type_directory:
    doc: "A directory node, containing a list of entries. Entries must be ordered by their names."
    seq:
      - id: entries
        type: dir_entry
        repeat: until
        repeat-until: _.kind.body == ')'
    types:
      dir_entry:
        doc: "A single entry within a directory, or a terminator."
        seq:
          - id: kind
            type: padded_str
            valid:
              expr: _.body == 'entry' or _.body == ')'
            doc: "Must be 'entry' (for a child node) or '' (for terminator)."
          - id: open_paren
            type: padded_str
            valid:
              expr: _.body == '('
            if: 'kind.body == "entry"'
          - id: name_key
            type: padded_str
            valid:
              expr: _.body == 'name'
            if: 'kind.body == "entry"'
          - id: name
            type: padded_str
            if: 'kind.body == "entry"'
          - id: node_key
            type: padded_str
            valid:
              expr: _.body == 'node'
            if: 'kind.body == "entry"'
          - id: node
            type: node
            if: 'kind.body == "entry"'
            doc: "The child node, present only if kind is 'entry'."
          - id: close_paren
            type: padded_str
            valid:
              expr: _.body == ')'
            if: 'kind.body == "entry"'
        instances:
          is_terminator:
            value: kind.body == ')'

  type_regular:
    doc: "A regular file node."
    seq:
      # Read attributes (like 'executable') until we hit 'contents'
      - id: attributes
        type: reg_attribute
        repeat: until
        repeat-until: _.key.body == "contents"
      # After the 'contents' token, read the file data
      - id: file_data
        type: file_content
    instances:
      is_executable:
        value: 'attributes[0].key.body == "executable"'
        doc: "True if the file has the 'executable' attribute."
    types:
      reg_attribute:
        doc: "An attribute of the file, e.g., 'executable' or 'contents'."
        seq:
          - id: key
            type: padded_str
            doc: "Attribute key, e.g., 'executable' or 'contents'."
            valid:
              expr: _.body == 'executable' or _.body == 'contents'
          - id: value
            type: padded_str
            if: 'key.body == "executable"'
            valid:
              expr: _.body == ''
            doc: "Must be '' if key is 'executable'."
      file_content:
        doc: "The raw data of the file, prefixed by length."
        seq:
          - id: len_contents
            type: u8
          # # This relies on the property of instances that they are lazily evaluated and cached.
          - size: 0
            if: nar_offset < 0
          - id: contents
            size: len_contents
          - id: padding
            size: (8 - (len_contents % 8)) % 8
        instances:
          nar_offset:
            value: _io.pos

  type_symlink:
    doc: "A symbolic link node."
    seq:
      - id: target_key
        type: padded_str
        doc: "Must be 'target'."
        valid:
          expr: _.body == 'target'
      - id: target_val
        type: padded_str
        doc: "The destination path of the symlink."
