# Foowrite Core2 — Bug Fixes

## 1. `dd` on an empty line did not reduce document size

### Symptom
Using `dd` on an empty line (e.g. after pressing Enter at the end of a paragraph and
immediately pressing Escape) would appear to delete the line visually, but the document
size did not decrease.  Repeated `dd` on empty lines produced an ever-growing list of
empty entries in the document.

### Root cause
The original foowrite separated "paragraphs" with a `---` sentinel stored as a real
document entry.  When `dd` was applied to an empty line it would only clear the string
(`*row_ = ""`), but never erase the list node, because the `document_.size() > 1`
guard was absent.  The core2 port inherited this logic but the `---` sentinel was
removed, so every document line can be a legitimate content line — including empty ones
that the user explicitly created.

### Fix (`main/editor.cpp`)
The `dd` branch now erases the list node when there is more than one entry:

```cpp
case 'd':
    if (command_line_str == "d") {
        if (document_.size() > 1 && row_ != document_.end()) {
            row_ = document_.erase(row_);
            if (row_ == document_.end()) --row_;
            current_line_ = *row_;
        } else {
            current_line_.clear();
            if (row_ != document_.end()) *row_ = "";
        }
        ncolumn_ = 0;
        command_line_ = {};
    }
```

### Regression test
`tests/test_text_objects.cc` — `FooWrite.TextObjects_DD_EmptyLine`: creates a two-line
document, calls `dd` on the empty second line, and asserts that `CountLines()` drops
from 2 to 1 and `GetDocument()` equals `"foo\n"`.
