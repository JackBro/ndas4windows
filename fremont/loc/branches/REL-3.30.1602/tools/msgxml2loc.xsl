<?xml version="1.0" encoding="utf-8"?>

<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns="http://schemas.ximeta.com/meta/loc/2006">

  <xsl:param name="component" />
  <xsl:param name="version" />
  <xsl:param name="lang">enu</xsl:param>
  <xsl:param name="oem">base</xsl:param>

  <xsl:output method="xml" indent="yes" encoding="utf-8" cdata-section-elements="base translation" />

  <xsl:template match="/">
	<!--
    <xsl:processing-instruction name="xml-stylesheet">
      <xsl:text>type="text/xsl" href="locview.xsl"</xsl:text>
    </xsl:processing-instruction>
	-->
    <localization>
	  <xsl:attribute name="component"><xsl:value-of select="$component"/></xsl:attribute>
	  <xsl:attribute name="version"><xsl:value-of select="$version"/></xsl:attribute>
	  <xsl:attribute name="lang"><xsl:value-of select="$lang"/></xsl:attribute>
	  <xsl:attribute name="oem"><xsl:value-of select="$oem"/></xsl:attribute>
      <xsl:apply-templates select="/mc/messages"/>
    </localization>
  </xsl:template>

  <xsl:template match="message">
    <xsl:choose>
      <xsl:when test="starts-with(./text[@lang='ENU']/text(),'CR_')"></xsl:when>
      <xsl:otherwise>
        <string>
          <xsl:attribute name="id">
            <xsl:value-of select="@symbolicName"/>
          </xsl:attribute>
          <base>
          <xsl:value-of select="./text[@lang='ENU']/text()"/>
          </base>
          <translation>
            <xsl:text>-</xsl:text>
          </translation>
        </string>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="*">
    <xsl:apply-templates />
  </xsl:template>

  <xsl:template match="text()">
    <xsl:apply-templates />
  </xsl:template>

</xsl:stylesheet>
