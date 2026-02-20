rec {

  /* Dit is
  een test. */

  x = 
  # Dit is een test.y;
  
  y = 123;

  # CR or CR/LF (but not explicit \r's) in strings should be
  # translated to LF.
  foo = "multiline
  string
  test\r";

  z = 456;}
