<?xml version="1.0"?>

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

  <xsl:output method='html' />

  <xsl:template match="logfile">
    <html>
      <head>
        <script type="text/javascript" src="treebits.js" />
        <link rel="stylesheet" href="logfile.css" type="text/css" />
        <title>Log File</title>
      </head>
      <body>
        <ul class='toplevel'>
          <xsl:for-each select='line|nest'>
            <li>
              <xsl:apply-templates select='.'/>
            </li>
          </xsl:for-each>
        </ul>
      </body>
    </html>
  </xsl:template>

  
  <xsl:template match="nest">

    <!-- The tree should be collapsed by default if all children are
         unimportant or if the header is unimportant. -->
<!--    <xsl:variable name="collapsed"
                  select="count(.//line[not(@priority = 3)]) = 0 or ./head[@priority = 3]" /> -->
    <xsl:variable name="collapsed" select="count(.//*[@error]) = 0"/>
                  
    <xsl:variable name="style"><xsl:if test="$collapsed">display: none;</xsl:if></xsl:variable>
    <xsl:variable name="arg"><xsl:choose><xsl:when test="$collapsed">true</xsl:when><xsl:otherwise>false</xsl:otherwise></xsl:choose></xsl:variable>
    
    <script type='text/javascript'>showTreeToggle(<xsl:value-of select="$collapsed"/>)</script>
    <xsl:apply-templates select='head'/>
    
    <ul class='nesting' style="{$style}">
      <xsl:for-each select='line|nest'>

        <!-- Is this the last line?  If so, mark it as such so that it
             can be rendered differently. -->
        <xsl:variable  name="class"><xsl:choose><xsl:when test="position() != last()">line</xsl:when><xsl:otherwise>lastline</xsl:otherwise></xsl:choose></xsl:variable>
        
        <li class='{$class}'>
          <span class='lineconn' />
          <span class='linebody'>
            <xsl:apply-templates select='.'/>
          </span>
        </li>
      </xsl:for-each>
    </ul>
  </xsl:template>

  
  <xsl:template match="head|line">
    <code>
      <xsl:if test="@error">
        <xsl:attribute name="class">error</xsl:attribute>
      </xsl:if>
      <xsl:apply-templates/>
    </code>
  </xsl:template>

  
  <xsl:template match="storeref">
    <em class='storeref'>
      <span class='popup'><xsl:apply-templates/></span>
      <span class='elided'>/...</span><xsl:apply-templates select='name'/><xsl:apply-templates select='path'/>
    </em>
  </xsl:template>
  
</xsl:stylesheet>