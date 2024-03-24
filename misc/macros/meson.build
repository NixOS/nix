# include.macros file
#========================================================================


# macro used to generate R headers
#---------------------------------------------------
gen_rheader = '''
  echo 'R"foo(' >> "$1"
  cat @INPUT@ >> "$1"
  echo ')foo"' >> "$1"
'''
