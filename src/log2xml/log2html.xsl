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
    <script type='text/javascript'>showTreeToggle("show","hide")</script>
    <xsl:apply-templates select='head'/>
    <ul class='nesting'>
      <xsl:for-each select='line|nest'>
        <xsl:param name="class"><xsl:choose><xsl:when test="position() != last()">line</xsl:when><xsl:otherwise>lastline</xsl:otherwise></xsl:choose></xsl:param>
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