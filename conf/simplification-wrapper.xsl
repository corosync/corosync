<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:import href="simplification.xsl"/>

<xsl:param name="step" select="'20'"/>
<xsl:variable name="step_concat" select="concat('step7.', $step)"/>

<xsl:template match="/">
    <xsl:choose>
        <xsl:when test="$step = '20'">
            <xsl:apply-templates mode="step7.20">
                <xsl:with-param name="out" select="0"/>
                <xsl:with-param name="stop-after" select="$step_concat"/>
            </xsl:apply-templates>
        </xsl:when>
        <xsl:otherwise>
            <xsl:message terminate="yes">
                <xsl:value-of select="concat('Not implemented: ', $step_concat)"/>
            </xsl:message>
        </xsl:otherwise>
    </xsl:choose>
</xsl:template>

</xsl:stylesheet>
