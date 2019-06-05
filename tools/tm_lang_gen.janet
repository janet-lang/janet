# Helper to generate core library mappings for janet
# Used to help build the tmLanguage grammar. Emits
# the entire .tmLanguage file for janet.

# Use dynamic binding and make this the first 
# expression in the file to not pollute (all-bindings)
(setdyn :allsyms  
  (array/concat
    @["break"
      "def"
      "do"
      "var"
      "set"
      "fn"
      "while"
      "if"
      "quote"
      "quasiquote"
      "unquote"
      "splice"]
    (all-bindings)))
(def allsyms (dyn :allsyms))

(def grammar-template
`````
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>fileTypes</key>
  <array>
    <string>janet</string>
  </array>
  <key>foldingStartMarker</key>
  <string>\{</string>
  <key>foldingStopMarker</key>
  <string>\}</string>
  <key>foldingStartMarker</key>
  <string>\[</string>
  <key>foldingStopMarker</key>
  <string>\]</string>
  <key>foldingStartMarker</key>
  <string>\(</string>
  <key>foldingStopMarker</key>
  <string>\)</string>
  <key>keyEquivalent</key>
  <string>^~L</string>
  <key>name</key>
  <string>Janet</string>
  <key>patterns</key>
  <array>
    <dict>
      <key>include</key>
      <string>#all</string>
    </dict>
    </array>
  <key>repository</key>
  <dict>
    <key>all</key>
    <dict>
      <key>patterns</key>
      <array>
        <dict>
          <key>include</key>
          <string>#comment</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#parens</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#brackets</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#braces</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#readermac</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#string</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#longstring</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#literal</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#corelib</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#r-number</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#dec-number</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#hex-number</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#keysym</string>
        </dict>
        <dict>
          <key>include</key>
          <string>#symbol</string>
        </dict>
      </array>
    </dict>
    <key>comment</key>
    <dict>
      <key>captures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.comment.janet</string>
        </dict>
      </dict>
      <key>match</key>
      <string>(#).*$</string>
      <key>name</key>
      <string>comment.line.janet</string>
    </dict>
    <key>braces</key>
    <dict>
      <key>begin</key>
      <string>(@?{)</string>
      <key>captures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.braces.begin.janet</string>
        </dict>
      </dict>
      <key>end</key>
      <string>(})</string>
      <key>captures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.braces.end.janet</string>
        </dict>
      </dict>
      <key>patterns</key>
      <array>
        <dict>
          <key>include</key>
          <string>#all</string>
        </dict>
      </array>
    </dict>
    <key>brackets</key>
    <dict>
      <key>begin</key>
      <string>(@?\[)</string>
      <key>captures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.brackets.begin.janet</string>
        </dict>
      </dict>
      <key>end</key>
      <string>(\])</string>
      <key>captures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.brackets.end.janet</string>
        </dict>
      </dict>
      <key>patterns</key>
      <array>
        <dict>
          <key>include</key>
          <string>#all</string>
        </dict>
      </array>
    </dict>
    <key>parens</key>
    <dict>
      <key>begin</key>
      <string>(@?\()</string>
      <key>captures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.parens.begin.janet</string>
        </dict>
      </dict>
      <key>end</key>
      <string>(\))</string>
      <key>captures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.parens.end.janet</string>
        </dict>
      </dict>
      <key>patterns</key>
      <array>
        <dict>
          <key>include</key>
          <string>#all</string>
        </dict>
      </array>
    </dict>
    <key>readermac</key>
    <dict>
      <key>match</key>
      <string>[\'\~\;\,]</string>
      <key>name</key>
      <string>punctuation.other.janet</string>
    </dict>
    <!-- string>(?&lt;![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*]) token match here (?![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])</string -->
    <key>literal</key>
    <dict>
      <key>match</key>
      <string>(?&lt;![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])(true|false|nil)(?![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])</string>
      <key>name</key>
      <string>constant.language.janet</string>
    </dict>
    <key>corelib</key>
    <dict>
      <key>match</key>
            <string>(?&lt;![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])(%ALLSYMBOLS%)(?![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])</string>
      <key>name</key>
      <string>keyword.control.janet</string>
    </dict>
    <key>keysym</key>
    <dict>
      <key>match</key>
      <string>(?&lt;![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*]):[\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*]*</string>
      <key>name</key>
      <string>constant.keyword.janet</string>
    </dict>
    <key>symbol</key>
    <dict>
      <key>match</key>
      <string>(?&lt;![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])[\.a-zA-Z_\-=!@\$%^&amp;?|\\/&lt;&gt;*][\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*]*</string>
      <key>name</key>
      <string>variable.other.janet</string>
    </dict>
    <key>hex-number</key>
    <dict>
      <key>match</key>
      <string>(?&lt;![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])[-+]?0x([_\da-fA-F]+|[_\da-fA-F]+\.[_\da-fA-F]*|\.[_\da-fA-F]+)(&amp;[+-]?[\da-fA-F]+)?(?![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])</string>
      <key>name</key>
      <string>constant.numeric.hex.janet</string>
    </dict>
    <key>dec-number</key>
    <dict>
      <key>match</key>
      <string>(?&lt;![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])[-+]?([_\d]+|[_\d]+\.[_\d]*|\.[_\d]+)([eE&amp;][+-]?[\d]+)?(?![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])</string>
      <key>name</key>
      <string>constant.numeric.decimal.janet</string>
    </dict>
    <key>r-number</key>
    <dict>
      <key>match</key>
      <string>(?&lt;![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])[-+]?\d\d?r([_\w]+|[_\w]+\.[_\w]*|\.[_\w]+)(&amp;[+-]?[\w]+)?(?![\.:\w_\-=!@\$%^&amp;?|\\/&lt;&gt;*])</string>
      <key>name</key>
      <string>constant.numeric.decimal.janet</string>
    </dict>
    <key>string</key>
    <dict>
      <key>begin</key>
      <string>(@?")</string>
      <key>beginCaptures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.string.begin.janet</string>
        </dict>
      </dict>
      <key>end</key>
      <string>(")</string>
      <key>endCaptures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.string.end.janet</string>
        </dict>
      </dict>
      <key>name</key>
      <string>string.quoted.double.janet</string>
      <key>patterns</key>
      <array>
        <dict>
          <key>match</key>
          <string>(\\[nevr0zft"\\']|\\x[0-9a-fA-F][0-9a-fA-f])</string>
          <key>name</key>
          <string>constant.character.escape.janet</string>
        </dict>
      </array>
    </dict>
    <key>longstring</key>
    <dict>
      <key>begin</key>
      <string>(@?)(`+)</string>
      <key>beginCaptures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.string.begin.janet</string>
        </dict>
        <key>2</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.string.begin.janet</string>
        </dict>
      </dict>
      <key>end</key>
      <string>\2</string>
      <key>endCaptures</key>
      <dict>
        <key>1</key>
        <dict>
          <key>name</key>
          <string>punctuation.definition.string.end.janet</string>
        </dict>
      </dict>
      <key>name</key>
      <string>string.quoted.triple.janet</string>
    </dict>
    <key>nomatch</key>
    <dict>
      <key>match</key>
      <string>\S+</string>
      <key>name</key>
      <string>invalid.illegal.janet</string>
    </dict>
  </dict>
    <key>scopeName</key>
    <string>source.janet</string>
    <key>uuid</key>
    <string>3743190f-20c4-44d0-8640-6611a983296b</string>
</dict>
</plist>
`````)

# Now we generate the bindings in the language.

(def- escapes
  {(get "|" 0) `\|`
   (get "-" 0) `\-`
   (get "+" 0) `\+`
   (get "*" 0) `\*`
   (get "^" 0) `\^`
   (get "$" 0) `\$`
   (get "?" 0) `\?`
   38 "&amp;"
   60 "&lt;"
   62 "&gt;"
   34 "&quot;"
   39 "&#39;"
   47 "&#47;"})

(defn- escape
  "Escape special characters for HTML and regex encoding."
  [str]
  (def buf @"")
  (loop [byte :in str]
    (if-let [rep (get escapes byte)]
      (buffer/push-string buf rep)
      (buffer/push-byte buf byte)))
  buf)

(def pattern (string/join (map escape allsyms) "|"))

(print (string/replace "%ALLSYMBOLS%" pattern grammar-template))
