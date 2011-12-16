<?xml version="1.0" encoding="utf-8"?>

<!--
  Copyright (c) 2011 Red Hat, Inc.

  All rights reserved.

  Author: Jan Friesse (jfriesse@redhat.com)

  This software licensed under BSD license, the text of which follows:

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  - Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  - Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
  - Neither the name of the Red Hat, Inc. nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
  THE POSSIBILITY OF SUCH DAMAGE.
-->

<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:date="http://exslt.org/dates-and-times" extension-element-prefixes="date">

<xsl:output method="text" />
<xsl:strip-space elements="*" />
<xsl:param name="inputfile"/>

<xsl:variable name='newline'><xsl:text>
</xsl:text></xsl:variable>

<xsl:template match="/corosync">
    <xsl:text># Corosync configuration file generated from </xsl:text>
    <xsl:value-of select="$inputfile"/><xsl:text> at </xsl:text>
    <xsl:value-of select="date:date-time()"/>

    <xsl:apply-templates select="@*"/>
    <xsl:apply-templates />
    <xsl:value-of select="$newline" />
</xsl:template>

<xsl:template match="/corosync//*">
    <xsl:value-of select="$newline" />
    <xsl:value-of select="$newline" />
    <xsl:call-template name="indent">
	<xsl:with-param name="depth" select="count(ancestor::*) - 1"/>
    </xsl:call-template>
    <xsl:value-of select="name()"/> {<xsl:apply-templates select="@*"/>
    <xsl:apply-templates />
    <xsl:value-of select="$newline" />
    <xsl:call-template name="indent">
	<xsl:with-param name="depth" select="count(ancestor::*) - 1"/>
    </xsl:call-template>
    <xsl:text>}</xsl:text>
</xsl:template>

<xsl:template match="@*">
    <xsl:value-of select="$newline" />
    <xsl:call-template name="indent">
	<xsl:with-param name="depth" select="count(ancestor::*) - 1"/>
    </xsl:call-template>
    <xsl:value-of select="name()"/><xsl:text>: </xsl:text><xsl:value-of select="."/>
</xsl:template>

<xsl:template match="text()">
</xsl:template>

<xsl:template name="indent">
    <xsl:param name="depth"/>
    <xsl:if test="$depth &gt; 0">
    <xsl:text>    </xsl:text>
    <xsl:call-template name="indent">
	<xsl:with-param name="depth" select="$depth - 1"/>
    </xsl:call-template>
    </xsl:if>
</xsl:template>

</xsl:stylesheet>
