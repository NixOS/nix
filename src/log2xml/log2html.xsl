<?xml version="1.0"?>

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  
  <xsl:template match="logfile">
    <html>
      <head>
        <link rel="stylesheet" href="logfile.css" type="text/css" />
        <title>Log File</title>
      </head>
      <body>
        <xsl:apply-templates/>
      </body>
    </html>
  </xsl:template>

  <xsl:template match="nest">
    <div class='nesting'>
      <div class='head'>
        <code>
          <xsl:value-of select="head"/>
        </code>
      </div>
      <blockquote class='body'>
        <xsl:for-each select='line|nest'>
          <xsl:if test="position() != last()">
            <table class='x'>
              <tr class='x'>
                <td class='dummy'>
                  <div class='dummy' />
                </td>
                <td>
                  <xsl:apply-templates select='.'/>
                </td>
              </tr>
            </table>
          </xsl:if>
          <xsl:if test="position() = last()">
            <table class='y'>
              <tr class='y'>
                <td class='dummy'>
                  <div class='dummy' />
                </td>
                <td>
                  <xsl:apply-templates select='.'/>
                </td>
              </tr>
            </table>
          </xsl:if>
        </xsl:for-each>
      </blockquote>
    </div>
  </xsl:template>
  
  <xsl:template match="line">
    <code class='line'>
      <xsl:value-of select="."/>
    </code>
  </xsl:template>
  
</xsl:stylesheet>