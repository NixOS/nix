# A simple comment
"a"+ # And another
## A double comment
"b"+  ## And another
# Nested # comments #
"c"+   # and # some # other #
# An empty line, following here:

"d"+      # and a comment not starting the line !

"e"+
/* multiline comments */
"f" +
/* multiline
   comments,
   on
   multiple
   lines
*/
"g" +
# Small, tricky comments
/**/ "h"+ /*/*/ "i"+ /***/ "j"+ /* /*/ "k"+ /*/* /*/ "l"+
# Comments with an even number of ending '*' used to fail:
"m"+
/* */ /* **/ /* ***/ /* ****/ "n"+
/* */ /** */ /*** */ /**** */ "o"+
/** **/ /*** ***/ /**** ****/ "p"+
/* * ** *** **** ***** */     "q"+
# Random comments
/* ***** ////// * / * / /* */ "r"+
# Mixed comments
/* # */
"s"+
# /* #
"t"+
# /* # */
"u"+
# /*********/
"v"+
## */*
"w"+
/*
 * Multiline, decorated comments
 * # This ain't a nest'd comm'nt
 */
"x"+
''${/** with **/"y"
  # real
  /* comments
     inside ! # */

  # (and empty lines)

}''+          /* And a multiline comment,
                 on the same line,
                 after some spaces
*/             # followed by a one-line comment
"z"
/* EOF */
