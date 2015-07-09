let

  s1 = ''
    This is an indented multi-line string
    literal.  An amount of whitespace at
    the start of each line matching the minimum
    indentation of all lines in the string
    literal together will be removed.  Thus,
    in this case four spaces will be
    stripped from each line, even though
      THIS LINE is indented six spaces.

    Also, empty lines don't count in the
    determination of the indentation level (the
    previous empty line has indentation 0, but
    it doesn't matter).
      
    Still, empty lines are not completely ignored,
    as the previous line has two spaces over the
    smallest indent, and these are preserved.
  '';

  s2 = ''  If the string starts with whitespace
    followed by a newline, it's stripped, but
    that's not the case here. Two spaces are
    stripped because of the "  " at the start. 
  '';

  s3 = ''
      This line is indented
      a bit further.
        ''; # indentation of last line doesn't count if it's empty

  s4 = ''
    Anti-quotations, like ${if true then "so" else "not so"}, are
    also allowed.
  '';

  s5 = ''
      The \ is not special here.
    ' can be followed by any character except another ', e.g. 'x'.
    Likewise for $, e.g. $$ or $varName.
    But ' followed by ' is special, as is $ followed by {.
    If you want them, use anti-quotations: ${"''"}, ${"\${"}.
  '';

  s6 = ''  
    Tabs are not interpreted as whitespace (since we can't guess
    what tab settings are intended), so don't use them.
 	This line starts with a space and a tab, so only one
    space will be stripped from each line.
  '';

  s7 = ''
    Also note that if the last line (just before the closing ' ')
    consists only of whitespace, it's ignored.  But here there is
    some non-whitespace stuff, so the line isn't removed. '';

  s8 = ''    ${""}${""}
    This shows a hacky way to preserve an empty line after the start.
    But there's no reason to do so: you could just repeat the empty
    line. The anti-quote must be doubled, lest it is ignored.
  '';

  s9 = ''
  ${""}  Similarly you can force an indentation level,
    in this case to 2 spaces.  This works because the anti-quote
    is significant (not whitespace).
  '';

  s10 = ''
  '';

  s11 = '''';

  s12 = ''   '';

  s13 = ''
    start on network-interfaces

    start script
    
      rm -f /var/run/opengl-driver
      ${if true
        then "ln -sf 123 /var/run/opengl-driver"
        else if true
        then "ln -sf 456 /var/run/opengl-driver"
        else ""
      }

      rm -f /var/log/slim.log
       
    end script

    env SLIM_CFGFILE=${"abc"}
    env SLIM_THEMESDIR=${"def"}
    env FONTCONFIG_FILE=/etc/fonts/fonts.conf  				# !!! cleanup
    env XKB_BINDIR=${"foo"}/bin         				# Needed for the Xkb extension.
    env LD_LIBRARY_PATH=${"libX11"}/lib:${"libXext"}/lib:/usr/lib/          # related to xorg-sys-opengl - needed to load libglx for (AI)GLX support (for compiz)

    ${if true
      then "env XORG_DRI_DRIVER_PATH=${"nvidiaDrivers"}/X11R6/lib/modules/drivers/"
    else if true
      then "env XORG_DRI_DRIVER_PATH=${"mesa"}/lib/modules/dri"
      else ""
    } 

    exec ${"slim"}/bin/slim
  '';

  s14 = ''
    Escaping of ' followed by ': '''
    Escaping of $ followed by {: ''${
    And finally to interpret \n etc. as in a string: ''\n, ''\r, ''\t.
  '';

  # Regression test: antiquotation in '${x}' should work, but didn't.
  s15 = let x = "bla"; in ''
    foo
    '${x}'
    bar
  '';

  # Test single antiquotation on its own line
  s16 = ''
    Some antiquotations evaluate to multi-line content.
    In that case, all the lines are indented to respect at least the
    indentation of the antiquotation itself.

      ${''
          This is why this line will be indented,
          and this one too !
        ''}

  '';

  s17 = ''
    Antiquotations on their own lines are very special, as they collapse when
    their content evaluates to an empty string.
    ${""}
    Therefore, these two paragraphs look like one.

  '';

  s18 = ''
    Also, antiquotation on their own line do not introduce empty lines when
    it is not strictly necessary.

    We have already shown that embedding an empty string does not introduce
    ${""}
    any empty line, but embedding an indented string also skips the extraneous
    ${''
        newline
      ''}
    in such a way that these lines look contiguous.

  '';

  s19 = ''
    ${ "Such antiquotations" }
      ${ "are\nintuitive\n" }
    ${''
        and very nice !

      ''}
  '';

  s20 = let test = ''test''; in ''
    This mechanism only works when the antiquotations are left alone on their own line.
    As soon as the antiquotation is surrounded by text or other antiquotations,
    it gets back to the behaviour described above.
      ${test} ${test}
      test ${test}
      ${test} test

  '';

in s1  + s2  + s3  + s4  + s5  + s6  + s7  + s8  + s9  + s10
 + s11 + s12 + s13 + s14 + s15 + s16 + s17 + s18 + s19 + s20
