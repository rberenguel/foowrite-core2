#include <gtest/gtest.h>
#include "../main/editor.h"
#include "../main/editor_mode.h"
#include "./editor_stubs.hpp"

TEST(FooWrite, TextObjects_DD) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123\nU\x29");
    auto line = editor.GetCurrentLine();
    auto doc = editor.GetDocument();
    EXPECT_EQ(line, "123");
    EXPECT_EQ(doc, "123\n\n");
    SendString(&editor, "ddi42");
    line = editor.GetCurrentLine();
    SendString(&editor, "D");
    doc = editor.GetDocument();
    EXPECT_EQ(doc, "42\n");
}

TEST(FooWrite, TextObjects_DD_2) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123\x29");
    SendString(&editor, "ddi456U");
    auto line = editor.GetCurrentLine();
    auto doc = editor.GetDocument();
    EXPECT_EQ(line, "456");
    EXPECT_EQ(doc, "456\n");
}

TEST(FooWrite, TextObjects_DD_EmptyLine) {
    // Regression: dd on an empty line must erase it from the document,
    // not merely clear its string content.
    Output out;
    Editor editor;
    editor.Init(&out);
    // Build: two lines — "foo" and "" (cursor lands on "" after Enter + Esc).
    SendString(&editor, "ifoo\n\x29");
    auto count_before = editor.CountLines();
    EXPECT_EQ(count_before, 2);
    SendString(&editor, "dd");
    auto count_after = editor.CountLines();
    EXPECT_EQ(count_after, 1);
    auto doc = editor.GetDocument();
    EXPECT_EQ(doc, "foo\n");
}

TEST(FooWrite, TextObjects_DDollar) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123L\x29");
    auto line = editor.GetCurrentLine();
    EXPECT_EQ(line, "123");
    SendString(&editor, "d");
    auto mods = KeyModifiers{};
    mods.shift = true;
    editor.ProcessKey(KEY_4, &mods, false);
    mods.shift = false;
    SendString(&editor, "iab");
    SendString(&editor, "D");
    line = editor.GetCurrentLine();
    EXPECT_EQ(line, "ab1");
}

TEST(FooWrite, TextObjects_CDollar) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor,
               "i123L\x29"
               "c");
    auto line = editor.GetCurrentLine();
    EXPECT_EQ(line, "123");
    auto mods = KeyModifiers{};
    mods.shift = true;
    editor.ProcessKey(KEY_4, &mods, false);
    mods.shift = false;
    SendString(&editor, "1");
    line = editor.GetCurrentLine();
    EXPECT_EQ(line, "11");
}

TEST(FooWrite, TextObjects_daw) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123 456 789 00\x29");
    auto mods = KeyModifiers{};
    mods.ctrl = true;
    editor.ProcessKey(KEY_A, &mods, false);
    mods.ctrl = false;
    SendString(&editor, "i0\x29wwi6\x29");
    auto line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456 6789 00");
    SendString(&editor, "daw");
    line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456 00");
}

TEST(FooWrite, TextObjects_daw_at_end) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123 456 789 00\x29");
    auto mods = KeyModifiers{};
    mods.ctrl = true;
    editor.ProcessKey(KEY_A, &mods, false);
    mods.ctrl = false;
    SendString(&editor, "i0\x29wwi6\x29");
    auto line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456 6789 00");
    SendString(&editor, "wdaw");
    line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456 6789");
}

TEST(FooWrite, TextObjects_diw) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123 456 789 00\x29");
    auto mods = KeyModifiers{};
    mods.ctrl = true;
    editor.ProcessKey(KEY_A, &mods, false);
    mods.ctrl = false;
    SendString(&editor, "i0\x29wwdiw");
    auto line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456  00");
}

TEST(FooWrite, TextObjects_caw_mid) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123 456 789 00\x29");
    auto mods = KeyModifiers{};
    mods.ctrl = true;
    editor.ProcessKey(KEY_A, &mods, false);
    mods.ctrl = false;
    SendString(&editor, "i0\x29wwcaw999");
    auto line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456 99900");
}

TEST(FooWrite, TextObjects_caw_end) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123 456 789 00\x29");
    auto mods = KeyModifiers{};
    mods.ctrl = true;
    editor.ProcessKey(KEY_A, &mods, false);
    mods.ctrl = false;
    SendString(&editor, "i0\x29wwwcaw999");
    auto line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456 789999");
}

TEST(FooWrite, TextObjects_dawi_at_end) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123 456 789 00\x29");
    auto mods = KeyModifiers{};
    mods.ctrl = true;
    editor.ProcessKey(KEY_A, &mods, false);
    mods.ctrl = false;
    SendString(&editor, "i0\x29wwi6\x29");
    auto line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456 6789 00");
    SendString(&editor, "wdawifoo");
    line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456 678foo9");
}

TEST(FooWrite, TextObjects_caw_at_end) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123 456 789 00\x29");
    auto mods = KeyModifiers{};
    mods.ctrl = true;
    editor.ProcessKey(KEY_A, &mods, false);
    mods.ctrl = false;
    SendString(&editor, "i0\x29wwwcaw999");
    auto line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456 789999");
}

TEST(FooWrite, TextObjects_ciw) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "i123 456 789 00\x29");
    auto mods = KeyModifiers{};
    mods.ctrl = true;
    editor.ProcessKey(KEY_A, &mods, false);
    mods.ctrl = false;
    SendString(&editor, "i0\x29wwciw999");
    auto line = editor.GetCurrentLine();
    EXPECT_EQ(line, "0123 456 999 00");
}
