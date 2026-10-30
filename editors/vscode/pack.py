#!/usr/bin/env python3
# .vsix 만들기 (vsce 없이). .vsix 는 정해진 꼴의 zip 이다:
#   [Content_Types].xml, extension.vsixmanifest, extension/<확장 파일들>
# 사용:  python3 pack.py   ->  hanlang-<버전>.vsix
import json, os, zipfile
from xml.sax.saxutils import escape

here = os.path.dirname(os.path.abspath(__file__))
pkg = json.load(open(os.path.join(here, 'package.json'), encoding='utf-8'))
files = ['package.json', 'language-configuration.json', 'extension.js', 'debug.js', 'README.md',
         'syntaxes/hanlang.tmLanguage.json', 'snippets/hanlang.json']
out = os.path.join(here, f"hanlang-{pkg['version']}.vsix")

content_types = '''<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json"/>
  <Default Extension=".js" ContentType="application/javascript"/>
  <Default Extension=".md" ContentType="text/markdown"/>
  <Default Extension=".vsixmanifest" ContentType="text/xml"/>
</Types>
'''
manifest = f'''<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="{pkg['name']}" Version="{pkg['version']}" Publisher="{pkg['publisher']}"/>
    <DisplayName>{escape(pkg['displayName'])}</DisplayName>
    <Description xml:space="preserve">{escape(pkg['description'])}</Description>
    <Categories>{escape(','.join(pkg['categories']))}</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="{pkg['engines']['vscode']}"/>
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace"/>
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code"/>
  </Installation>
  <Dependencies/>
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true"/>
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true"/>
  </Assets>
</PackageManifest>
'''
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('[Content_Types].xml', content_types)
    z.writestr('extension.vsixmanifest', manifest)
    for f in files:
        z.write(os.path.join(here, f), 'extension/' + f)
print(out)
