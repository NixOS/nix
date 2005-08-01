<?xml version="1.0"?>

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

  <xsl:template match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()"/>
    </xsl:copy>
  </xsl:template>

  <xsl:template match="line">
    <line>
      <xsl:if test="contains(text(), ' *** ') or
                    contains(text(), 'LaTeX Error') or
                    contains(text(), 'FAIL:') or
                    contains(text(), ' error: ') or
                    true">
         <xsl:attribute name="error"></xsl:attribute>
      </xsl:if>
      <xsl:apply-templates select="@*|node()"/>
    </line>
  </xsl:template>
  
</xsl:stylesheet>